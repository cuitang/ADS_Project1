#include "preprocess/AdsPreprocess.h"
#include "index/AdsIndex.h"
#include <stdio.h>
int main(int argc,char **argv)
{
    const char *queries[]={"the","and","king","queen","love","runs","walked","not","no","having","does","its","herself","thine"};
    AdsPreprocessor *ctx=NULL; AdsIndex *index=NULL; size_t i;
    if(argc!=4) return 2;
    if(ads_pp_create(argv[1],&ctx)!=ADS_PP_OK||ads_index_open(argv[2],&index)!=ADS_INDEX_OK) {
        ads_pp_destroy(ctx); ads_index_close(index); return 1;
    }
    printf("policy\tquery\tfiltered_as_stopword\tstem\tmatching_documents\n");
    for(i=0;i<sizeof(queries)/sizeof(queries[0]);++i) {
        AdsTokenList tokens={0}; AdsPostingListView view={0}; AdsIndexStatus s=ADS_INDEX_NOT_FOUND;
        if(ads_pp_process(ctx,queries[i],0,&tokens)!=ADS_PP_OK) { ads_pp_destroy(ctx); ads_index_close(index); return 1; }
        if(tokens.count) s=ads_index_lookup_stem(index,tokens.tokens[0].stem,&view);
        if(s!=ADS_INDEX_OK&&s!=ADS_INDEX_NOT_FOUND) { ads_pp_tokens_free(&tokens); ads_pp_destroy(ctx); ads_index_close(index); return 1; }
        printf("%s\t%s\t%d\t%s\t%zu\n",argv[3],queries[i],tokens.count?0:1,tokens.count?tokens.tokens[0].stem:"",view.posting_count);
        ads_pp_tokens_free(&tokens);
    }
    ads_pp_destroy(ctx); ads_index_close(index); return 0;
}
