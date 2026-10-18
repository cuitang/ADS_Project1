#include "index/AdsIndex.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define ADS_FORMAT_VERSION 1u
#define DOCUMENT_HEADER "doc_id\twork_title\tsource_url\tclean_path"

typedef struct
{
    char *stem;
    AdsPosting *postings;
    size_t posting_count;
} AdsTerm;

struct AdsIndex
{
    uint32_t format_version;
    AdsTerm *terms; /* 按 stem 排序 */
    size_t term_count;
    uint32_t *document_ids; /* 按 doc_id 排序 */
    char **document_fields; /* 标题、来源、路径；构建时为 NULL */
    size_t document_count;
    size_t indexed_position_count;
    double build_seconds; /* 当前构建所用 CPU 时间，不持久化 */
};

typedef struct
{
    uint32_t doc_id;
    uint64_t position;
    char *stem;
} Token;

typedef struct
{
    Token *items;
    size_t count, capacity;
} TokenBuffer;

typedef struct
{
    char **items;
    size_t count, capacity;
} Words;

/* Windows 上公开路径按 UTF-8 解释，支持中文目录。 */
#ifdef _WIN32
static wchar_t *wide_path(const char *path)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *result;
    if (n == 0)
        return NULL;
    result = malloc((size_t)n * sizeof(*result));
    if (result && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       path, -1, result, n))
    {
        free(result);
        return NULL;
    }
    return result;
}
#endif

static FILE *open_file(const char *path, int write)
{
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    FILE *file;
    if (!wide)
        return NULL;
    file = _wfopen(wide, write ? L"wb" : L"rb");
    free(wide);
    return file;
#else
    return fopen(path, write ? "wb" : "rb");
#endif
}

static int make_directory(const char *path)
{
#ifdef _WIN32
    wchar_t *wide = wide_path(path);
    int rc;
    if (!wide)
        return -1;
    rc = _wmkdir(wide);
    free(wide);
#else
    int rc = mkdir(path, 0777);
#endif
    return rc == 0 || errno == EEXIST ? 0 : -1;
}

static char *copy_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

static int reserve(void **data, size_t *capacity, size_t need, size_t item_size)
{
    size_t n;
    void *p;
    if (need <= *capacity)
        return 1;
    n = *capacity ? *capacity : 16;
    while (n < need)
    {
        if (n > SIZE_MAX / 2)
        {
            n = need;
            break;
        }
        n *= 2;
    }
    if (n > SIZE_MAX / item_size)
        return 0;
    p = realloc(*data, n * item_size);
    if (!p)
        return 0;
    *data = p;
    *capacity = n;
    return 1;
}

/* EOF 时返回 OK 且 *line == NULL；允许 LF、CRLF 和末行无换行。 */
static AdsIndexStatus read_line(FILE *file, char **line)
{
    char *p = NULL;
    size_t n = 0, cap = 0;
    int c;
    *line = NULL;
    while ((c = fgetc(file)) != EOF && c != '\n')
    {
        if (c == 0)
        {
            free(p);
            return ADS_INDEX_FORMAT_ERROR;
        }
        if (!reserve((void **)&p, &cap, n + 2, 1))
        {
            free(p);
            return ADS_INDEX_OUT_OF_MEMORY;
        }
        p[n++] = (char)c;
    }
    if (ferror(file))
    {
        free(p);
        return ADS_INDEX_IO_ERROR;
    }
    if (c == EOF && n == 0)
    {
        free(p);
        return ADS_INDEX_OK;
    }
    if (!reserve((void **)&p, &cap, n + 1, 1))
    {
        free(p);
        return ADS_INDEX_OUT_OF_MEMORY;
    }
    if (n && p[n - 1] == '\r')
        --n;
    p[n] = 0;
    *line = p;
    return ADS_INDEX_OK;
}

static void strip_bom(char *s)
{
    if (strlen(s) >= 3 && (unsigned char)s[0] == 0xef &&
        (unsigned char)s[1] == 0xbb && (unsigned char)s[2] == 0xbf)
        memmove(s, s + 3, strlen(s + 3) + 1);
}

