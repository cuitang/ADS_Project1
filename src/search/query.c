/*
 * Query.c 查询逻辑
 *
 * 原 search.c 中除 main、usage、命令行参数解析之外的部分。
 */

#include <stdio.h>
#include <string.h>

#include "search/dict.h"
#include "search/text.h"
#include "search/query.h"

/* 查询结果，供调用方读取 */
int query_doc[MAX_DOC];
int query_num[MAX_DOC];
int query_count = 0;
int query_den = 0;

/* 查询分词结果，停用词的词干记为空串 */
static char query_words[MAX_WORD][64];
static char query_stems[MAX_WORD][64];
static int query_length = 0;

/* 多词查询统计用 */
static int hit[MAX_DOC];

/*
 * 切分查询串，结果存入 query_words 和 query_stems。
 * 保留停用词，短语核验需要按原始词位逐词比对。
 */
static void cut(const char *s)
{
    int i;
    int n;

    query_length = 0;

    n = text_split(s, 1);

    if (n < 0)
        n = 0;

    if (n > MAX_WORD)
        n = MAX_WORD;

    for (i = 0; i < n; i++)
    {
        strncpy(
            query_words[i],
            text_surface(i),
            sizeof(query_words[i]) - 1);

        query_words[i][sizeof(query_words[i]) - 1] = '\0';

        if (text_is_stop(i))
        {
            query_stems[i][0] = '\0';
        }
        else
        {
            strncpy(
                query_stems[i],
                text_stem(i),
                sizeof(query_stems[i]) - 1);

            query_stems[i][sizeof(query_stems[i]) - 1] = '\0';
        }
    }

    query_length = n;
}

/* 按文档号升序排序，采用插入排序 */
static void sort_result(void)
{
    int i;
    int k;

    for (i = 1; i < query_count; i++)
    {
        int doc = query_doc[i];
        int num = query_num[i];

        k = i - 1;

        while (k >= 0 && query_doc[k] > doc)
        {
            query_doc[k + 1] = query_doc[k];
            query_num[k + 1] = query_num[k];
            k--;
        }

        query_doc[k + 1] = doc;
        query_num[k + 1] = num;
    }
}

/* 收集不同的有效词干 */
static int collect_stems(char stem[][64])
{
    int n = 0;
    int i;
    int j;

    for (i = 0; i < query_length; i++)
    {
        if (query_stems[i][0] == '\0')
            continue;

        for (j = 0; j < n; j++)
        {
            if (strcmp(stem[j], query_stems[i]) == 0)
                break;
        }

        if (j == n)
        {
            strcpy(stem[n], query_stems[i]);
            n++;
        }
    }

    return n;
}

/* 单词查询，文档内词干出现次数不少于 t 才命中 */
static int do_word(int t)
{
    const HitList *list;
    size_t i;

    query_count = 0;
    query_den = 0;

    if (query_length == 0)
    {
        fprintf(stderr, "提示：查询内容为空。\n");
        return 0;
    }

    if (query_length > 1)
    {
        fprintf(
            stderr,
            "错误：word 模式只接受一个词，收到 %d 个。\n",
            query_length);
        return 2;
    }

    if (query_stems[0][0] == '\0')
    {
        fprintf(
            stderr,
            "提示：%s 是停用词，没有可检索的词。\n",
            query_words[0]);
        return 0;
    }

    list = dict_find(query_stems[0]);

    if (list != NULL)
    {
        for (i = 0; i < list->n; i++)
        {
            if (list->items[i].position_count >= t)
            {
                query_doc[query_count] =
                    (int)list->items[i].doc;

                query_num[query_count] =
                    list->items[i].position_count;

                query_count++;

                if (query_count >= MAX_DOC)
                    break;
            }
        }
    }

    sort_result();
    return 0;
}

/*
 * 多词查询，命中比例不低于 tau。
 */
