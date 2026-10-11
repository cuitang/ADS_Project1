#include "index/AdsIndex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

static char *path_join(const char *dir, const char *name)
{
    size_t n = strlen(dir) + strlen(name) + 2;
    char *path = malloc(n);
    if (!path) exit(2);
    snprintf(path, n, "%s/%s", dir, name);
    return path;
}

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    int ok;
    if (!file) return 0;
    ok = fputs(text, file) >= 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

/* 忽略换行的 CR/LF 差异，其余字节必须完全一致。 */
static int normalized_character(FILE *file)
{
    int c;
    do { c = fgetc(file); } while (c == '\r');
    return c;
}

static int equal_files(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    int ok = 1, ca, cb;
    if (!fa || !fb) {
        if (fa) fclose(fa);
        if (fb) fclose(fb);
        return 0;
    }
    do {
        ca = normalized_character(fa);
        cb = normalized_character(fb);
        if (ca != cb) {
            /* 末行是否有换行不影响 TSV 内容。 */
            if (ca == EOF && cb == '\n' && normalized_character(fb) == EOF) break;
            if (cb == EOF && ca == '\n' && normalized_character(fa) == EOF) break;
            ok = 0; break;
        }
    } while (ca != EOF);
    if (ferror(fa) || ferror(fb)) ok = 0;
    fclose(fa);
    fclose(fb);
    return ok;
}

static int sample_tests(const char *scratch)
{
    AdsIndex *index = NULL, *loaded = NULL;
    AdsPostingListView view;
    AdsIndexInfo info;
    char *dir = path_join(scratch, "sample");
    char *postings = path_join(dir, "postings.tsv");
    char *documents = path_join(dir, "documents.tsv");
    char *copy_dir = path_join(scratch, "copy");
    char *copy_documents = path_join(copy_dir, "documents.tsv");

    CHECK(ads_index_open("data/test/index", &index) == ADS_INDEX_OK);
    CHECK(ads_index_get_info(index, &info) == ADS_INDEX_OK);
    CHECK(info.document_count == 4 && info.term_count == 4 &&
          info.indexed_position_count == 11 && info.format_version == 1);
    CHECK(ads_index_lookup_stem(index, "king", &view) == ADS_INDEX_OK);
    CHECK(view.posting_count == 3 && view.postings[0].doc_id == 1 &&
          view.postings[1].doc_id == 2 && view.postings[2].doc_id == 4);
    CHECK(view.postings[0].position_count == 1 && view.postings[0].positions[0] == 1);
    CHECK(ads_index_lookup_stem(index, "queen", &view) == ADS_INDEX_OK);
    CHECK(view.postings[0].doc_id == 3 && view.postings[0].positions[0] == 0);
    CHECK(ads_index_save(index, copy_dir) == ADS_INDEX_OK);
    CHECK(equal_files("data/test/index/documents.tsv", copy_documents));
    ads_index_close(index);
    index = NULL;

    CHECK(ads_index_build_from_tokens("data/test/expected_tokens.tsv",
          "data/test/demo_stopwords.txt", "data/sample/documents.tsv",
          &index) == ADS_INDEX_OK);
    CHECK(ads_index_save(index, dir) == ADS_INDEX_OK);
    CHECK(equal_files("data/test/expected_index.tsv", postings));
    CHECK(equal_files("data/sample/documents.tsv", documents));
    CHECK(ads_index_open(dir, &loaded) == ADS_INDEX_OK);
    ads_index_close(index);
    index = NULL; /* 重新打开的对象独立拥有内存。 */
    CHECK(ads_index_lookup_stem(loaded, "walk", &view) == ADS_INDEX_OK);
    CHECK(view.posting_count == 3 && view.postings[2].doc_id == 3 &&
          view.postings[2].positions[0] == 3);
    CHECK(ads_index_lookup_stem(loaded, "missing", &view) == ADS_INDEX_NOT_FOUND);
    CHECK(view.postings == NULL && view.posting_count == 0);
    CHECK(ads_index_lookup_stem(NULL, "walk", &view) == ADS_INDEX_INVALID_ARGUMENT);
    CHECK(view.postings == NULL && view.posting_count == 0);
    ads_index_close(loaded);
    free(dir); free(postings); free(documents); free(copy_dir); free(copy_documents);
    return 0;
}

