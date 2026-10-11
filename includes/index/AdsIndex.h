#ifndef ADS_INDEX_H
#define ADS_INDEX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct AdsIndex AdsIndex;

    typedef struct
    {
        uint32_t doc_id;
        const uint64_t *positions;
        size_t position_count;
    } AdsPosting;

    typedef struct
    {
        const AdsPosting *postings;
        size_t posting_count;
    } AdsPostingListView;

    typedef enum
    {
        ADS_INDEX_OK = 0,
        ADS_INDEX_NOT_FOUND,
        ADS_INDEX_INVALID_ARGUMENT,
        ADS_INDEX_IO_ERROR,
        ADS_INDEX_FORMAT_ERROR,
        ADS_INDEX_OUT_OF_MEMORY,
        ADS_INDEX_UNSUPPORTED_VERSION
    } AdsIndexStatus;

    typedef struct
    {
        uint32_t format_version;
        size_t document_count;
        size_t term_count;
        size_t indexed_position_count;
        double build_seconds; // 本次构建 CPU 时间；open 加载的索引为 0。
    } AdsIndexInfo;

    // 读取指定路径的tokens和停用词，建立倒排索引； 记录文档数、词项数、词位总数和构建耗时；遇到空文件或读入失败时给出明确记录。
    // stopwords_path == NULL 表示不过滤停用词。位置不重新编号。
    // document_path 可为 NULL；否则读入按 doc_id 升序的文档清单，含空文档。
    // 输入路径为 UTF-8，TSV 表头和列定义与 data/sample、data/test 中一致。
    // 成功时 *out_index 交给调用者，失败时置 NULL（out_index 必须非 NULL）。
    // 只有表头的 token 文件生成空索引；没有表头的空文件为格式错误。
    AdsIndexStatus ads_index_build_from_tokens(
        const char *tokens_path,
        const char *stopwords_path,
        const char *document_path,
        AdsIndex **out_index);

    // clean 文本的分词和词干化由 A 提供，当前只接受 token TSV。

    // 保存当前的倒排索引
    // 在指定文件夹下生成数个文件
    // 保存 UTF-8 metadata.tsv、postings.tsv、documents.tsv；父目录须存在。
    // document_path 可为 NULL；此时构建后的标题、来源、路径保存为空字段。
    // 现有文件会覆盖；非事务保存，推荐使用独立输出目录。
    AdsIndexStatus ads_index_save(
        const AdsIndex *index,
        const char *index_dir);

    // 打开指定文件夹， 读取对应名字文件
    // 所有数据加载到内存；失败时 *out_index == NULL。
    AdsIndexStatus ads_index_open(
        const char *index_dir,
        AdsIndex **out_index);

    // 在倒排索引中 根据词干返回文档id、词频和位置。不存在的词返回空结果
    // 返回 NOT_FOUND；视图只读、无需释放，直到 ads_index_close() 前有效。
    AdsIndexStatus ads_index_lookup_stem(
        const AdsIndex *index,
        const char *stem,
        AdsPostingListView *out_view);

    AdsIndexStatus ads_index_get_info(
        const AdsIndex *index,
        AdsIndexInfo *out_info);

    // 释放索引资源
    void ads_index_close(AdsIndex *index);

#ifdef __cplusplus
}
#endif

#endif /* ADS_INDEX_H */
