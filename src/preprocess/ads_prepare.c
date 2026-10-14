/* Member A's C11 command-line pipeline. Run from the repository root.
 * Every TSV header matches member B's supplied index implementation.
 * No default stop list is silently loaded. Counters/positions are 64 bit.
 */
#include "preprocess/AdsPreprocess.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DOC_HEADER "doc_id\twork_title\tsource_url\tclean_path"
#define SOURCE_HEADER "doc_id\twork_title\tsource_url\traw_path\tclean_path\textraction\tstart_marker"
#define COUNT_HEADER "word\tcorpus_tf\tdocument_df\tdocument_ratio\ttoken_ratio"

typedef struct { char *word; uint64_t tf, df, last_doc; double dr, tr; } Word;
typedef struct { Word *slots; size_t capacity, size; uint64_t tokens, documents; } Counts;
/* 只将人工确认的功能词作为候选；高频主题词不会仅因 TF/DF 高而被删。 */
typedef struct { const char *name, *filename; double tf, df; } Threshold;
static const Threshold thresholds[] = {
    {"conservative", "stopwords_conservative.txt", .001, .50},
    {"balanced", "stopwords.txt", .0005, .25},
    {"aggressive", "stopwords_aggressive.txt", .0001, .10}
};

static int fail(const char *what, const char *path)
{ fprintf(stderr, "%s: %s\n", what, path ? path : ""); return 1; }
static char *copy(const char *s)
{ size_t n = strlen(s)+1; char *p = malloc(n); if (p) memcpy(p,s,n); return p; }
static int fields(char *s, char **out, size_t count)
{
    size_t i; out[0] = s;
    for (i=1; i<count; ++i) { char *p = strchr(out[i-1],'\t'); if (!p) return 0; *p=0; out[i]=p+1; }
    return strchr(out[count-1], '\t') == NULL;
}
static int number(const char *s, uint64_t *out)
{
    uint64_t n=0;
    if (!*s) return 0;
    for (; *s; ++s) { unsigned d=(unsigned)(*s-'0'); if (d>9 || n>(UINT64_MAX-d)/10) return 0; n=n*10+d; }
    *out=n; return 1;
}
static int header(FILE *f, const char *expected)
{
    char *line=NULL; AdsPreprocessStatus s=ads_pp_read_line(f,&line);
    int ok=s==ADS_PP_OK && line && !strcmp(line,expected);
    free(line); return ok;
}
static uint64_t hash(const char *s)
{ uint64_t h=UINT64_C(14695981039346656037); for (;*s;++s) { h^=(unsigned char)*s; h*=UINT64_C(1099511628211); } return h; }
static int grow(Counts *c)
{
    /* 开放寻址哈希表，容量为 2 的幂；装载率达到约 0.7 时扩容重排。 */
    size_t n=c->capacity ? c->capacity*2 : 16384, i; Word *slots;
    if (n<c->capacity || n>SIZE_MAX/sizeof(*slots)) return 0;
    slots=calloc(n,sizeof(*slots)); if (!slots) return 0;
    for(i=0;i<c->capacity;++i) if(c->slots[i].word) {
        size_t j=(size_t)(hash(c->slots[i].word)&(n-1));
        while(slots[j].word) j=(j+1)&(n-1);
        slots[j]=c->slots[i];
    }
    free(c->slots); c->slots=slots; c->capacity=n; return 1;
}
static Word *get_word(Counts *c,const char *word)
{
    size_t i;
    if(!c->capacity || c->size>=c->capacity*7/10) if(!grow(c)) return NULL;
    i=(size_t)(hash(word)&(c->capacity-1));
    while(c->slots[i].word && strcmp(c->slots[i].word,word)) i=(i+1)&(c->capacity-1);
    if(!c->slots[i].word) { c->slots[i].word=copy(word); if(!c->slots[i].word) return NULL; c->slots[i].last_doc=UINT64_MAX; ++c->size; }
    return &c->slots[i];
}
static void counts_free(Counts *c)
{ size_t i; for(i=0;i<c->capacity;++i) free(c->slots[i].word); free(c->slots); }
static int rank_words(const void *a,const void *b)
{
    const Word *x=*(Word *const *)a,*y=*(Word *const *)b;
    return x->tf==y->tf ? strcmp(x->word,y->word) : (x->tf>y->tf ? -1 : 1);
}
static int alpha_words(const void *a,const void *b)
{ return strcmp((*(Word *const *)a)->word,(*(Word *const *)b)->word); }
static Word **word_array(Counts *c,int alpha)
{
    Word **p=malloc((c->size?c->size:1)*sizeof(*p)); size_t i,n=0;
    if(!p) return NULL;
    for(i=0;i<c->capacity;++i) if(c->slots[i].word) p[n++]=&c->slots[i];
    if(n>1) qsort(p,n,sizeof(*p),alpha?alpha_words:rank_words);
    return p;
}
static char *path_join(const char *a,const char *b)
{
    size_t n=strlen(a),m=strlen(b); char *p;
    if(n>SIZE_MAX-m-2) return NULL;
    p=malloc(n+m+2); if(p) snprintf(p,n+m+2,"%s/%s",a,b); return p;
}