static AdsIndexStatus read_header(FILE *file, const char *expected)
{
    char *line = NULL;
    AdsIndexStatus status = read_line(file, &line);
    if (status == ADS_INDEX_OK)
    {
        if (!line)
            status = ADS_INDEX_FORMAT_ERROR;
        else
        {
            strip_bom(line);
            if (strcmp(line, expected))
                status = ADS_INDEX_FORMAT_ERROR;
        }
    }
    free(line);
    return status;
}

static int number(const char *s, uint64_t *value)
{
    uint64_t n = 0;
    if (!*s)
        return 0;
    for (; *s; ++s)
    {
        unsigned digit;
        if (*s < '0' || *s > '9')
            return 0;
        digit = (unsigned)(*s - '0');
        if (n > (UINT64_MAX - digit) / 10)
            return 0;
        n = n * 10 + digit;
    }
    *value = n;
    return 1;
}

/* 精确拆分 n 列，允许列为空；具体内容由调用方校验。 */
static int fields(char *line, char **parts, size_t n)
{
    size_t i;
    parts[0] = line;
    for (i = 1; i < n; ++i)
    {
        char *tab = strchr(parts[i - 1], '\t');
        if (!tab)
            return 0;
        *tab = 0;
        parts[i] = tab + 1;
    }
    return strchr(parts[n - 1], '\t') == NULL;
}

static char *index_path(const char *dir, const char *name)
{
    size_t a = strlen(dir), b = strlen(name);
    char *path;
    if (a > SIZE_MAX - b - 2)
        return NULL;
    path = malloc(a + b + 2);
    if (path)
        snprintf(path, a + b + 2, "%s/%s", dir, name);
    return path;
}

static void tokens_free(TokenBuffer *tokens)
{
    size_t i;
    for (i = 0; i < tokens->count; ++i)
        free(tokens->items[i].stem);
    free(tokens->items);
    memset(tokens, 0, sizeof(*tokens));
}

static AdsIndexStatus token_add(TokenBuffer *tokens, uint32_t doc,
                                uint64_t pos, const char *stem)
{
    char *copy;
    if (!*stem || strpbrk(stem, " \t\r\n"))
        return ADS_INDEX_FORMAT_ERROR;
    if (tokens->count == SIZE_MAX ||
        !reserve((void **)&tokens->items, &tokens->capacity,
                 tokens->count + 1, sizeof(*tokens->items)))
        return ADS_INDEX_OUT_OF_MEMORY;
    copy = copy_string(stem);
    if (!copy)
        return ADS_INDEX_OUT_OF_MEMORY;
    tokens->items[tokens->count++] = (Token){doc, pos, copy};
    return ADS_INDEX_OK;
}

