#include "preprocess/AdsPreprocess.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(expr) do { ++checks; if(!(expr)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); return 1; } } while(0)

static int test_html(void)
{
    char *text=NULL;
    CHECK(ads_pp_html_text("<title>NOISE</title><table>HOME</table><b>ACTOR</b>"
          "<blockquote><i>Enter KING</i><br>hel<b>lo</b> &amp; &#107;ing&nbsp;queen."
          "<!-- bad words --><script>if (x < 2) NOISE;</script></blockquote>",ADS_HTML_BLOCKQUOTES,"",&text)==ADS_PP_OK);
    CHECK(!strcmp(text,"Enter KING\nhello & king queen.\n")); free(text);
    CHECK(ads_pp_html_text("<p>EDITOR</p><table><tr><td>5<td>Since time<tr><td><td>his end</table>",ADS_HTML_TABLE,NULL,&text)==ADS_PP_OK);
    CHECK(strstr(text,"Since time")&&strstr(text,"his end")&&!strstr(text,"EDITOR")); free(text);
    CHECK(ads_pp_html_text("<blockquote>DEDICATION</blockquote><blockquote>EVEN as the sun</blockquote>",ADS_HTML_BLOCKQUOTES,"EVEN as the sun",&text)==ADS_PP_OK);
    CHECK(!strcmp(text,"EVEN as the sun\n")); free(text);
    CHECK(ads_pp_html_text("<blockquote>abc",ADS_HTML_BLOCKQUOTES,"missing marker",&text)==ADS_PP_FORMAT_ERROR);
    CHECK(ads_pp_html_text("<blockquote><!-- broken",ADS_HTML_BLOCKQUOTES,NULL,&text)==ADS_PP_FORMAT_ERROR);
    CHECK(ads_pp_html_text("<blockquote>A</blockquote>",ADS_HTML_BLOCKQUOTES,NULL,&text)==ADS_PP_OK);
    CHECK(!strcmp(text,"A\n")); free(text);
    CHECK(ads_pp_html_text("<blockquote>First line<br><a NAME = 'speech2'><b>ACTOR</b></a>"
          "<blockquote>Second line<p><i>Exit</i><table>Shakespeare homepage</table>",
          ADS_HTML_BLOCKQUOTES,NULL,&text)==ADS_PP_OK);
    CHECK(!strcmp(text,"First line\nSecond line\nExit\n")); free(text);
    return 0;
}
static int test_tokens(void)
{
    AdsPreprocessor *ctx=NULL; AdsTokenList list={0}; char *stemmed=NULL; char long_word[2049];
    CHECK(ads_pp_create("data/test/demo_stopwords.txt",&ctx)==ADS_PP_OK);
    CHECK(ads_pp_process(ctx,"The king runs and walks.",0,&list)==ADS_PP_OK);
    CHECK(list.count==3);
    CHECK(!strcmp(list.tokens[0].stem,"king")&&list.tokens[0].position==1);
    CHECK(!strcmp(list.tokens[1].stem,"run")&&list.tokens[1].position==2);
    CHECK(!strcmp(list.tokens[2].stem,"walk")&&list.tokens[2].position==4);
    ads_pp_tokens_free(&list);
    CHECK(ads_pp_process(ctx,"king and queen",1,&list)==ADS_PP_OK);
    CHECK(list.count==3&&list.tokens[1].is_stopword&&!strcmp(list.tokens[1].surface,"and"));
    ads_pp_tokens_free(&list);
    CHECK(ads_pp_process(ctx,"king the queen",1,&list)==ADS_PP_OK);
    CHECK(list.count==3&&list.tokens[1].is_stopword&&!strcmp(list.tokens[1].surface,"the"));
    ads_pp_tokens_free(&list);
    CHECK(ads_pp_process(ctx,"A THE and",0,&list)==ADS_PP_OK&&list.count==0); ads_pp_tokens_free(&list);
    CHECK(ads_pp_tokenize("King's NE'ER ice-cold 123_QUEEN",&list)==ADS_PP_OK);
    CHECK(list.count==7&&!strcmp(list.tokens[0].surface,"king")&&!strcmp(list.tokens[1].surface,"s")&&
          !strcmp(list.tokens[6].surface,"queen")); ads_pp_tokens_free(&list);
    CHECK(ads_pp_tokenize("\t 123 --\n",&list)==ADS_PP_OK&&list.count==0); ads_pp_tokens_free(&list);
    CHECK(ads_pp_tokenize("",&list)==ADS_PP_OK&&list.count==0); ads_pp_tokens_free(&list);
    memset(long_word,'a',sizeof(long_word)-1); long_word[sizeof(long_word)-1]=0;
    CHECK(ads_pp_tokenize(long_word,&list)==ADS_PP_OK&&list.count==1&&strlen(list.tokens[0].surface)==2048);
    ads_pp_tokens_free(&list);
    CHECK(ads_pp_stem(ctx,"walked",&stemmed)==ADS_PP_OK&&!strcmp(stemmed,"walk")); free(stemmed);
    CHECK(ads_pp_stem(ctx,"KING",&stemmed)==ADS_PP_INVALID_ARGUMENT);
    ads_pp_destroy(ctx); ctx=NULL;
    CHECK(ads_pp_create("does-not-exist.stopwords",&ctx)==ADS_PP_IO_ERROR&&ctx==NULL);
    CHECK(ads_pp_create(NULL,&ctx)==ADS_PP_OK); ads_pp_destroy(ctx);
    CHECK(ads_pp_write_text("build/bad_stopwords.txt","king and\n")==ADS_PP_OK);
    CHECK(ads_pp_create("build/bad_stopwords.txt",&ctx)==ADS_PP_FORMAT_ERROR&&ctx==NULL);
    CHECK(ads_pp_write_text("build/collision_stopwords.txt","have\nit\n")==ADS_PP_OK);
    CHECK(ads_pp_create("build/collision_stopwords.txt",&ctx)==ADS_PP_OK);
    CHECK(ads_pp_process(ctx,"have having its it",0,&list)==ADS_PP_OK&&list.count==2);
    CHECK(list.tokens[0].position==1&&!strcmp(list.tokens[0].surface,"having")&&!strcmp(list.tokens[0].stem,"have"));
    CHECK(list.tokens[1].position==2&&!strcmp(list.tokens[1].surface,"its")&&!strcmp(list.tokens[1].stem,"it"));
    ads_pp_tokens_free(&list); ads_pp_destroy(ctx);
    return 0;
}
static int test_sample_files(void)
{
    FILE *expected=ads_pp_fopen("data/test/expected_tokens.tsv","rb");
    AdsPreprocessor *ctx=NULL; char *line=NULL; unsigned doc;
    CHECK(expected!=NULL);
    CHECK(ads_pp_read_line(expected,&line)==ADS_PP_OK&&line&&!strcmp(line,"doc_id\tposition\tstem")); free(line);
    CHECK(ads_pp_create("data/test/demo_stopwords.txt",&ctx)==ADS_PP_OK);
    for(doc=1;doc<=4;++doc) {
        char path[64],row[128],*text=NULL; AdsTokenList tokens={0}; size_t i;
        snprintf(path,sizeof(path),"data/sample/D%u.txt",doc);
        CHECK(ads_pp_read_text(path,&text)==ADS_PP_OK);
        CHECK(ads_pp_process(ctx,text,0,&tokens)==ADS_PP_OK); free(text);
        for(i=0;i<tokens.count;++i) {
            snprintf(row,sizeof(row),"%u\t%" PRIu64 "\t%s",doc,tokens.tokens[i].position,tokens.tokens[i].stem);
            CHECK(ads_pp_read_line(expected,&line)==ADS_PP_OK&&line&&!strcmp(line,row)); free(line); line=NULL;
        }
        ads_pp_tokens_free(&tokens);
    }
    CHECK(ads_pp_read_line(expected,&line)==ADS_PP_OK&&line==NULL);
    fclose(expected); ads_pp_destroy(ctx); return 0;
}
static int test_official_vocabulary(void)
{
    FILE *voc=ads_pp_fopen("third_party/porter/voc.txt","rb"),*out=ads_pp_fopen("third_party/porter/output.txt","rb");
    AdsPreprocessor *ctx=NULL; char *word=NULL,*expected=NULL; size_t pairs=0;
    CHECK(voc&&out); CHECK(ads_pp_create(NULL,&ctx)==ADS_PP_OK);
    for(;;) {
        char *actual=NULL;
        CHECK(ads_pp_read_line(voc,&word)==ADS_PP_OK);
        CHECK(ads_pp_read_line(out,&expected)==ADS_PP_OK);
        if(!word) { CHECK(expected==NULL); break; }
        CHECK(expected!=NULL);
        CHECK(ads_pp_stem(ctx,word,&actual)==ADS_PP_OK);
        if(strcmp(actual,expected)) fprintf(stderr,"Mismatch: %s -> %s, expected %s\n",word,actual,expected);
        CHECK(!strcmp(actual,expected));
        free(word); free(expected); free(actual); word=expected=NULL; ++pairs;
    }
    CHECK(pairs>23000);
    printf("Official Porter vocabulary pairs: %zu\n",pairs);
    fclose(voc); fclose(out); ads_pp_destroy(ctx); return 0;
}
int main(void)
{
    if(test_html()||test_tokens()||test_sample_files()||test_official_vocabulary()) return 1;
    printf("Preprocessing checks passed: %u\n",checks); return 0;
}