static int do_multi(double tau)
{
    char stem[MAX_WORD][64];
    int n;
    int i;
    int j;

    query_count = 0;
    query_den = 0;

    if (query_length == 0)
    {
        fprintf(stderr, "提示：查询内容为空。\n");
        return 0;
    }

    n = collect_stems(stem);

    if (n == 0)
    {
        fprintf(
            stderr,
            "提示：查询中全部是停用词，没有可检索的词。\n");
        return 0;
    }

    query_den = n;

    for (i = 0; i < MAX_DOC; i++)
        hit[i] = 0;

    for (i = 0; i < n; i++)
    {
        const HitList *list = dict_find(stem[i]);

        if (list == NULL)
            continue;

        for (j = 0; j < (int)list->n; j++)
        {
            int doc = (int)list->items[j].doc;

            if (doc >= 0 && doc < MAX_DOC)
                hit[doc]++;
        }
    }

    for (i = 0; i < MAX_DOC; i++)
    {
        if (hit[i] == 0)
            continue;

        if ((double)hit[i] / n + 0.000001 < tau)
            continue;

        query_doc[query_count] = i;
        query_num[query_count] = hit[i];
        query_count++;
    }

    sort_result();
    return 0;
}

/*
 * 判断文档从 start 开始是否与查询短语逐词相同。
 * 实词比较词干，停用词比较词形。
 */
static int phrase_at(int start)
{
    int j;

    for (j = 0; j < query_length; j++)
    {
        if (query_stems[j][0] != '\0')
        {
            const char *doc_stem =
                text_stem(start + j);

            if (doc_stem == NULL ||
                strcmp(doc_stem, query_stems[j]) != 0)
            {
                return 0;
            }
        }
        else
        {
            if (strcmp(
                    text_surface(start + j),
                    query_words[j]) != 0)
            {
                return 0;
            }
        }
    }

    return 1;
}

/*
 * 连续短语查询。
 * 先以实词的倒排表求交集得到候选文档，
 * 再逐篇读取原文核对词位连续性。
 */
static int do_phrase(void)
{
    char stem[MAX_WORD][64];
    int candidates[MAX_DOC];
    int n;
    int candidate_count = 0;
    int i;
    int j;

    query_count = 0;
    query_den = 0;

    if (query_length == 0)
    {
        fprintf(stderr, "提示：查询内容为空。\n");
        return 0;
    }

    n = collect_stems(stem);

    if (n == 0)
    {
        fprintf(
            stderr,
            "提示：短语中全部是停用词，无法定位候选文档。\n");
        return 0;
    }

    for (i = 0; i < n; i++)
    {
        const HitList *list = dict_find(stem[i]);

        if (list == NULL)
        {
            candidate_count = 0;
            break;
        }

        if (i == 0)
        {
            for (j = 0; j < (int)list->n; j++)
            {
                candidates[candidate_count] =
                    (int)list->items[j].doc;

                candidate_count++;
            }
        }
        else
        {
            int kept = 0;
            int k = 0;

            for (j = 0; j < candidate_count; j++)
            {
                while (k < (int)list->n &&
                       (int)list->items[k].doc <
                           candidates[j])
                {
                    k++;
                }

                if (k < (int)list->n &&
                    (int)list->items[k].doc ==
                        candidates[j])
                {
                    candidates[kept] = candidates[j];
                    kept++;
                }
            }

            candidate_count = kept;
        }

        if (candidate_count == 0)
            break;
    }

    for (i = 0; i < candidate_count; i++)
    {
        int doc = candidates[i];
        const char *path =
            dict_path((unsigned int)doc);
        int word_count;
        int start;
        int found = -1;

        if (path == NULL)
        {
            fprintf(
                stderr,
                "错误：文档 %d 缺少原文路径，无法核验短语。\n",
                doc);
            return 3;
        }

        if (text_split_file(path, 1) < 0)
        {
            fprintf(
                stderr,
                "错误：无法打开原文 %s\n",
                path);
            return 3;
        }

        word_count = text_count();

        for (start = 0;
             start + query_length <= word_count;
             start++)
        {
            if (phrase_at(start))
            {
                found = start;
                break;
            }
        }

        if (found >= 0)
        {
            query_doc[query_count] = doc;
            query_num[query_count] = found;
            query_count++;
        }
    }

    sort_result();
    return 0;
}

int query(
    const char *text,
    const char *mode,
    double threshold)
{
    cut(text);

    if (strcmp(mode, "word") == 0)
        return do_word((int)threshold);

    if (strcmp(mode, "multi") == 0)
        return do_multi(threshold);

    if (strcmp(mode, "phrase") == 0)
        return do_phrase();

    fprintf(
        stderr,
        "错误：未知模式 %s。可用模式为 word、multi、phrase。\n",
        mode);

    return 2;
}