static int edge_tests(const char *scratch)
{
    char *tokens = path_join(scratch, "tokens.tsv");
    char *stopwords = path_join(scratch, "stopwords.txt");
    char *documents = path_join(scratch, "documents.tsv");
    char *dir = path_join(scratch, "edges");
    char *empty_dir = path_join(scratch, "empty");
    char *metadata = path_join(dir, "metadata.tsv");
    char *postings = path_join(dir, "postings.tsv");
    char *saved_docs = path_join(dir, "documents.tsv");
    AdsIndex *index = NULL, *loaded = NULL;
    AdsIndexInfo info;
    AdsPostingListView view;

    CHECK(write_text(tokens, "\xef\xbb\xbf" "doc_id\tposition\tstem\r\n"
          "42\t9\tking\r\n42\t0\tthe\r\n7\t0\tand\r\n"
          "42\t5\tqueen\r\n42\t2\tking"));
    CHECK(write_text(stopwords, "\xef\xbb\xbf" "the\r\nand\r\nand\r\n\r\n"));
    CHECK(write_text(documents, "doc_id\twork_title\tsource_url\tclean_path\n"
          "7\tStopwords only\t\t\n42\tRepeated king\t\t\n99\tEmpty\t\t\n"));
    CHECK(ads_index_build_from_tokens(tokens, stopwords, documents, &index) == ADS_INDEX_OK);
    CHECK(ads_index_get_info(index, &info) == ADS_INDEX_OK);
    CHECK(info.document_count == 3 && info.term_count == 2 && info.indexed_position_count == 3);
    CHECK(ads_index_lookup_stem(index, "king", &view) == ADS_INDEX_OK);
    CHECK(view.posting_count == 1 && view.postings[0].doc_id == 42 &&
          view.postings[0].position_count == 2 &&
          view.postings[0].positions[0] == 2 && view.postings[0].positions[1] == 9);
    CHECK(ads_index_lookup_stem(index, "the", &view) == ADS_INDEX_NOT_FOUND);
    CHECK(ads_index_lookup_stem(index, "queen", &view) == ADS_INDEX_OK);
    CHECK(view.postings[0].positions[0] == 5);
    CHECK(ads_index_save(index, dir) == ADS_INDEX_OK);
    CHECK(ads_index_open(dir, &loaded) == ADS_INDEX_OK);
    CHECK(ads_index_get_info(loaded, &info) == ADS_INDEX_OK && info.document_count == 3);
    ads_index_close(loaded); loaded = NULL;
    ads_index_close(index); index = NULL;

    CHECK(ads_index_build_from_tokens(tokens, NULL, NULL, &index) == ADS_INDEX_OK);
    CHECK(ads_index_get_info(index, &info) == ADS_INDEX_OK);
    CHECK(info.document_count == 2 && info.term_count == 4 && info.indexed_position_count == 5);
    CHECK(ads_index_save(index, dir) == ADS_INDEX_OK);
    CHECK(ads_index_open(dir, &loaded) == ADS_INDEX_OK);
    ads_index_close(loaded); loaded = NULL;
    ads_index_close(index); index = NULL;

    CHECK(write_text(tokens, "doc_id\tposition\tstem\n"));
    CHECK(ads_index_build_from_tokens(tokens, NULL, NULL, &index) == ADS_INDEX_OK);
    CHECK(ads_index_save(index, empty_dir) == ADS_INDEX_OK);
    CHECK(ads_index_open(empty_dir, &loaded) == ADS_INDEX_OK);
    CHECK(ads_index_get_info(loaded, &info) == ADS_INDEX_OK &&
          info.document_count == 0 && info.term_count == 0 && info.indexed_position_count == 0);
    ads_index_close(loaded); loaded = NULL;
    ads_index_close(index); index = NULL;

    CHECK(write_text(tokens, "doc_id\tposition\tstem\n7\t0\tand\n"));
    CHECK(ads_index_build_from_tokens(tokens, stopwords, NULL, &index) == ADS_INDEX_OK);
    CHECK(ads_index_save(index, empty_dir) == ADS_INDEX_OK);
    CHECK(ads_index_open(empty_dir, &loaded) == ADS_INDEX_OK);
    CHECK(ads_index_get_info(loaded, &info) == ADS_INDEX_OK &&
          info.document_count == 1 && info.term_count == 0);
    ads_index_close(loaded); loaded = NULL;
    ads_index_close(index); index = NULL;

    CHECK(ads_index_build_from_tokens(NULL, NULL, NULL, &index) == ADS_INDEX_INVALID_ARGUMENT);
    CHECK(index == NULL);
    CHECK(ads_index_open(NULL, &index) == ADS_INDEX_INVALID_ARGUMENT && index == NULL);
    CHECK(ads_index_save(NULL, dir) == ADS_INDEX_INVALID_ARGUMENT);
    CHECK(ads_index_build_from_tokens("missing-file.tsv", NULL, NULL, &index) == ADS_INDEX_IO_ERROR);
    CHECK(index == NULL);
    CHECK(ads_index_open("missing-index-directory", &index) == ADS_INDEX_IO_ERROR && index == NULL);
    CHECK(write_text(tokens, ""));
    CHECK(ads_index_build_from_tokens(tokens, NULL, NULL, &index) == ADS_INDEX_FORMAT_ERROR && index == NULL);
    CHECK(write_text(tokens, "bad-header\n"));
    CHECK(ads_index_build_from_tokens(tokens, NULL, NULL, &index) == ADS_INDEX_FORMAT_ERROR);
    CHECK(write_text(tokens, "doc_id\tposition\tstem\n1\t0\tking\n1\t0\tqueen\n"));
    CHECK(ads_index_build_from_tokens(tokens, NULL, NULL, &index) == ADS_INDEX_FORMAT_ERROR);
    CHECK(write_text(tokens, "doc_id\tposition\tstem\n4294967296\t0\tking\n"));
    CHECK(ads_index_build_from_tokens(tokens, NULL, NULL, &index) == ADS_INDEX_FORMAT_ERROR);
    CHECK(write_text(tokens, "doc_id\tposition\tstem\n1\t18446744073709551616\tking\n"));
    CHECK(ads_index_build_from_tokens(tokens, NULL, NULL, &index) == ADS_INDEX_FORMAT_ERROR);
    CHECK(write_text(tokens, "doc_id\tposition\tstem\n1\t0\tking\n"));
    CHECK(ads_index_build_from_tokens(tokens, NULL, documents, &index) == ADS_INDEX_FORMAT_ERROR);
    CHECK(index == NULL);

    CHECK(write_text(metadata, "key\tvalue\nformat_version\t2\ndocument_count\t2\n"
          "term_count\t4\nindexed_position_count\t5\nposition_base\t0\n"));
    CHECK(ads_index_open(dir, &index) == ADS_INDEX_UNSUPPORTED_VERSION && index == NULL);
    CHECK(write_text(metadata, "key\tvalue\nformat_version\t1\ndocument_count\t2\n"
          "term_count\t4\nindexed_position_count\t123\nposition_base\t0\n"));
    CHECK(ads_index_open(dir, &index) == ADS_INDEX_FORMAT_ERROR);
    CHECK(write_text(metadata, "key\tvalue\nformat_version\t1\ndocument_count\t2\n"
          "term_count\t4\nindexed_position_count\t5\nposition_base\t0\n"));
    CHECK(write_text(postings, "stem\tdoc_id\ttf\tpositions\nking\t42\t2\t9,2\n"));
    CHECK(ads_index_open(dir, &index) == ADS_INDEX_FORMAT_ERROR);
    CHECK(write_text(postings, "stem\tdoc_id\ttf\tpositions\nking\t42\t2\t2\n"));
    CHECK(ads_index_open(dir, &index) == ADS_INDEX_FORMAT_ERROR);
    CHECK(write_text(postings, "stem\tdoc_id\ttf\tpositions\n"
          "and\t7\t1\t0\nking\t42\t2\t2,9\nqueen\t42\t1\t5\nthe\t42\t1\t0\n"));
    CHECK(write_text(saved_docs, "doc_id\twork_title\tsource_url\tclean_path\n"
          "7\t\t\t\n8\t\t\t\n"));
    CHECK(ads_index_open(dir, &index) == ADS_INDEX_FORMAT_ERROR);
    ads_index_close(NULL);
    free(tokens); free(stopwords); free(documents); free(dir); free(empty_dir);
    free(metadata); free(postings); free(saved_docs);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Run from project root: index_operation_test <existing scratch directory>\n");
        return 2;
    }
    if (sample_tests(argv[1]) || edge_tests(argv[1])) return 1;
    puts("PASS: sample open/lookup, build/save/open, metadata preservation, filtering, repeated positions, empty indexes and malformed inputs");
    return 0;
}
