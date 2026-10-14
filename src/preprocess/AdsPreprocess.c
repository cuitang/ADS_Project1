#include "preprocess/AdsPreprocess.h"
#include "../../third_party/porter/porter.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#endif

struct AdsPreprocessor { char **stops; size_t count; struct stemmer *porter; };
typedef struct { char *data; size_t size, capacity; } Buffer;
static int letter(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static char lower(unsigned char c) { return (char)(c >= 'A' && c <= 'Z' ? c + ('a'-'A') : c); }
static char *copy_n(const char *s, size_t n)
{
    char *p;
    if (n == SIZE_MAX) return NULL;
    p = malloc(n + 1);
    if (p) { memcpy(p, s, n); p[n] = 0; }
    return p;
}
static int reserve(void **p, size_t *cap, size_t need, size_t width)
{
    size_t n; void *q;
    if (need <= *cap) return 1;
    n = *cap ? *cap : 32;
    while (n < need) { if (n > SIZE_MAX/2) { n = need; break; } n *= 2; }
    if (width && n > SIZE_MAX/width) return 0;
    q = realloc(*p, n * width);
    if (!q) return 0;
    *p = q; *cap = n; return 1;
}
static int put(Buffer *b, char c)
{
    if (b->size > SIZE_MAX-2 || !reserve((void **)&b->data, &b->capacity, b->size+2, 1)) return 0;
    b->data[b->size++] = c; b->data[b->size] = 0; return 1;
}
#ifdef _WIN32
static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    wchar_t *p;
    if (!n) return NULL;
    p = malloc((size_t)n * sizeof(*p));
    if (p && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, p, n)) { free(p); return NULL; }
    return p;
}
#endif
FILE *ads_pp_fopen(const char *path, const char *mode)
{
    if (!path || !mode) return NULL;
#ifdef _WIN32
    wchar_t *p = wide(path), *m = wide(mode); FILE *f = NULL;
    if (p && m) f = _wfopen(p, m);
    free(p); free(m); return f;
#else
    return fopen(path, mode);
#endif
}
static int mkdir_one(const char *path)
{
    int rc;
#ifdef _WIN32
    wchar_t *p = wide(path);
    if (!p) return 0;
    rc = _wmkdir(p); free(p);
#else
    rc = mkdir(path, 0777);
#endif
    return rc == 0 || errno == EEXIST;
}
int ads_pp_mkdirs(const char *path)
{
    char *p; size_t i; int ok = 1;
    if (!path || !*path) return 0;
    p = copy_n(path, strlen(path)); if (!p) return 0;
    for (i = 1; p[i]; ++i) {
        if ((p[i] == '/' || p[i] == '\\') && !(i == 2 && p[1] == ':')) {
            char c = p[i]; p[i] = 0;
            if (!mkdir_one(p)) { ok = 0; break; }
            p[i] = c;
        }
    }
    if (ok) ok = mkdir_one(p);
    free(p); return ok;
}
AdsPreprocessStatus ads_pp_read_line(FILE *f, char **out)
{
    Buffer b = {0}; int c;
    if (!f || !out) return ADS_PP_INVALID_ARGUMENT;
    *out = NULL;
    while ((c = fgetc(f)) != EOF && c != '\n') {
        if (c == 0) { free(b.data); return ADS_PP_FORMAT_ERROR; }
        if (!put(&b, (char)c)) { free(b.data); return ADS_PP_OUT_OF_MEMORY; }
    }
    if (ferror(f)) { free(b.data); return ADS_PP_IO_ERROR; }
    if (c == EOF && !b.size) { free(b.data); return ADS_PP_OK; }
    if (!b.data && !put(&b, '\0')) return ADS_PP_OUT_OF_MEMORY;
    if (b.size && b.data[b.size-1] == '\r') b.data[--b.size] = 0;
    if (b.size >= 3 && (unsigned char)b.data[0] == 0xef &&
        (unsigned char)b.data[1] == 0xbb && (unsigned char)b.data[2] == 0xbf)
        memmove(b.data, b.data+3, strlen(b.data+3)+1);
    *out = b.data; return ADS_PP_OK;
}
AdsPreprocessStatus ads_pp_read_text(const char *path, char **out)
{
    FILE *f; Buffer b = {0}; char chunk[8192]; size_t n;
    if (!out || !path) return ADS_PP_INVALID_ARGUMENT;
    *out = NULL; f = ads_pp_fopen(path, "rb"); if (!f) return ADS_PP_IO_ERROR;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) != 0) {
        if (memchr(chunk, 0, n)) { fclose(f); free(b.data); return ADS_PP_FORMAT_ERROR; }
        if (b.size > SIZE_MAX-n-1 || !reserve((void **)&b.data, &b.capacity, b.size+n+1, 1)) {
            fclose(f); free(b.data); return ADS_PP_OUT_OF_MEMORY;
        }
        memcpy(b.data+b.size, chunk, n); b.size += n; b.data[b.size] = 0;
    }
    if (ferror(f)) { fclose(f); free(b.data); return ADS_PP_IO_ERROR; }
    fclose(f);
    if (!b.data) b.data = copy_n("", 0);
    if (!b.data) return ADS_PP_OUT_OF_MEMORY;
    *out = b.data; return ADS_PP_OK;
}
AdsPreprocessStatus ads_pp_write_text(const char *path, const char *text)
{
    FILE *f; int bad;
    if (!path || !text) return ADS_PP_INVALID_ARGUMENT;
    f = ads_pp_fopen(path, "wb"); if (!f) return ADS_PP_IO_ERROR;
    bad = fputs(text, f) == EOF;
    if (fclose(f)) bad = 1;
    return bad ? ADS_PP_IO_ERROR : ADS_PP_OK;
}
static int compare_words(const void *a, const void *b)
{ return strcmp(*(char * const *)a, *(char * const *)b); }
AdsPreprocessStatus ads_pp_create(const char *path, AdsPreprocessor **out)
{
    AdsPreprocessor *ctx; FILE *f = NULL; char *line = NULL; size_t cap = 0;
    AdsPreprocessStatus status = ADS_PP_OK;
    if (!out) return ADS_PP_INVALID_ARGUMENT;
    *out = NULL; ctx = calloc(1, sizeof(*ctx)); if (!ctx) return ADS_PP_OUT_OF_MEMORY;
    ctx->porter = create_stemmer();
    if (!ctx->porter) { free(ctx); return ADS_PP_OUT_OF_MEMORY; }
    if (path) {
        f = ads_pp_fopen(path, "rb");
        if (!f) { ads_pp_destroy(ctx); return ADS_PP_IO_ERROR; }
        while ((status = ads_pp_read_line(f, &line)) == ADS_PP_OK && line) {
            size_t i;
            if (!*line || line[0] == '#') { free(line); line = NULL; continue; }
            for (i = 0; line[i]; ++i) if (line[i] < 'a' || line[i] > 'z') break;
            if (line[i]) { status = ADS_PP_FORMAT_ERROR; break; }
            if (!reserve((void **)&ctx->stops, &cap, ctx->count+1, sizeof(*ctx->stops))) {
                status = ADS_PP_OUT_OF_MEMORY; break;
            }
            ctx->stops[ctx->count++] = line; line = NULL;
        }
        free(line); fclose(f);
    }
    if (status != ADS_PP_OK) { ads_pp_destroy(ctx); return status; }
    if (ctx->count > 1) qsort(ctx->stops, ctx->count, sizeof(*ctx->stops), compare_words);
    *out = ctx; return ADS_PP_OK;
}
void ads_pp_destroy(AdsPreprocessor *ctx)
{
    size_t i; if (!ctx) return;
    for (i = 0; i < ctx->count; ++i) free(ctx->stops[i]);
    free(ctx->stops); free_stemmer(ctx->porter); free(ctx);
}
int ads_pp_is_stopword(const AdsPreprocessor *ctx, const char *word)
{
    if (!ctx || !word || !ctx->count) return 0;
    return bsearch(&word, ctx->stops, ctx->count, sizeof(*ctx->stops), compare_words) != NULL;
}
AdsPreprocessStatus ads_pp_stem(AdsPreprocessor *ctx, const char *word, char **out)
{
    size_t n, i; char *p; int last;
    if (!ctx || !word || !out) return ADS_PP_INVALID_ARGUMENT;
    *out = NULL; n = strlen(word);
    if (!n || n > INT_MAX) return ADS_PP_INVALID_ARGUMENT;
    for (i = 0; i < n; ++i) if (word[i] < 'a' || word[i] > 'z') return ADS_PP_INVALID_ARGUMENT;
    p = copy_n(word, n); if (!p) return ADS_PP_OUT_OF_MEMORY;
    last = stem(ctx->porter, p, (int)n-1); p[last+1] = 0; *out = p; return ADS_PP_OK;
}
void ads_pp_tokens_free(AdsTokenList *list)
{
    size_t i; if (!list) return;
    for (i = 0; i < list->count; ++i) { free(list->tokens[i].surface); free(list->tokens[i].stem); }
    free(list->tokens); list->tokens = NULL; list->count = 0;
}
AdsPreprocessStatus ads_pp_tokenize(const char *text, AdsTokenList *out)
{
    size_t i = 0, cap = 0;
    if (!out || !text) return ADS_PP_INVALID_ARGUMENT;
    out->tokens = NULL; out->count = 0;
    while (text[i]) {
        size_t begin, j, n; char *word;
        if (!letter((unsigned char)text[i])) { ++i; continue; }
        begin = i; while (letter((unsigned char)text[i])) ++i; n = i-begin;
        word = copy_n(text+begin, n);
        if (!word) goto memory;
        for (j = 0; j < n; ++j) word[j] = lower((unsigned char)word[j]);
        if (!reserve((void **)&out->tokens, &cap, out->count+1, sizeof(*out->tokens))) { free(word); goto memory; }
        out->tokens[out->count] = (AdsPreprocessToken){(uint64_t)out->count, word, NULL, 0};
        ++out->count;
    }
    return ADS_PP_OK;
memory:
    ads_pp_tokens_free(out); return ADS_PP_OUT_OF_MEMORY;
}
AdsPreprocessStatus ads_pp_process(AdsPreprocessor *ctx, const char *text, int keep, AdsTokenList *out)
{
    AdsTokenList all = {0}; AdsPreprocessStatus status; size_t i, n = 0;
    if (!ctx || !out || !text) return ADS_PP_INVALID_ARGUMENT;
    out->tokens = NULL; out->count = 0;
    status = ads_pp_tokenize(text, &all); if (status != ADS_PP_OK) return status;
    for (i = 0; i < all.count; ++i) {
        AdsPreprocessToken *t = &all.tokens[i];
        t->is_stopword = ads_pp_is_stopword(ctx, t->surface);
        if (t->is_stopword && !keep) { free(t->surface); t->surface = NULL; continue; }
        if (t->is_stopword) {
            t->stem = copy_n(t->surface, strlen(t->surface));
            status = t->stem ? ADS_PP_OK : ADS_PP_OUT_OF_MEMORY;
        } else status = ads_pp_stem(ctx, t->surface, &t->stem);
        if (status != ADS_PP_OK) { ads_pp_tokens_free(&all); return status; }
    }
    for (i = 0; i < all.count; ++i) if (all.tokens[i].surface) all.tokens[n++] = all.tokens[i];
    all.count = n; *out = all; return ADS_PP_OK;
}

