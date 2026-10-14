/* Small executable connecting A's real files to B's existing index API. */
#include "index/AdsIndex.h"
#include "preprocess/AdsPreprocess.h"
#include <stdio.h>
#include <time.h>
int main(int argc,char **argv)
{
    AdsIndex *index=NULL,*loaded=NULL; AdsIndexInfo before,after; AdsIndexStatus s;
    clock_t start=clock();
    if(argc!=4) { fprintf(stderr,"Usage: build_index_from_a tokens.tsv documents.tsv output_dir\n"); return 2; }
    /* A has filtered ORIGINAL SURFACES before stemming. A second filter on
     * stems could delete legitimate stem collisions; pass NULL to B. */
    s=ads_index_build_from_tokens(argv[1],NULL,argv[2],&index);
    if(s!=ADS_INDEX_OK) { fprintf(stderr,"Index build status=%d\n",s); return 1; }
    s=ads_index_get_info(index,&before);
    if(s!=ADS_INDEX_OK||!ads_pp_mkdirs(argv[3])) goto fail;
    s=ads_index_save(index,argv[3]); if(s!=ADS_INDEX_OK) goto fail;
    ads_index_close(index); index=NULL;
    s=ads_index_open(argv[3],&loaded); if(s!=ADS_INDEX_OK) goto fail;
    s=ads_index_get_info(loaded,&after);
    if(s!=ADS_INDEX_OK||before.document_count!=after.document_count||before.term_count!=after.term_count||
       before.indexed_position_count!=after.indexed_position_count) goto fail;
    printf("documents\t%zu\nterms\t%zu\nindexed_positions\t%zu\nbuild_clock_seconds\t%.6f\nroundtrip_clock_seconds\t%.6f\n",
           before.document_count,before.term_count,before.indexed_position_count,before.build_seconds,
           (double)(clock()-start)/CLOCKS_PER_SEC);
    ads_index_close(loaded); return 0;
fail:
    fprintf(stderr,"Index save/load validation failed, status=%d\n",s);
    ads_index_close(index); ads_index_close(loaded); return 1;
}
