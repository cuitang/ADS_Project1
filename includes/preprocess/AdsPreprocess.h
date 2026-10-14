#ifndef ADS_PREPROCESS_H
#define ADS_PREPROCESS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* C11 shared by corpus generation and member C's query processing.
 * All paths are UTF-8. Text tokenization is exactly [A-Za-z]+, lowercased.
 * Positions count ALL tokens, starting at zero, BEFORE stopword removal.
 */
typedef enum {
    ADS_PP_OK = 0, ADS_PP_INVALID_ARGUMENT, ADS_PP_IO_ERROR,
    ADS_PP_FORMAT_ERROR, ADS_PP_OUT_OF_MEMORY
} AdsPreprocessStatus;

typedef struct AdsPreprocessor AdsPreprocessor;
typedef struct {
    uint64_t position;
    char *surface;               /* normalized original spelling */
    char *stem;                  /* NULL for tokenize(); owned otherwise */
    int is_stopword;
} AdsPreprocessToken;
typedef struct { AdsPreprocessToken *tokens; size_t count; } AdsTokenList;
typedef enum { ADS_HTML_BLOCKQUOTES, ADS_HTML_TABLE } AdsHtmlMode;

/* NULL stopwords_path means no stopword filtering, NOT an implicit default.
 * A missing named file is an error. Each context owns its Porter state. */
AdsPreprocessStatus ads_pp_create(const char *stopwords_path, AdsPreprocessor **out);
void ads_pp_destroy(AdsPreprocessor *context);
int ads_pp_is_stopword(const AdsPreprocessor *context, const char *surface);
AdsPreprocessStatus ads_pp_stem(AdsPreprocessor *context, const char *surface, char **out);
AdsPreprocessStatus ads_pp_tokenize(const char *text, AdsTokenList *out);
/* keep_stopwords=1 supplies the COMPLETE sequence for phrase verification.
 * Stopword stems equal their surface forms. Non-stopwords use Porter.
 * keep_stopwords=0 is the document-index / ordinary-query operation.
 * Free every successful result, including empty results, with tokens_free(). */
AdsPreprocessStatus ads_pp_process(AdsPreprocessor *context, const char *text,
                                 int keep_stopwords, AdsTokenList *out);
void ads_pp_tokens_free(AdsTokenList *list);

/* Site-specific extraction: blockquotes for plays/poems, table for Elegy.
 * Includes stage directions; excludes speaker labels outside blockquotes.
 * Optional start_marker excludes Venus's prefatory blockquotes. */
AdsPreprocessStatus ads_pp_html_text(const char *html, AdsHtmlMode mode,
                                   const char *start_marker, char **out);
AdsPreprocessStatus ads_pp_read_text(const char *path, char **out);
AdsPreprocessStatus ads_pp_write_text(const char *path, const char *text);
FILE *ads_pp_fopen(const char *path, const char *mode);
int ads_pp_mkdirs(const char *path);
/* Dynamically allocated line, strips CR/LF and first-line BOM if present.
 * EOF: OK and *out==NULL. Embedded NUL: FORMAT_ERROR. */
AdsPreprocessStatus ads_pp_read_line(FILE *file, char **out);
const char *ads_pp_status_string(AdsPreprocessStatus status);
#endif
