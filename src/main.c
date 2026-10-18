#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "search/dict.h"
#include "search/text.h"
#include "search/query.h"

#ifdef _WIN32
#include <windows.h>
#endif

#define DEF_INDEX "data/index"
#define DEF_STOPS "config/stopwords.txt"

static double now_seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER f;
    LARGE_INTEGER c;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);

    return (double)c.QuadPart /
           (double)f.QuadPart;
#else
    return (double)clock() / CLOCKS_PER_SEC;
#endif
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
    char mode[16];
    char query_text[512];

    const char *index_dir = DEF_INDEX;
    const char *stops = DEF_STOPS;

    int t = 1;
    double tau = 1.0;
    int csv = 0;
    int i;
    int rc;

    double begin;
    double finish;

    if (argc < 2)
    {
        usage();
        return 2;
    }

    strcpy(mode, argv[1]);

    if (strcmp(mode, "word") != 0 &&
        strcmp(mode, "multi") != 0 &&
        strcmp(mode, "phrase") != 0 &&
        strcmp(mode, "info") != 0)
    {
        fprintf(
            stderr,
            "错误：未知模式 %s。可用模式为 word、multi、phrase、info。\n",
            mode);
        return 2;
    }

    query_text[0] = '\0';

    for (i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--csv") == 0)
        {
            csv = 1;
            continue;
        }

        if (strcmp(argv[i], "--t") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "错误：--t 缺少参数。\n");
                return 2;
            }

            i++;
            t = atoi(argv[i]);

            if (t < 1)
            {
                fprintf(stderr, "错误：--t 应不小于 1。\n");
                return 2;
            }

            continue;
        }

        if (strcmp(argv[i], "--tau") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "错误：--tau 缺少参数。\n");
                return 2;
            }

            i++;
            tau = atof(argv[i]);

            if (tau < 0.0 || tau > 1.0)
            {
                fprintf(
                    stderr,
                    "错误：--tau 应在 0 到 1 之间。\n");
                return 2;
            }

            continue;
        }

        if (strcmp(argv[i], "--index") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "错误：--index 缺少参数。\n");
                return 2;
            }

            index_dir = argv[++i];
            continue;
        }

        if (strcmp(argv[i], "--stops") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "错误：--stops 缺少参数。\n");
                return 2;
            }

            stops = argv[++i];
            continue;
        }

        if (argv[i][0] == '-' &&
            argv[i][1] == '-')
        {
            fprintf(
                stderr,
                "错误：未知选项 %s\n",
                argv[i]);
            return 2;
        }

        if (strlen(query_text) +
                strlen(argv[i]) + 2 >=
            sizeof(query_text))
        {
            fprintf(stderr, "错误：查询内容过长。\n");
            return 2;
        }

        if (query_text[0] != '\0')
            strcat(query_text, " ");

        strcat(query_text, argv[i]);
    }

    if (!text_open(stops))
    {
        fprintf(
            stderr,
            "错误：无法打开停用词文件 %s\n",
            stops);
        return 3;
    }

    begin = now_seconds();

    if (!dict_open(index_dir))
    {
        fprintf(
            stderr,
            "错误：无法打开索引目录 %s\n",
            index_dir);

        text_close();
        return 3;
    }

    finish = now_seconds();

    fprintf(
        stderr,
        "[已加载索引] %s：%d 篇文档，%d 个词干，%d 个位置，耗时 %.4f 秒\n",
        index_dir,
        dict_doc_count(),
        dict_term_count(),
        dict_position_count(),
        finish - begin);

    if (strcmp(mode, "info") == 0)
    {
        printf("文档数 %d\n", dict_doc_count());
        printf("词干数 %d\n", dict_term_count());
        printf("索引位置数 %d\n", dict_position_count());

        dict_close();
        text_close();

        return 0;
    }

    begin = now_seconds();

    rc = query(
        query_text,
        mode,
        (strcmp(mode, "word") == 0)
            ? (double)t
            : tau);

    finish = now_seconds();

    if (rc != 0)
    {
        dict_close();
        text_close();
        return rc;
    }

    if (csv)
    {
        printf("%s,%s,", mode, query_text);

        if (strcmp(mode, "word") == 0)
            printf("%d", t);
        else if (strcmp(mode, "multi") == 0)
            printf("%.2f", tau);

        printf(",%d,", query_count);

        for (i = 0; i < query_count; i++)
        {
            if (i > 0)
                printf(";");

            printf("%d", query_doc[i]);
        }

        printf(",%.6f\n", finish - begin);
    }
    else
    {
        for (i = 0; i < query_count; i++)
        {
            const char *title =
                dict_title((unsigned int)query_doc[i]);

            if (title == NULL)
                title = "(无标题)";

            if (strcmp(mode, "word") == 0)
            {
                printf(
                    "D%04d %s 出现 %d 次\n",
                    query_doc[i],
                    title,
                    query_num[i]);
            }
            else if (strcmp(mode, "multi") == 0)
            {
                printf(
                    "D%04d %s 命中 %d/%d 个词\n",
                    query_doc[i],
                    title,
                    query_num[i],
                    query_den);
            }
            else
            {
                printf(
                    "D%04d %s 连续匹配，起始词位 %d\n",
                    query_doc[i],
                    title,
                    query_num[i]);
            }
        }

        printf("共 %d 条结果\n", query_count);

        fprintf(
            stderr,
            "[耗时] %.6f 秒\n",
            finish - begin);
    }

    dict_close();
    text_close();

    return 0;
}