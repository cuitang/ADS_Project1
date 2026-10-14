/* Integration oracle: independently read every clean document, call A's
 * public API, and compare selected terms' exact IDs/positions with B's
 * reloaded disk index. This tests the A -> TSV -> B -> disk -> B boundary. */
#include "preprocess/AdsPreprocess.h"
#include "index/AdsIndex.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *query; char *stem; AdsPostingListView view; size_t cursor; } Probe;
int main(int argc,char **argv)
{
    Probe probes[]={ {"king",NULL,{0},0}, {"queen",NULL,{0},0}, {"love",NULL,{0},0},
        {"runs",NULL,{0},0}, {"walked",NULL,{0},0}, {"not",NULL,{0},0},
        {"no",NULL,{0},0}, {"wherefore",NULL,{0},0}, {"the",NULL,{0},0}, {"zzzznotaword",NULL,{0},0},
        {"having",NULL,{0},0}, {"its",NULL,{0},0} };
    size_t q,n=sizeof(probes)/sizeof(probes[0]);
    AdsPreprocessor *ctx=NULL; AdsIndex *index=NULL; AdsIndexInfo info;
    FILE *docs=NULL; char *line=NULL; uint64_t count=0,positions=0; int rc=1;
    if(argc!=4) { fprintf(stderr,"Usage: test_full_index documents.tsv stopwords.txt index_dir\n"); return 2; }
    if(ads_pp_create(argv[2],&ctx)!=ADS_PP_OK || ads_index_open(argv[3],&index)!=ADS_INDEX_OK) goto done;
    if(ads_index_get_info(index,&info)!=ADS_INDEX_OK) goto done;
    for(q=0;q<n;++q) {
        AdsTokenList query={0}; AdsIndexStatus s;
        if(ads_pp_process(ctx,probes[q].query,0,&query)!=ADS_PP_OK) goto done;
        if(query.count) {
            probes[q].stem=malloc(strlen(query.tokens[0].stem)+1);
            if(!probes[q].stem) { ads_pp_tokens_free(&query); goto done; }
            strcpy(probes[q].stem,query.tokens[0].stem);
            s=ads_index_lookup_stem(index,probes[q].stem,&probes[q].view);
            if(s!=ADS_INDEX_OK&&s!=ADS_INDEX_NOT_FOUND) { ads_pp_tokens_free(&query); goto done; }
        }
        ads_pp_tokens_free(&query);
    }
    docs=ads_pp_fopen(argv[1],"rb"); if(!docs) goto done;
    if(ads_pp_read_line(docs,&line)!=ADS_PP_OK||!line||strcmp(line,"doc_id\twork_title\tsource_url\tclean_path")) goto done;
    free(line); line=NULL;
    for(;;) {
        char *tabs[3],*p,*text=NULL; uint32_t id; AdsTokenList tokens={0}; size_t i,k;
        if(ads_pp_read_line(docs,&line)!=ADS_PP_OK) goto done;
        if(!line) break;
        p=line;
        for(k=0;k<3;++k) { tabs[k]=strchr(p,'\t'); if(!tabs[k]) goto done; *tabs[k]=0; p=tabs[k]+1; }
        id=(uint32_t)strtoul(line,NULL,10);
        if(ads_pp_read_text(p,&text)!=ADS_PP_OK||ads_pp_process(ctx,text,0,&tokens)!=ADS_PP_OK) { free(text); goto done; }
        free(text); positions+=(uint64_t)tokens.count;
        for(q=0;q<n;++q) if(probes[q].stem) {
            const AdsPosting *posting=NULL; size_t found=0;
            if(probes[q].cursor<probes[q].view.posting_count && probes[q].view.postings[probes[q].cursor].doc_id==id)
                posting=&probes[q].view.postings[probes[q].cursor];
            for(i=0;i<tokens.count;++i) if(!strcmp(tokens.tokens[i].stem,probes[q].stem)) {
                if(!posting||found>=posting->position_count||posting->positions[found]!=tokens.tokens[i].position) {
                    fprintf(stderr,"Mismatch: doc=%u query=%s position=%" PRIu64 "\n",id,probes[q].query,tokens.tokens[i].position);
                    ads_pp_tokens_free(&tokens); goto done;
                }
                ++found;
            }
            if(posting) { if(found!=posting->position_count) { ads_pp_tokens_free(&tokens); goto done; } ++probes[q].cursor; }
        }
        ++count; ads_pp_tokens_free(&tokens); free(line); line=NULL;
    }
    if(count!=info.document_count||positions!=info.indexed_position_count) goto done;
    for(q=0;q<n;++q) if(probes[q].cursor!=probes[q].view.posting_count) goto done;
    printf("Full-corpus index integration PASS: documents=%" PRIu64 ", indexed_positions=%" PRIu64 ", probe_queries=%zu\n",count,positions,n);
    rc=0;
done:
    if(rc) fprintf(stderr,"Full-corpus index integration FAILED.\n");
    for(q=0;q<n;++q) free(probes[q].stem);
    free(line); if(docs) fclose(docs); ads_pp_destroy(ctx); ads_index_close(index); return rc;
}