static int clean(const char *sources,const char *documents)
{
    FILE *in=ads_pp_fopen(sources,"rb"),*out=NULL;
    char *line=NULL; uint64_t previous=0, docs=0; AdsPreprocessStatus s=ADS_PP_OK; int rc=1;
    if(!in) return fail("Cannot read source manifest",sources);
    if(!header(in,SOURCE_HEADER)) { fail("Invalid source header",sources); goto done; }
    if(!ads_pp_mkdirs("data/clean")) { fail("Cannot create clean directory","data/clean"); goto done; }
    out=ads_pp_fopen(documents,"wb"); if(!out) { fail("Cannot write document manifest",documents); goto done; }
    fprintf(out,"%s\n",DOC_HEADER);
    while((s=ads_pp_read_line(in,&line))==ADS_PP_OK && line) {
        char *p[7],*html=NULL,*text=NULL; uint64_t id; AdsHtmlMode mode;
        if(!fields(line,p,7) || !number(p[0],&id) || !id || id>UINT32_MAX || id<=previous) {
            fail("Invalid or unordered source row",line); goto done;
        }
        if(!strcmp(p[5],"blockquote")) mode=ADS_HTML_BLOCKQUOTES;
        else if(!strcmp(p[5],"table")) mode=ADS_HTML_TABLE;
        else { fail("Unknown extraction mode",p[5]); goto done; }
        s=ads_pp_read_text(p[3],&html);
        if(s==ADS_PP_OK) s=ads_pp_html_text(html,mode,p[6],&text);
        free(html);
        if(s!=ADS_PP_OK || !text || !*text) {
            free(text); fail("HTML extraction failed or produced empty text",p[3]); goto done;
        }
        s=ads_pp_write_text(p[4],text); free(text);
        if(s!=ADS_PP_OK) { fail("Cannot write clean document",p[4]); goto done; }
        if(fprintf(out,"%s\t%s\t%s\t%s\n",p[0],p[1],p[2],p[4])<0) goto done;
        previous=id; ++docs; free(line); line=NULL;
    }
    if(s!=ADS_PP_OK || ferror(out)) goto done;
    fprintf(stderr,"Cleaned %" PRIu64 " documents.\n",docs); rc=0;
done:
    free(line); fclose(in); if(out && fclose(out)) rc=1; return rc;
}

static int count_words(const char *documents,const char *output)
{
    FILE *in=ads_pp_fopen(documents,"rb"),*out=NULL; Counts c={0}; Word **rank=NULL;
    char *line=NULL; uint64_t previous=0; size_t i; int rc=1; AdsPreprocessStatus s=ADS_PP_OK;
    if(!in) return fail("Cannot read document manifest",documents);
    if(!header(in,DOC_HEADER)) { fail("Invalid document header",documents); goto done; }
    while((s=ads_pp_read_line(in,&line))==ADS_PP_OK && line) {
        char *p[4],*text=NULL; uint64_t id; AdsTokenList tokens={0};
        if(!fields(line,p,4)||!number(p[0],&id)||!id||id>UINT32_MAX||id<=previous) goto done;
        s=ads_pp_read_text(p[3],&text);
        if(s==ADS_PP_OK) s=ads_pp_tokenize(text,&tokens);
        free(text);
        if(s!=ADS_PP_OK) { fail("Cannot tokenize document",p[3]); goto done; }
        for(i=0;i<tokens.count;++i) {
            Word *w=get_word(&c,tokens.tokens[i].surface);
            if(!w||w->tf==UINT64_MAX||c.tokens==UINT64_MAX) { ads_pp_tokens_free(&tokens); goto done; }
            ++w->tf; ++c.tokens;
            /* 文档按 ID 升序读取。同词在同一文档重复出现只增加 TF，
             * last_doc 保证 DF 对每篇文档只加一次，无需每词保存文档集合。 */
            if(w->last_doc!=id) { ++w->df; w->last_doc=id; }
        }
        ads_pp_tokens_free(&tokens); ++c.documents; previous=id; free(line); line=NULL;
    }
    if(s!=ADS_PP_OK) goto done;
    rank=word_array(&c,0); if(!rank) goto done;
    out=ads_pp_fopen(output,"wb"); if(!out) { fail("Cannot write word counts",output); goto done; }
    fprintf(out,"%s\n",COUNT_HEADER);
    for(i=0;i<c.size;++i) {
        Word *w=rank[i];
        fprintf(out,"%s\t%" PRIu64 "\t%" PRIu64 "\t%.12f\t%.12f\n",w->word,w->tf,w->df,
                c.documents?(double)w->df/(double)c.documents:0.0,c.tokens?(double)w->tf/(double)c.tokens:0.0);
    }
    if(ferror(out)) goto done;
    fprintf(stderr,"Counted %" PRIu64 " tokens, %zu surface words, %" PRIu64 " documents.\n",c.tokens,c.size,c.documents); rc=0;
    fprintf(stderr,"Hash slots: %zu; sizeof(Word): %zu bytes.\n",c.capacity,sizeof(Word));
done:
    free(line); free(rank); counts_free(&c); fclose(in); if(out&&fclose(out)) rc=1; return rc;
}

