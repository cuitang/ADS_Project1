/*
 * dict.c 词典模块
 *
 * 索引数据由索引模块提供，作品名和原文路径从索引目录的 documents.tsv 读取。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dict.h"
#include "index/AdsIndex.h"

/* 一个词干最多命中的文档数，等于文档总数。正式语料 912 篇，取 2000。 */
#define MAX_HITS 2000

static AdsIndex     *index_handle = NULL;   /* 索引句柄 */
static AdsIndexInfo  index_info;            /* 索引规模信息 */
static int           info_loaded = 0;       /* 规模信息是否已取到 */

static Hit     hit_buffer[MAX_HITS];        /* 转换用缓冲区 */
static HitList result_list;                 /* 当前查找结果 */

static char **doc_titles = NULL;            /* 文档号到作品名 */
static char **doc_paths  = NULL;            /* 文档号到原文路径 */
static int    doc_capacity = 0;             /* 两个数组当前的容量 */

/* 按制表符切分一行，返回切出的段数 */
static int split_tab(char *s, char *field[], int max_field)
{
    int i = 0;
    int n = 0;

    field[n] = s;
    n++;

    while (s[i] != '\0') {
        if (s[i] == '\t') {
            s[i] = '\0';
            if (n < max_field) {
                field[n] = s + i + 1;
                n++;
            }
        }
        i++;
    }
    return n;
}

static char *copy_str(const char *s)
{
    size_t len = strlen(s) + 1;
    char  *p   = (char *)malloc(len);
    if (p != NULL) memcpy(p, s, len);
    return p;
}

/* 保证两个文档数组能容纳 0 到 need-1 的下标 */
static int grow_docs(int need)
{
    int    slots;
    char **new_titles;
    char **new_paths;
    int    i;

    if (need <= doc_capacity) return 1;

    slots = (doc_capacity == 0) ? 64 : doc_capacity;
    while (slots < need) slots *= 2;

    new_titles = (char **)realloc(doc_titles, sizeof(char *) * (size_t)slots);
    if (new_titles == NULL) return 0;
    doc_titles = new_titles;

    new_paths = (char **)realloc(doc_paths, sizeof(char *) * (size_t)slots);
    if (new_paths == NULL) return 0;
    doc_paths = new_paths;

    for (i = doc_capacity; i < slots; i++) {
        doc_titles[i] = NULL;
        doc_paths[i]  = NULL;
    }
    doc_capacity = slots;
    return 1;
}

/*
 * 读取 documents.tsv，列为 doc_id、work_title、source_url、clean_path。
 */
static int read_documents(const char *path)
{
    FILE *fp;
    char  line[1024];
    int   lineno = 0;

    fp = fopen(path, "r");
    if (fp == NULL) return 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *field[4];
        int   n;
        int   doc;

        lineno++;
        if (lineno == 1) continue;          /* 跳过表头 */

        n = (int)strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[n - 1] = '\0';
            n--;
        }
        if (n == 0) continue;

        if (split_tab(line, field, 4) < 4) continue;

        doc = atoi(field[0]);
        if (doc < 0) continue;
        if (!grow_docs(doc + 1)) { fclose(fp); return 0; }

        free(doc_titles[doc]);
        free(doc_paths[doc]);
        doc_titles[doc] = copy_str(field[1]);
        doc_paths[doc]  = copy_str(field[3]);
        if (doc_titles[doc] == NULL || doc_paths[doc] == NULL) { fclose(fp); return 0; }
    }
    fclose(fp);
    return 1;
}

int dict_open(const char *index_dir)
{
    char path[512];
    int  n;

    if (index_dir == NULL) return 0;

    if (ads_index_open(index_dir, &index_handle) != ADS_INDEX_OK) {
        index_handle = NULL;
        return 0;
    }
    info_loaded = (ads_index_get_info(index_handle, &index_info) == ADS_INDEX_OK);

    /* 索引目录中包含 documents.tsv，据此取得作品名和原文路径 */
    n = (int)strlen(index_dir);
    if (n + 20 < (int)sizeof(path)) {
        strcpy(path, index_dir);
        if (n > 0 && path[n - 1] != '/' && path[n - 1] != '\\') strcat(path, "/");
        strcat(path, "documents.tsv");
        read_documents(path);
    }
    return 1;
}

void dict_close(void)
{
    int i;

    if (index_handle != NULL) {
        ads_index_close(index_handle);
        index_handle = NULL;
    }
    for (i = 0; i < doc_capacity; i++) {
        free(doc_titles[i]);
        free(doc_paths[i]);
    }
    free(doc_titles);
    free(doc_paths);
    doc_titles = NULL;
    doc_paths  = NULL;
    doc_capacity = 0;
    result_list.items = NULL;
    result_list.n = 0;
}

/*
 * 按词干查找倒排表。
 * 把索引模块返回的视图转换为 Hit 数组，词位仍指向原数据，不做复制。
 */
const HitList *dict_find(const char *stem)
{
    AdsPostingListView view;
    size_t i;
    size_t n;

    result_list.items = NULL;
    result_list.n = 0;

    if (index_handle == NULL || stem == NULL) return NULL;
    if (ads_index_lookup_stem(index_handle, stem, &view) != ADS_INDEX_OK) return NULL;

    n = view.posting_count;
    if (n > MAX_HITS) n = MAX_HITS;

    for (i = 0; i < n; i++) {
        hit_buffer[i].doc            = view.postings[i].doc_id;
        hit_buffer[i].positions      = view.postings[i].positions;
        hit_buffer[i].position_count = (int)view.postings[i].position_count;
    }

    result_list.items = hit_buffer;
    result_list.n = n;
    return &result_list;
}

const char *dict_title(unsigned int doc)
{
    if ((int)doc >= doc_capacity) return NULL;
    return doc_titles[doc];
}

const char *dict_path(unsigned int doc)
{
    if ((int)doc >= doc_capacity) return NULL;
    return doc_paths[doc];
}

int dict_doc_count(void)
{
    return info_loaded ? (int)index_info.document_count : 0;
}

int dict_term_count(void)
{
    return info_loaded ? (int)index_info.term_count : 0;
}

int dict_position_count(void)
{
    return info_loaded ? (int)index_info.indexed_position_count : 0;
}
