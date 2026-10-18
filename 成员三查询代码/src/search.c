/*
 * search.c 迷你搜索引擎查询程序
 *
 * ADS Project 1, 8-1 Roll Your Own Mini Search Engine
 *
 * 提供查询函数 query(text, mode, threshold)，以及调用它的命令行程序。
 *
 * 用法
 *   search word <词> [--t N] 单词查询
 *   search multi <词> <词> ... [--tau R] 多词查询
 *   search phrase <词> <词> ... 连续短语查询
 *   search info 打印索引规模
 *
 * 选项
 *   --t N        单词查询的最低词频，默认 1
 *   --tau R      多词查询的匹配比例，默认 1.0
 *   --index 目录 索引目录，默认 data/index
 *   --stops 文件 停用词表，默认 data/stopwords.txt
 *   --csv        结果以逗号分隔的一行输出，用于实验记录
 *
 * 退出码
 *   0 正常结束，包括查不到结果
 *   2 命令行参数错误
 *   3 数据文件无法读取
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dict.h"
#include "text.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define DEF_INDEX "data/index"
#define DEF_STOPS "data/stopwords.txt"

#define MAX_WORD 50         /* 一次查询最多的词数 */
#define MAX_DOC  2000       /* 最多的文档数，正式语料 912 篇 */

/* 查询结果，供调用方读取 */
int query_doc[MAX_DOC];     /* 命中文档号，升序排列 */
int query_num[MAX_DOC];     /* 单词查询为词频，多词查询为命中词数，短语查询为起始词位 */
int query_count = 0;        /* 结果条数 */
int query_den   = 0;        /* 多词查询的分母，即不同有效词干的个数 */

/* 查询分词结果，停用词的词干记为空串 */
static char query_words[MAX_WORD][64];
static char query_stems[MAX_WORD][64];
static int  query_length = 0;

/* 多词查询统计用，hit[3] 表示文档 3 命中的查询词数 */
static int hit[MAX_DOC];

int query(const char *text, const char *mode, double threshold);

/* 返回当前时间，单位秒 */
static double now_seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    return (double)clock() / CLOCKS_PER_SEC;
#endif
}

/*
 * 切分查询串，结果存入 query_words 和 query_stems。
 * 保留停用词，短语核验需要按原始词位逐词比对。
 */
static void cut(const char *s)
{
    int i, n;

    query_length = 0;
    n = text_split(s, 1);
    if (n < 0) n = 0;
    if (n > MAX_WORD) n = MAX_WORD;

    for (i = 0; i < n; i++) {
        strncpy(query_words[i], text_surface(i), sizeof(query_words[i]) - 1);
        query_words[i][sizeof(query_words[i]) - 1] = '\0';

        if (text_is_stop(i)) {
            query_stems[i][0] = '\0';
        } else {
            strncpy(query_stems[i], text_stem(i), sizeof(query_stems[i]) - 1);
            query_stems[i][sizeof(query_stems[i]) - 1] = '\0';
        }
    }
    query_length = n;
}

/* 按文档号升序排序，采用插入排序 */
static void sort_result(void)
{
    int i, k;

    for (i = 1; i < query_count; i++) {
        int doc = query_doc[i];
        int num = query_num[i];

        k = i - 1;
        while (k >= 0 && query_doc[k] > doc) {
            query_doc[k + 1] = query_doc[k];
            query_num[k + 1] = query_num[k];
            k--;
        }
        query_doc[k + 1] = doc;
        query_num[k + 1] = num;
    }
}