static int qualifies(const Word *w,const Threshold *t,const AdsPreprocessor *candidates)
/* 同时满足功能词候选、全语料词次占比、文档覆盖率，才成为停用词。 */
{ return ads_pp_is_stopword(candidates,w->word) && w->tr+1e-12>=t->tf && w->dr+1e-12>=t->df; }
static int select_stops(const char *stats,const char *candidates_path,const char *config_dir,const char *results_dir)
{
    Counts c={0}; FILE *in=ads_pp_fopen(stats,"rb"),*summary=NULL,*decisions=NULL;
    AdsPreprocessor *candidates=NULL; Word **words=NULL; char *line=NULL,*p_summary=NULL,*p_decisions=NULL;
    AdsPreprocessStatus s=ADS_PP_OK; size_t i,t; int rc=1;
    if(!in) return fail("Cannot read word counts",stats);
    if(!header(in,COUNT_HEADER)) goto done;
    s=ads_pp_create(candidates_path,&candidates); if(s!=ADS_PP_OK) goto done;
    while((s=ads_pp_read_line(in,&line))==ADS_PP_OK&&line) {
        char *p[5],*end; Word *w; uint64_t tf,df; double dr,tr;
        if(!fields(line,p,5)||!number(p[1],&tf)||!number(p[2],&df)) goto done;
        dr=strtod(p[3],&end); if(*end||dr<0||dr>1) goto done;
        tr=strtod(p[4],&end); if(*end||tr<0||tr>1) goto done;
        w=get_word(&c,p[0]); if(!w||w->tf||UINT64_MAX-c.tokens<tf) goto done;
        w->tf=tf; w->df=df; w->dr=dr; w->tr=tr; c.tokens+=tf;
        free(line); line=NULL;
    }
    if(s!=ADS_PP_OK||!ads_pp_mkdirs(config_dir)||!ads_pp_mkdirs(results_dir)) goto done;
    words=word_array(&c,1); if(!words) goto done;
    p_summary=path_join(results_dir,"stopword_thresholds.tsv"); p_decisions=path_join(results_dir,"stopword_decisions.tsv");
    if(!p_summary||!p_decisions) goto done;
    summary=ads_pp_fopen(p_summary,"wb"); decisions=ads_pp_fopen(p_decisions,"wb"); if(!summary||!decisions) goto done;
    fprintf(summary,"policy\tmin_token_ratio\tmin_document_ratio\tstopword_count\tremoved_tokens\tretained_tokens\tremoved_ratio\n");
    for(t=0;t<3;++t) {
        char *path=path_join(config_dir,thresholds[t].filename); FILE *out;
        uint64_t removed=0; size_t selected=0;
        if(!path) goto done;
        out=ads_pp_fopen(path,"wb"); free(path); if(!out) goto done;
        for(i=0;i<c.size;++i) if(qualifies(words[i],&thresholds[t],candidates)) {
            fprintf(out,"%s\n",words[i]->word); removed+=words[i]->tf; ++selected;
        }
        if(ferror(out)) { fclose(out); goto done; }
        if(fclose(out)) goto done;
        fprintf(summary,"%s\t%.6f\t%.6f\t%zu\t%" PRIu64 "\t%" PRIu64 "\t%.9f\n",
                thresholds[t].name,thresholds[t].tf,thresholds[t].df,selected,removed,c.tokens-removed,
                c.tokens?(double)removed/(double)c.tokens:0.0);
    }
    fprintf(decisions,"word\tcorpus_tf\tdocument_df\tdocument_ratio\ttoken_ratio\tfunction_candidate\tconservative\tbalanced\taggressive\n");
    for(i=0;i<c.size;++i) {
        Word *w=words[i];
        fprintf(decisions,"%s\t%" PRIu64 "\t%" PRIu64 "\t%.12f\t%.12f\t%d\t%d\t%d\t%d\n",
                w->word,w->tf,w->df,w->dr,w->tr,ads_pp_is_stopword(candidates,w->word),
                qualifies(w,&thresholds[0],candidates),qualifies(w,&thresholds[1],candidates),qualifies(w,&thresholds[2],candidates));
    }
    if(ferror(summary)||ferror(decisions)) goto done;
    fprintf(stderr,"Selected 3 reviewed stopword policies; balanced is config/stopwords.txt.\n"); rc=0;
done:
    free(line); free(words); free(p_summary); free(p_decisions); counts_free(&c); ads_pp_destroy(candidates); fclose(in);
    if(summary&&fclose(summary)) rc=1;
    if(decisions&&fclose(decisions)) rc=1;
    return rc;
}