static int compare_words(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void words_free(Words *words)
{
    size_t i;
    for (i = 0; i < words->count; ++i)
        free(words->items[i]);
    free(words->items);
}

// 加载停用词
static AdsIndexStatus words_load(const char *path, Words *words)
{
    FILE *file;
    char *line = NULL;
    AdsIndexStatus status = ADS_INDEX_OK;
    int first = 1;
    if (!path)
        return ADS_INDEX_OK;
    file = open_file(path, 0);
    if (!file)
        return ADS_INDEX_IO_ERROR;
    while ((status = read_line(file, &line)) == ADS_INDEX_OK && line)
    {
        if (first)
        {
            strip_bom(line);
            first = 0;
        }
        if (!*line)
        {
            free(line);
            line = NULL;
            continue;
        }
        if (strpbrk(line, " \t\r\n"))
        {
            status = ADS_INDEX_FORMAT_ERROR;
            break;
        }
        if (!reserve((void **)&words->items, &words->capacity,
                     words->count + 1, sizeof(*words->items)))
        {
            status = ADS_INDEX_OUT_OF_MEMORY;
            break;
        }
        words->items[words->count++] = line;
        line = NULL;
    }
    free(line);
    fclose(file);
    if (words->count > 1)
        qsort(words->items, words->count, sizeof(*words->items), compare_words);
    return status;
}

static int is_stopword(const Words *words, const char *stem)
{
    if (!words || !words->count)
        return 0;
    return bsearch(&stem, words->items, words->count,
                   sizeof(*words->items), compare_words) != NULL;
}

static int compare_doc_position(const void *a, const void *b)
{
    const Token *x = a, *y = b;
    if (x->doc_id != y->doc_id)
        return x->doc_id < y->doc_id ? -1 : 1;
    if (x->position != y->position)
        return x->position < y->position ? -1 : 1;
    return 0;
}

static int compare_stem(const void *a, const void *b)
{
    const Token *x = a, *y = b;
    int cmp = strcmp(x->stem, y->stem);
    return cmp ? cmp : compare_doc_position(a, b);
}

void ads_index_close(AdsIndex *index)
{
    size_t i, j;
    if (!index)
        return;
    for (i = 0; i < index->term_count; ++i)
    {
        AdsTerm *term = &index->terms[i];
        for (j = 0; j < term->posting_count; ++j)
            free((void *)term->postings[j].positions);
        free(term->postings);
        free(term->stem);
    }
    for (i = 0; index->document_fields && i < index->document_count; ++i)
        free(index->document_fields[i]);
    free(index->document_fields);
    free(index->document_ids);
    free(index->terms);
    free(index);
}

/* 同一 doc_id 的同一位置只能有一个 token；停用词过滤前也检查。 */
static AdsIndexStatus assemble(TokenBuffer *tokens, const Words *words,
                               AdsIndex **out)
{
    AdsIndex *index = calloc(1, sizeof(*index));
    size_t i, term_capacity = 0;
    AdsIndexStatus status = ADS_INDEX_OUT_OF_MEMORY;
    if (!index)
        return status;
    index->format_version = ADS_FORMAT_VERSION;
    // 第一次遍历tokens， 按照文档id顺序， 统计一共出现了多少篇文档， 并检查可能的重复位置
    if (tokens->count > 1)
        qsort(tokens->items, tokens->count, sizeof(Token), compare_doc_position);
    for (i = 0; i < tokens->count; ++i)
    {
        // 检查位置是否重复
        if (i && tokens->items[i].doc_id == tokens->items[i - 1].doc_id &&
            tokens->items[i].position == tokens->items[i - 1].position)
        {
            status = ADS_INDEX_FORMAT_ERROR;
            goto fail;
        }
        if (!i || tokens->items[i].doc_id != tokens->items[i - 1].doc_id)
            ++index->document_count;
    }
    if (index->document_count)
    {
        size_t d = 0;
        index->document_ids = calloc(index->document_count, sizeof(uint32_t));
        if (!index->document_ids)
            goto fail;
        for (i = 0; i < tokens->count; ++i)
            if (!i || tokens->items[i].doc_id != tokens->items[i - 1].doc_id)
                index->document_ids[d++] = tokens->items[i].doc_id;
    }
    // 第二次遍历tokens， 按照 tokens 的字母顺序， 建立倒排索引
    if (tokens->count > 1)
        qsort(tokens->items, tokens->count, sizeof(Token), compare_stem);
    for (i = 0; i < tokens->count;)
    {
        size_t end = i + 1, j, posting_count = 0, p = 0;
        AdsTerm *term;
        while (end < tokens->count && !strcmp(tokens->items[i].stem, tokens->items[end].stem))
            ++end;
        if (is_stopword(words, tokens->items[i].stem))
        {
            i = end;
            continue;
        }
        if (!reserve((void **)&index->terms, &term_capacity,
                     index->term_count + 1, sizeof(AdsTerm)))
            goto fail;
        term = &index->terms[index->term_count++];
        memset(term, 0, sizeof(*term));
        term->stem = copy_string(tokens->items[i].stem);
        if (!term->stem)
            goto fail;
        // 检查当前单词在多少文档中出现
        for (j = i; j < end; ++j)
            if (j == i || tokens->items[j].doc_id != tokens->items[j - 1].doc_id)
                ++posting_count;
        term->postings = calloc(posting_count, sizeof(AdsPosting));
        if (!term->postings)
            goto fail;
        term->posting_count = posting_count;
        for (j = i; j < end;)
        {
            size_t next = j + 1, k;
            uint64_t *positions;
            while (next < end && tokens->items[next].doc_id == tokens->items[j].doc_id)
                ++next;
            positions = calloc(next - j, sizeof(uint64_t));
            if (!positions)
                goto fail;
            term->postings[p] = (AdsPosting){tokens->items[j].doc_id,
                                             positions, next - j};
            for (k = j; k < next; ++k)
                positions[k - j] = tokens->items[k].position;
            index->indexed_position_count += next - j;
            ++p;
            j = next;
        }
        i = end;
    }
    *out = index;
    return ADS_INDEX_OK;
fail:
    ads_index_close(index);
    return status;
}

AdsIndexStatus ads_index_lookup_stem(const AdsIndex *index, const char *stem,
                                     AdsPostingListView *out_view)
{
    size_t left = 0, right;
    if (!out_view)
        return ADS_INDEX_INVALID_ARGUMENT;
    *out_view = (AdsPostingListView){NULL, 0};
    if (!index || !stem || !*stem)
        return ADS_INDEX_INVALID_ARGUMENT;
    right = index->term_count;
    while (left < right)
    {
        size_t mid = left + (right - left) / 2;
        int cmp = strcmp(index->terms[mid].stem, stem);
        if (!cmp)
        {
            *out_view = (AdsPostingListView){index->terms[mid].postings,
                                             index->terms[mid].posting_count};
            return ADS_INDEX_OK;
        }
        if (cmp < 0)
            left = mid + 1;
        else
            right = mid;
    }
    return ADS_INDEX_NOT_FOUND;
}

AdsIndexStatus ads_index_get_info(const AdsIndex *index, AdsIndexInfo *out_info)
{
    if (!index || !out_info)
        return ADS_INDEX_INVALID_ARGUMENT;
    *out_info = (AdsIndexInfo){index->format_version, index->document_count,
                               index->term_count, index->indexed_position_count,
                               index->build_seconds};
    return ADS_INDEX_OK;
}

static AdsIndexStatus load_documents(FILE *file, AdsIndex *index, size_t expected);

AdsIndexStatus ads_index_build_from_tokens(const char *tokens_path,
                                           const char *stopwords_path,
                                           const char *document_path,
                                           AdsIndex **out_index)
{
    TokenBuffer tokens = {0};
    Words words = {0};
    FILE *file = NULL;
    char *line = NULL;
    AdsIndexStatus status;
    clock_t start = clock();
    if (!out_index)
        return ADS_INDEX_INVALID_ARGUMENT;
    *out_index = NULL;

    if (!tokens_path || !*tokens_path)
        return ADS_INDEX_INVALID_ARGUMENT;
    status = words_load(stopwords_path, &words);

    if (status != ADS_INDEX_OK)
        goto done;
    file = open_file(tokens_path, 0);
    if (!file)
    {
        status = ADS_INDEX_IO_ERROR;
        goto done;
    }
    status = read_header(file, "doc_id\tposition\tstem");
    if (status != ADS_INDEX_OK)
        goto done;

    // 读入 tokens
    while ((status = read_line(file, &line)) == ADS_INDEX_OK && line)
    {
        char *parts[3];
        uint64_t doc, pos;
        if (!*line)
        {
            free(line);
            line = NULL;
            continue;
        }
        if (!fields(line, parts, 3) || !number(parts[0], &doc) ||
            doc > UINT32_MAX || !number(parts[1], &pos))
        {
            status = ADS_INDEX_FORMAT_ERROR;
            break;
        }
        status = token_add(&tokens, (uint32_t)doc, pos, parts[2]);
        free(line);
        line = NULL;
        if (status != ADS_INDEX_OK)
            break;
    }
    if (status == ADS_INDEX_OK)
    {
        status = assemble(&tokens, &words, out_index);
        if (status == ADS_INDEX_OK && document_path)
        {
            FILE *documents = open_file(document_path, 0);
            if (!documents)
                status = ADS_INDEX_IO_ERROR;
            else
            {
                status = load_documents(documents, *out_index, SIZE_MAX);
                fclose(documents);
            }
            if (status != ADS_INDEX_OK)
            {
                ads_index_close(*out_index);
                *out_index = NULL;
            }
        }
        if (status == ADS_INDEX_OK && start != (clock_t)-1)
        {
            clock_t end = clock();
            if (end != (clock_t)-1 && end >= start)
                (*out_index)->build_seconds = (double)(end - start) / CLOCKS_PER_SEC;
        }
    }
done:
    free(line);
    if (file)
        fclose(file);
    tokens_free(&tokens);
    words_free(&words);
    return status;
}

static AdsIndexStatus load_metadata(FILE *file, AdsIndexInfo *info)
{
    const char *keys[] = {"format_version", "document_count", "term_count",
                          "indexed_position_count", "position_base"};
    uint64_t values[5] = {0};
    unsigned seen = 0;
    char *line = NULL;
    AdsIndexStatus status = read_header(file, "key\tvalue");
    size_t i;
    if (status != ADS_INDEX_OK)
        return status;
    while ((status = read_line(file, &line)) == ADS_INDEX_OK && line)
    {
        char *parts[2];
        if (!*line)
        {
            free(line);
            line = NULL;
            continue;
        }
        if (!fields(line, parts, 2) || !*parts[0] || !*parts[1])
        {
            status = ADS_INDEX_FORMAT_ERROR;
            break;
        }
        for (i = 0; i < 5; ++i)
        {
            if (strcmp(parts[0], keys[i]))
                continue;
            if ((seen & (1u << i)) || !number(parts[1], &values[i]))
            {
                status = ADS_INDEX_FORMAT_ERROR;
                break;
            }
            seen |= 1u << i;
        }
        free(line);
        line = NULL;
        if (status != ADS_INDEX_OK)
            break;
    }
    free(line);
    if (status != ADS_INDEX_OK)
        return status;
    if (seen != 31u)
        return ADS_INDEX_FORMAT_ERROR;
    if (values[0] != ADS_FORMAT_VERSION)
        return ADS_INDEX_UNSUPPORTED_VERSION;
    if (values[4] != 0 || values[1] > SIZE_MAX ||
        values[2] > SIZE_MAX || values[3] > SIZE_MAX)
        return ADS_INDEX_FORMAT_ERROR;
    *info = (AdsIndexInfo){ADS_FORMAT_VERSION, (size_t)values[1],
                           (size_t)values[2], (size_t)values[3], 0.0};
    return ADS_INDEX_OK;
}

static AdsIndexStatus load_postings(FILE *file, TokenBuffer *tokens)
{
    char *line = NULL, *previous_stem = NULL;
    uint32_t previous_doc = 0;
    AdsIndexStatus status = read_header(file, "stem\tdoc_id\ttf\tpositions");
    if (status != ADS_INDEX_OK)
        return status;
    while ((status = read_line(file, &line)) == ADS_INDEX_OK && line)
    {
        char *parts[4], *cursor;
        uint64_t doc, tf, previous_pos = 0;
        size_t count = 0;
        int cmp;
        if (!*line)
        {
            free(line);
            line = NULL;
            continue;
        }
        if (!fields(line, parts, 4) || !number(parts[1], &doc) ||
            doc > UINT32_MAX || !number(parts[2], &tf) || tf == 0 ||
            tf > SIZE_MAX || !*parts[3])
        {
            status = ADS_INDEX_FORMAT_ERROR;
            break;
        }
        cmp = previous_stem ? strcmp(previous_stem, parts[0]) : -1;
        if (cmp > 0 || (cmp == 0 && doc <= previous_doc))
        {
            status = ADS_INDEX_FORMAT_ERROR;
            break;
        }
        cursor = parts[3];
        for (;;)
        {
            char *comma = strchr(cursor, ',');
            uint64_t pos;
            if (comma)
                *comma = 0;
            if (!number(cursor, &pos) || (count && pos <= previous_pos))
            {
                status = ADS_INDEX_FORMAT_ERROR;
                break;
            }
            status = token_add(tokens, (uint32_t)doc, pos, parts[0]);
            if (status != ADS_INDEX_OK)
                break;
            previous_pos = pos;
            ++count;
            if (!comma)
                break;
            cursor = comma + 1;
        }
        if (status == ADS_INDEX_OK && count != tf)
            status = ADS_INDEX_FORMAT_ERROR;
        if (status != ADS_INDEX_OK)
            break;
        free(previous_stem);
        previous_stem = copy_string(parts[0]);
        if (!previous_stem)
        {
            status = ADS_INDEX_OUT_OF_MEMORY;
            break;
        }
        previous_doc = (uint32_t)doc;
        free(line);
        line = NULL;
    }
    free(previous_stem);
    free(line);
    return status;
}

static AdsIndexStatus load_documents(FILE *file, AdsIndex *index, size_t expected)
{
    uint32_t *ids = NULL;
    char **details = NULL;
    size_t count = 0, used = 0, i, id_capacity = 0, detail_capacity = 0;
    char *line = NULL;
    AdsIndexStatus status = read_header(file, DOCUMENT_HEADER);
    if (status != ADS_INDEX_OK)
        return status;
    if (expected && expected != SIZE_MAX)
    {
        ids = calloc(expected, sizeof(*ids));
        details = calloc(expected, sizeof(*details));
        if (!ids || !details)
        {
            status = ADS_INDEX_OUT_OF_MEMORY;
            goto done;
        }
        id_capacity = detail_capacity = expected;
    }
    while ((status = read_line(file, &line)) == ADS_INDEX_OK && line)
    {
        char *tab, *rest, *parts[3];
        uint64_t id;
        if (!*line)
        {
            free(line);
            line = NULL;
            continue;
        }
        tab = strchr(line, '\t');
        if (!tab || count >= expected)
        {
            status = ADS_INDEX_FORMAT_ERROR;
            break;
        }
        *tab = 0;
        rest = tab + 1;
        if (!number(line, &id) || id > UINT32_MAX ||
            (count && id <= ids[count - 1]))
        {
            status = ADS_INDEX_FORMAT_ERROR;
            break;
        }
        if (!reserve((void **)&ids, &id_capacity, count + 1, sizeof(*ids)) ||
            !reserve((void **)&details, &detail_capacity, count + 1, sizeof(*details)))
        {
            status = ADS_INDEX_OUT_OF_MEMORY;
            break;
        }
        /* 保存分列前的元数据，随后验证恰好有三列。 */
        details[count] = copy_string(rest);
        if (!details[count])
        {
            status = ADS_INDEX_OUT_OF_MEMORY;
            break;
        }
        ++used;
        if (!fields(rest, parts, 3))
        {
            status = ADS_INDEX_FORMAT_ERROR;
            break;
        }
        ids[count++] = (uint32_t)id;
        free(line);
        line = NULL;
    }
    if (status == ADS_INDEX_OK && expected != SIZE_MAX && count != expected)
        status = ADS_INDEX_FORMAT_ERROR;
    /* 所有 posting 的 doc_id 都必须在清单中；额外的空文档允许存在。 */
    for (i = 0; status == ADS_INDEX_OK && i < index->document_count; ++i)
    {
        size_t left = 0, right = count;
        while (left < right)
        {
            size_t mid = left + (right - left) / 2;
            if (ids[mid] < index->document_ids[i])
                left = mid + 1;
            else
                right = mid;
        }
        if (left == count || ids[left] != index->document_ids[i])
            status = ADS_INDEX_FORMAT_ERROR;
    }
    if (status == ADS_INDEX_OK)
    {
        free(index->document_ids);
        index->document_ids = ids;
        index->document_fields = details;
        index->document_count = count;
        ids = NULL;
        details = NULL;
    }
done:
    free(line);
    for (i = 0; details && i < used; ++i)
        free(details[i]);
    free(details);
    free(ids);
    return status;
}

AdsIndexStatus ads_index_open(const char *index_dir, AdsIndex **out_index)
{
    const char *names[] = {"metadata.tsv", "postings.tsv", "documents.tsv"};
    TokenBuffer tokens = {0};
    AdsIndexInfo expected = {0};
    AdsIndex *index = NULL;
    AdsIndexStatus status = ADS_INDEX_OK;
    size_t i;
    if (!out_index)
        return ADS_INDEX_INVALID_ARGUMENT;
    *out_index = NULL;
    if (!index_dir || !*index_dir)
        return ADS_INDEX_INVALID_ARGUMENT;
    for (i = 0; i < 3; ++i)
    {
        char *path = index_path(index_dir, names[i]);
        FILE *file;
        if (!path)
        {
            status = ADS_INDEX_OUT_OF_MEMORY;
            break;
        }
        file = open_file(path, 0);
        free(path);
        if (!file)
        {
            status = ADS_INDEX_IO_ERROR;
            break;
        }
        if (i == 0)
            status = load_metadata(file, &expected);
        else if (i == 1)
        {
            status = load_postings(file, &tokens);
            if (status == ADS_INDEX_OK)
                status = assemble(&tokens, NULL, &index);
            if (status == ADS_INDEX_OK &&
                (index->term_count != expected.term_count ||
                 index->indexed_position_count != expected.indexed_position_count))
                status = ADS_INDEX_FORMAT_ERROR;
        }
        else
            status = load_documents(file, index, expected.document_count);
        fclose(file);
        if (status != ADS_INDEX_OK)
            break;
    }
    tokens_free(&tokens);
    if (status != ADS_INDEX_OK)
        ads_index_close(index);
    else
        *out_index = index;
    return status;
}

/* 保存为三个 UTF-8 TSV；目录的父目录必须已经存在。
 * 覆盖已有目录不是事务操作，建议每个索引版本使用独立输出目录。 */
AdsIndexStatus ads_index_save(const AdsIndex *index, const char *index_dir)
{
    const char *names[] = {"documents.tsv", "postings.tsv", "metadata.tsv"};
    size_t file_index, i, j, k;
    if (!index || !index_dir || !*index_dir)
        return ADS_INDEX_INVALID_ARGUMENT;
    if (make_directory(index_dir) != 0)
        return ADS_INDEX_IO_ERROR;
    for (file_index = 0; file_index < 3; ++file_index)
    {
        char *path = index_path(index_dir, names[file_index]);
        FILE *file;
        int failed;
        if (!path)
            return ADS_INDEX_OUT_OF_MEMORY;
        file = open_file(path, 1);
        free(path);
        if (!file)
            return ADS_INDEX_IO_ERROR;
        if (file_index == 0)
        {
            fprintf(file, DOCUMENT_HEADER "\n");
            for (i = 0; i < index->document_count; ++i)
                fprintf(file, "%" PRIu32 "\t%s\n", index->document_ids[i],
                        index->document_fields ? index->document_fields[i] : "\t\t");
        }
        else if (file_index == 1)
        {
            fprintf(file, "stem\tdoc_id\ttf\tpositions\n");
            for (i = 0; i < index->term_count; ++i)
                for (j = 0; j < index->terms[i].posting_count; ++j)
                {
                    const AdsPosting *posting = &index->terms[i].postings[j];
                    fprintf(file, "%s\t%" PRIu32 "\t%zu\t",
                            index->terms[i].stem, posting->doc_id, posting->position_count);
                    for (k = 0; k < posting->position_count; ++k)
                        fprintf(file, "%s%" PRIu64, k ? "," : "", posting->positions[k]);
                    fputc('\n', file);
                }
        }
        else
        {
            fprintf(file, "key\tvalue\nformat_version\t%" PRIu32 "\ndocument_count\t%zu\nterm_count\t%zu"
                          "\nindexed_position_count\t%zu\nposition_base\t0\n",
                    index->format_version, index->document_count,
                    index->term_count, index->indexed_position_count);
        }
        failed = ferror(file);
        if (fclose(file) != 0)
            failed = 1;
        if (failed)
            return ADS_INDEX_IO_ERROR;
    }
    return ADS_INDEX_OK;
}
