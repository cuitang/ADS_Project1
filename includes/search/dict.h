#ifndef DICT_H
#define DICT_H

#include <stddef.h>
#include <stdint.h>

/*
 * dict.h 词典模块
 *
 * 词典即倒排索引，记录每个词干出现在哪些文档、哪些词位。
 * 索引数据由索引模块提供，作品名和原文路径从索引目录的 documents.tsv 读取。
 */

/* 一条命中记录：某个词干在某篇文档中的情况 */
typedef struct
{
    unsigned int doc;          /* 文档号 */
    const uint64_t *positions; /* 词位列表 */
    int position_count;        /* 词位个数，即该词在这篇文档中的词频 */
} Hit;

/* 一个词干的全部命中记录，按文档号升序 */
typedef struct
{
    const Hit *items;
    size_t n;
} HitList;

/* 打开索引目录。成功返回 1，失败返回 0。 */
int dict_open(const char *index_dir);
void dict_close(void);

/* 按词干查找，查不到返回 NULL。
   返回结果由本模块保存，下一次 dict_find 时被覆盖。 */
const HitList *dict_find(const char *stem);

/* 文档号对应的作品名和原文路径，没有记录时返回 NULL */
const char *dict_title(unsigned int doc);
const char *dict_path(unsigned int doc);

/* 索引规模 */
int dict_doc_count(void);
int dict_term_count(void);
int dict_position_count(void);

#endif
