/*
 * text.c 文字切词模块
 *
 * 调用预处理模块完成分词，并保存最近一次的切词结果供查询使用。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "search/Text.h"
#include "preprocess/AdsPreprocess.h"

static AdsPreprocessor *preprocessor = NULL;
static AdsTokenList     word_list;
static int              words_loaded = 0;

int text_open(const char *stopwords_path)
{
    memset(&word_list, 0, sizeof(word_list));
    words_loaded = 0;

    if (ads_pp_create(stopwords_path, &preprocessor)
            != ADS_PP_OK) {
        preprocessor = NULL;
        return 0;
    }

    return 1;
}

void text_close(void)
{
    if (words_loaded) {
        ads_pp_tokens_free(&word_list);
        memset(&word_list, 0, sizeof(word_list));
        words_loaded = 0;
    }

    if (preprocessor != NULL) {
        ads_pp_destroy(preprocessor);
        preprocessor = NULL;
    }
}

int text_split(const char *s, int keep_stopwords)
{
    if (words_loaded) {
        ads_pp_tokens_free(&word_list);
        memset(&word_list, 0, sizeof(word_list));
        words_loaded = 0;
    }

    if (preprocessor == NULL || s == NULL)
        return -1;

    if (ads_pp_process(
            preprocessor,
            s,
            keep_stopwords,
            &word_list) != ADS_PP_OK) {
        return -1;
    }

    words_loaded = 1;
    return (int)word_list.count;
}

static char *read_file(const char *path)
{
    FILE *fp;
    long size;
    char *buf;
    size_t got;

    fp = fopen(path, "rb");
    if (fp == NULL)
        return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    size = ftell(fp);

    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    rewind(fp);

    buf = (char *)malloc((size_t)size + 1);

    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }

    got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);

    buf[got] = '\0';
    return buf;
}

int text_split_file(const char *path, int keep_stopwords)
{
    char *body;
    int n;

    body = read_file(path);

    if (body == NULL)
        return -1;

    n = text_split(body, keep_stopwords);
    free(body);

    return n;
}

int text_count(void)
{
    return words_loaded ? (int)word_list.count : 0;
}

const char *text_surface(int i)
{
    if (!words_loaded ||
        i < 0 ||
        (size_t)i >= word_list.count) {
        return "";
    }

    return word_list.tokens[i].surface;
}

const char *text_stem(int i)
{
    if (!words_loaded ||
        i < 0 ||
        (size_t)i >= word_list.count) {
        return NULL;
    }

    if (word_list.tokens[i].is_stopword)
        return NULL;

    return word_list.tokens[i].stem;
}

int text_is_stop(int i)
{
    if (!words_loaded ||
        i < 0 ||
        (size_t)i >= word_list.count) {
        return 0;
    }

    return word_list.tokens[i].is_stopword;
}