/* Tolerant scanner for this pinned site, not a general browser HTML parser.
 * Quoted '>' is honored; comments/scripts are skipped. Inline tags do not
 * split words. Structural tags introduce whitespace, preventing fusion. */
static int tag_is(const char *name, const char *expected) { return strcmp(name, expected) == 0; }
static int whitespace(unsigned char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f'; }
static const char *find_case_insensitive(const char *text, const char *needle)
{
    size_t n=strlen(needle), i;
    for(;*text;++text) {
        for(i=0;i<n && text[i] && lower((unsigned char)text[i])==needle[i];++i) { }
        if(i==n) return text;
    }
    return NULL;
}
static int speech_anchor(const char *start, const char *end)
{
    const char *p;
    for(p=start;(size_t)(end-p)>4;++p) {
        const char *q; size_t i;
        if(p>start&&!whitespace((unsigned char)p[-1])) continue;
        if(lower((unsigned char)p[0])!='n'||lower((unsigned char)p[1])!='a'||
           lower((unsigned char)p[2])!='m'||lower((unsigned char)p[3])!='e') continue;
        q=p+4; while(q<end&&whitespace((unsigned char)*q)) ++q;
        if(q==end||*q++!='=') continue;
        while(q<end&&whitespace((unsigned char)*q)) ++q;
        if(q<end&&(*q=='\''||*q=='"')) ++q;
        for(i=0;i<6&&q+i<end&&lower((unsigned char)q[i])=="speech"[i];++i) { }
        if(i==6) return 1;
    }
    return 0;
}
static int entity(const char *s, size_t *used, unsigned long *value)
{
    const char *end = strchr(s, ';'); size_t n; unsigned long v = 0;
    if (!end || end-s > 16) return 0;
    n = (size_t)(end-s+1);
    if (!strncmp(s, "&amp;", n)) v = '&';
    else if (!strncmp(s, "&lt;", n)) v = '<';
    else if (!strncmp(s, "&gt;", n)) v = '>';
    else if (!strncmp(s, "&quot;", n)) v = '"';
    else if (!strncmp(s, "&apos;", n)) v = '\'';
    else if (!strncmp(s, "&nbsp;", n)) v = ' ';
    else if (s[1] == '#') {
        char *tail; int base = s[2] == 'x' || s[2] == 'X' ? 16 : 10;
        const char *digits = s + (base == 16 ? 3 : 2);
        errno = 0; v = strtoul(digits, &tail, base);
        if (errno || tail != end || tail == digits || v > 0x10ffffUL) return 0;
    } else { *used = n; *value = ' '; return 1; } /* no entity names become words */
    *used = n; *value = v; return 1;
}
AdsPreprocessStatus ads_pp_html_text(const char *html, AdsHtmlMode mode,
                                   const char *marker, char **out)
{
    size_t i = 0; unsigned depth = 0; int ignored = 0; Buffer b = {0};
    const char *capture = mode == ADS_HTML_TABLE ? "table" : "blockquote";
    if (!html || !out || (mode != ADS_HTML_TABLE && mode != ADS_HTML_BLOCKQUOTES)) return ADS_PP_INVALID_ARGUMENT;
    *out = NULL;
    while (html[i]) {
        if (ignored) {
            const char *end=find_case_insensitive(html+i,ignored==1?"</script":"</style");
            if(!end) { free(b.data); return ADS_PP_FORMAT_ERROR; }
            i=(size_t)(end-html);
        }
        if (html[i] == '<') {
            size_t j = i+1, k = 0; char name[32], quote = 0; int closing = 0;
            if (!strncmp(html+i, "<!--", 4)) {
                const char *end = strstr(html+i+4, "-->");
                if (!end) { free(b.data); return ADS_PP_FORMAT_ERROR; }
                i = (size_t)(end-html)+3; continue;
            }
            while (whitespace((unsigned char)html[j])) ++j;
            if (html[j] == '/') { closing = 1; ++j; }
            while (letter((unsigned char)html[j]) || (html[j] >= '0' && html[j] <= '9')) {
                if (k+1 < sizeof(name)) name[k++] = lower((unsigned char)html[j]);
                ++j;
            }
            name[k] = 0;
            for (; html[j]; ++j) {
                if (quote) { if (html[j] == quote) quote = 0; }
                else if (html[j] == '\'' || html[j] == '"') quote = html[j];
                else if (html[j] == '>') break;
            }
            if (!html[j]) { free(b.data); return ADS_PP_FORMAT_ERROR; }
            /* Many MIT scenes omit the final </blockquote>. Navigation
             * tables and new speaker anchors are semantic boundaries even
             * when markup nesting is broken. This also excludes role labels. */
            if(mode==ADS_HTML_BLOCKQUOTES && !closing &&
               (tag_is(name,"table") || (tag_is(name,"a")&&speech_anchor(html+i,html+j)))) {
                depth=0;
                if(!put(&b,'\n')) goto memory;
            }
            if (tag_is(name, "script")) ignored = closing ? 0 : 1;
            if (tag_is(name, "style")) ignored = closing ? 0 : 2;
            if (!ignored && tag_is(name, capture)) {
                if (closing) { if (depth) --depth; }
                else ++depth;
            }
            if (!ignored && (depth || tag_is(name, capture)) &&
                (tag_is(name, capture) || tag_is(name, "br") || tag_is(name, "p") ||
                 tag_is(name, "tr") || tag_is(name, "td") || tag_is(name, "div"))) {
                if (!put(&b, '\n')) goto memory;
            }
            i = j+1; continue;
        }
        if (depth && !ignored) {
            unsigned char ch = (unsigned char)html[i];
            if (ch == '&') {
                size_t used; unsigned long v;
                if (entity(html+i, &used, &v)) {
                    char c = v < 128 ? (char)v : ' ';
                    if (!put(&b, c ? c : ' ')) goto memory;
                    i += used; continue;
                }
            }
            /* The English corpus is ISO-8859-1/ASCII. Non-ASCII source bytes
             * become delimiters rather than invalid UTF-8 in clean output. */
            if (!put(&b, ch < 128 ? (char)ch : ' ')) goto memory;
        }
        ++i;
    }
    if (!b.data) b.data = copy_n("", 0);
    if (!b.data) goto memory;
    if (marker && *marker) {
        char *start = strstr(b.data, marker);
        if (!start) { free(b.data); return ADS_PP_FORMAT_ERROR; }
        memmove(b.data, start, strlen(start)+1); b.size = strlen(b.data);
    }
    if (!reserve((void **)&b.data, &b.capacity, b.size+2, 1)) goto memory;
    /* Collapse horizontal whitespace and repeated blank lines. */
    {
        size_t r, w = 0; int pending_space = 0, pending_line = 0;
        for (r = 0; b.data[r]; ++r) {
            unsigned char c = (unsigned char)b.data[r];
            if (whitespace(c)) { if (c == '\r' || c == '\n') pending_line = 1; else pending_space = 1; continue; }
            if (w && (pending_space || pending_line)) b.data[w++] = pending_line ? '\n' : ' ';
            pending_space = pending_line = 0; b.data[w++] = (char)c;
        }
        if (w) b.data[w++] = '\n';
        b.data[w] = 0;
    }
    *out = b.data; return ADS_PP_OK;
memory:
    free(b.data); return ADS_PP_OUT_OF_MEMORY;
}
const char *ads_pp_status_string(AdsPreprocessStatus s)
{
    switch (s) {
    case ADS_PP_OK: return "OK";
    case ADS_PP_INVALID_ARGUMENT: return "invalid argument";
    case ADS_PP_IO_ERROR: return "I/O error";
    case ADS_PP_FORMAT_ERROR: return "format error";
    case ADS_PP_OUT_OF_MEMORY: return "out of memory";
    default: return "unknown error";
    }
}