static int emit(const char *documents,const char *stops,const char *output,const char *doc_stats)
{
    FILE *in=ads_pp_fopen(documents,"rb"),*out=NULL,*stats=NULL; AdsPreprocessor *ctx=NULL;
    char *line=NULL; AdsPreprocessStatus s=ADS_PP_OK; uint64_t previous=0,total=0,docs=0; int rc=1;
    if(!in) return fail("Cannot read document manifest",documents);
    if(!header(in,DOC_HEADER)) goto done;
    s=ads_pp_create(!strcmp(stops,"-")?NULL:stops,&ctx); if(s!=ADS_PP_OK) { fail("Cannot load stopwords",stops); goto done; }
    out=ads_pp_fopen(output,"wb"); stats=ads_pp_fopen(doc_stats,"wb"); if(!out||!stats) goto done;
    fprintf(out,"doc_id\tposition\tstem\n");
    fprintf(stats,"doc_id\toriginal_token_count\tindexed_token_count\tremoved_stopword_count\n");
    while((s=ads_pp_read_line(in,&line))==ADS_PP_OK&&line) {
        char *p[4],*text=NULL; AdsTokenList tokens={0}; uint64_t id,indexed=0; size_t i;
        if(!fields(line,p,4)||!number(p[0],&id)||!id||id>UINT32_MAX||id<=previous) goto done;
        s=ads_pp_read_text(p[3],&text);
        if(s==ADS_PP_OK) s=ads_pp_process(ctx,text,1,&tokens);
        free(text);
        if(s!=ADS_PP_OK) { fail("Cannot preprocess document",p[3]); goto done; }
        /* 先按原词形排除停用词，再输出词干；直接保留原始 position，
         * 禁止按输出行号重编号，否则连续短语的间隔信息会丢失。 */
        for(i=0;i<tokens.count;++i) if(!tokens.tokens[i].is_stopword) {
            fprintf(out,"%" PRIu64 "\t%" PRIu64 "\t%s\n",id,tokens.tokens[i].position,tokens.tokens[i].stem); ++indexed;
        }
        fprintf(stats,"%" PRIu64 "\t%zu\t%" PRIu64 "\t%" PRIu64 "\n",id,tokens.count,indexed,(uint64_t)tokens.count-indexed);
        total+=indexed; ++docs; previous=id; ads_pp_tokens_free(&tokens); free(line); line=NULL;
    }
    if(s!=ADS_PP_OK||ferror(out)||ferror(stats)) goto done;
    fprintf(stderr,"Emitted %" PRIu64 " indexed positions from %" PRIu64 " documents.\n",total,docs); rc=0;
done:
    free(line); fclose(in); if(out&&fclose(out)) rc=1; if(stats&&fclose(stats)) rc=1; ads_pp_destroy(ctx); return rc;
}
int main(int argc,char **argv)
{
    clock_t start=clock(); int rc;
    if(argc==4&&!strcmp(argv[1],"clean")) rc=clean(argv[2],argv[3]);
    else if(argc==4&&!strcmp(argv[1],"count")) rc=count_words(argv[2],argv[3]);
    else if(argc==6&&!strcmp(argv[1],"select")) rc=select_stops(argv[2],argv[3],argv[4],argv[5]);
    else if(argc==6&&!strcmp(argv[1],"emit")) rc=emit(argv[2],argv[3],argv[4],argv[5]);
    else {
        fprintf(stderr,"Usage:\n  ads_prepare clean sources.tsv documents.tsv\n"
                "  ads_prepare count documents.tsv counts.tsv\n"
                "  ads_prepare select counts.tsv function_words.txt config_dir results_dir\n"
                "  ads_prepare emit documents.tsv stopwords.txt|- tokens.tsv doc_stats.tsv\n"); return 2;
    }
    fprintf(stderr,"clock() seconds: %.6f\n",(double)(clock()-start)/CLOCKS_PER_SEC);
    if(rc) fprintf(stderr,"Pipeline failed; inspect input paths and TSV formats.\n");
    return rc;
}
