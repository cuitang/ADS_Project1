#ifndef ADS_SEARCH_QUERY_H
#define ADS_SEARCH_QUERY_H

#define MAX_DOC 2000
#define MAX_WORD 50

extern int query_doc[MAX_DOC];
extern int query_num[MAX_DOC];
extern int query_count;
extern int query_den;

/*
 * threshold:
 *   word  模式表示最低词频 t
 *   multi 模式表示匹配比例 tau
 *   phrase 模式忽略
 */
int query(const char *text, const char *mode, double threshold);

#endif