/* 收集不同的有效词干，停用词和重复词不计入，返回个数 */
static int collect_stems(char stem[][64])
{
    int n = 0;
    int i, j;

    for (i = 0; i < query_length; i++) {
        if (query_stems[i][0] == '\0') continue;

        for (j = 0; j < n; j++) {
            if (strcmp(stem[j], query_stems[i]) == 0) break;
        }
        if (j == n) {
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
    query_den   = 0;

    if (query_length == 0) {
        fprintf(stderr, "提示：查询内容为空。\n");
        return 0;
    }
    if (query_length > 1) {
        fprintf(stderr, "错误：word 模式只接受一个词，收到 %d 个。\n", query_length);
        return 2;
    }
    if (query_stems[0][0] == '\0') {
        fprintf(stderr, "提示：%s 是停用词，没有可检索的词。\n", query_words[0]);
        return 0;
    }

    list = dict_find(query_stems[0]);
    if (list != NULL) {
        for (i = 0; i < list->n; i++) {
            if (list->items[i].position_count >= t) {
                query_doc[query_count] = (int)list->items[i].doc;
                query_num[query_count] = list->items[i].position_count;
                query_count++;
                if (query_count >= MAX_DOC) break;
            }
        }
    }
    sort_result();
    return 0;
}

/*
 * 多词查询，命中比例不低于 tau 才命中。
 * 命中比例为命中的不同词干数除以查询中的不同有效词干数。
 */
static int do_multi(double tau)
{
    char stem[MAX_WORD][64];
    int  n;
    int  i, j;

    query_count = 0;
    query_den   = 0;

    if (query_length == 0) {
        fprintf(stderr, "提示：查询内容为空。\n");
        return 0;
    }

    n = collect_stems(stem);
    if (n == 0) {
        fprintf(stderr, "提示：查询中全部是停用词，没有可检索的词。\n");
        return 0;
    }
    query_den = n;

    /* 统计每篇文档命中的查询词数 */
    for (i = 0; i < MAX_DOC; i++) hit[i] = 0;
    for (i = 0; i < n; i++) {
        const HitList *list = dict_find(stem[i]);
        if (list == NULL) continue;
        for (j = 0; j < (int)list->n; j++) {
            int doc = (int)list->items[j].doc;
            if (doc >= 0 && doc < MAX_DOC) hit[doc]++;
        }
    }

    /* 按比例筛选 */
    for (i = 0; i < MAX_DOC; i++) {
        if (hit[i] == 0) continue;

        /* 加一个极小值，避免浮点误差 */
        if ((double)hit[i] / n + 0.000001 < tau) continue;

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

    for (j = 0; j < query_length; j++) {
        if (query_stems[j][0] != '\0') {
            const char *doc_stem = text_stem(start + j);
            if (doc_stem == NULL || strcmp(doc_stem, query_stems[j]) != 0) return 0;
        } else {
            if (strcmp(text_surface(start + j), query_words[j]) != 0) return 0;
        }
    }
    return 1;
}

/*
 * 连续短语查询。
 * 先以实词的倒排表求交集得到候选文档，再逐篇读取原文核对词位。
 */
static int do_phrase(void)
{
    char stem[MAX_WORD][64];
    int  candidates[MAX_DOC];
    int  n, candidate_count = 0;
    int  i, j;

    query_count = 0;
    query_den   = 0;

    if (query_length == 0) {
        fprintf(stderr, "提示：查询内容为空。\n");
        return 0;
    }

    n = collect_stems(stem);
    if (n == 0) {
        fprintf(stderr, "提示：短语中全部是停用词，无法定位候选文档。\n");
        return 0;
    }

    /* 求各实词倒排表的交集 */
    for (i = 0; i < n; i++) {
        const HitList *list = dict_find(stem[i]);

        if (list == NULL) { candidate_count = 0; break; }

        if (i == 0) {
            for (j = 0; j < (int)list->n; j++) {
                candidates[candidate_count] = (int)list->items[j].doc;
                candidate_count++;
            }
        } else {
            /* 两组文档号均为升序，用双指针求交集 */
            int kept = 0;
            int k = 0;

            for (j = 0; j < candidate_count; j++) {
                while (k < (int)list->n && (int)list->items[k].doc < candidates[j]) k++;
                if (k < (int)list->n && (int)list->items[k].doc == candidates[j]) {
                    candidates[kept] = candidates[j];
                    kept++;
                }
            }
            candidate_count = kept;
        }
        if (candidate_count == 0) break;
    }

    /* 逐篇核对词位连续性 */
    for (i = 0; i < candidate_count; i++) {
        int         doc  = candidates[i];
        const char *path = dict_path((unsigned int)doc);
        int         word_count, start, found = -1;

        if (path == NULL) {
            fprintf(stderr, "错误：文档 %d 缺少原文路径，无法核验短语。\n", doc);
            return 3;
        }
        if (text_split_file(path, 1) < 0) {
            fprintf(stderr, "错误：无法打开原文 %s\n", path);
            return 3;
        }

        word_count = text_count();
        for (start = 0; start + query_length <= word_count; start++) {
            if (phrase_at(start)) {
                found = start;
                break;
            }
        }

        if (found >= 0) {
            query_doc[query_count] = doc;
            query_num[query_count] = found;
            query_count++;
        }
    }
    sort_result();
    return 0;
}

/*
 * 执行查询。threshold 在 word 模式为最低词频，在 multi 模式为匹配比例，
 * phrase 模式不使用。调用前需先完成 text_open 和 dict_open。
 */
int query(const char *text, const char *mode, double threshold)
{
    cut(text);

    if (strcmp(mode, "word") == 0)   return do_word((int)threshold);
    if (strcmp(mode, "multi") == 0)  return do_multi(threshold);
    if (strcmp(mode, "phrase") == 0) return do_phrase();

    fprintf(stderr, "错误：未知模式 %s。可用模式为 word、multi、phrase。\n", mode);
    return 2;
}

static void usage(void)
{
    printf("用法：\n");
    printf("  search word <词> [--t N] 单词查询\n");
    printf("  search multi <词> <词> ... [--tau R] 多词查询\n");
    printf("  search phrase <词> <词> ... 连续短语查询\n");
    printf("  search info 打印索引规模\n");
    printf("\n选项：--t N  --tau R  --index 目录  --stops 文件  --csv\n");
}

int main(int argc, char **argv)
{
    char   mode[16];
    char   query_text[512];
    const char *index_dir = DEF_INDEX;
    const char *stops     = DEF_STOPS;
    int    t   = 1;
    double tau = 1.0;
    int    csv = 0;
    int    i, rc;
    double begin, finish;

    if (argc < 2) {
        usage();
        return 2;
    }

    strcpy(mode, argv[1]);
    if (strcmp(mode, "word") != 0 && strcmp(mode, "multi") != 0 &&
        strcmp(mode, "phrase") != 0 && strcmp(mode, "info") != 0) {
        fprintf(stderr, "错误：未知模式 %s。可用模式为 word、multi、phrase、info。\n", mode);
        return 2;
    }

    query_text[0] = '\0';

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0) { csv = 1; continue; }

        if (strcmp(argv[i], "--t") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "错误：--t 缺少参数。\n"); return 2; }
            i++;
            t = atoi(argv[i]);
            if (t < 1) { fprintf(stderr, "错误：--t 应不小于 1。\n"); return 2; }
            continue;
        }

        if (strcmp(argv[i], "--tau") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "错误：--tau 缺少参数。\n"); return 2; }
            i++;
            tau = atof(argv[i]);
            if (tau < 0.0 || tau > 1.0) {
                fprintf(stderr, "错误：--tau 应在 0 到 1 之间。\n");
                return 2;
            }
            continue;
        }

        if (strcmp(argv[i], "--index") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "错误：--index 缺少参数。\n"); return 2; }
            index_dir = argv[++i];
            continue;
        }

        if (strcmp(argv[i], "--stops") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "错误：--stops 缺少参数。\n"); return 2; }
            stops = argv[++i];
            continue;
        }

        if (argv[i][0] == '-' && argv[i][1] == '-') {
            fprintf(stderr, "错误：未知选项 %s\n", argv[i]);
            return 2;
        }

        /* 其余参数视为查询词 */
        if (strlen(query_text) + strlen(argv[i]) + 2 >= sizeof(query_text)) {
            fprintf(stderr, "错误：查询内容过长。\n");
            return 2;
        }
        if (query_text[0] != '\0') strcat(query_text, " ");
        strcat(query_text, argv[i]);
    }

    if (!text_open(stops)) {
        fprintf(stderr, "错误：无法打开停用词文件 %s\n", stops);
        return 3;
    }

    begin = now_seconds();
    if (!dict_open(index_dir)) {
        fprintf(stderr, "错误：无法打开索引目录 %s\n", index_dir);
        text_close();
        return 3;
    }
    finish = now_seconds();
    fprintf(stderr, "[已加载索引] %s：%d 篇文档，%d 个词干，%d 个位置，耗时 %.4f 秒\n",
            index_dir, dict_doc_count(), dict_term_count(), dict_position_count(),
            finish - begin);

    if (strcmp(mode, "info") == 0) {
        printf("文档数 %d\n", dict_doc_count());
        printf("词干数 %d\n", dict_term_count());
        printf("索引位置数 %d\n", dict_position_count());
        dict_close();
        text_close();
        return 0;
    }

    begin = now_seconds();
    rc = query(query_text, mode, (strcmp(mode, "word") == 0) ? (double)t : tau);
    finish = now_seconds();

    if (rc != 0) {
        dict_close();
        text_close();
        return rc;
    }

    if (csv) {
        /* 模式，查询，阈值，结果数，命中文档号，查询耗时 */
        printf("%s,%s,", mode, query_text);
        if (strcmp(mode, "word") == 0)       printf("%d", t);
        else if (strcmp(mode, "multi") == 0) printf("%.2f", tau);
        printf(",%d,", query_count);
        for (i = 0; i < query_count; i++) {
            if (i > 0) printf(";");
            printf("%d", query_doc[i]);
        }
        printf(",%.6f\n", finish - begin);
    } else {
        for (i = 0; i < query_count; i++) {
            const char *title = dict_title((unsigned int)query_doc[i]);
            if (title == NULL) title = "(无标题)";

            if (strcmp(mode, "word") == 0) {
                printf("D%04d %s 出现 %d 次\n", query_doc[i], title, query_num[i]);
            } else if (strcmp(mode, "multi") == 0) {
                printf("D%04d %s 命中 %d/%d 个词\n",
                       query_doc[i], title, query_num[i], query_den);
            } else {
                printf("D%04d %s 连续匹配，起始词位 %d\n",
                       query_doc[i], title, query_num[i]);
            }
        }
        printf("共 %d 条结果\n", query_count);
        fprintf(stderr, "[耗时] %.6f 秒\n", finish - begin);
    }

    dict_close();
    text_close();
    return 0;
}
