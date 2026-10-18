#ifndef TEXT_H
#define TEXT_H

/*
 * text.h 文字切词模块
 *
 * 把查询串和文档正文切成词，供查询和短语核验使用。
 * 分词规则、停用词过滤、词干化由预处理模块完成。
 */

/* 初始化。stopwords_path 为停用词文件路径，NULL 表示不过滤。
   成功返回 1，失败返回 0。 */
int  text_open(const char *stopwords_path);
void text_close(void);

/* 把一段文字切成词。
   keep_stopwords 为 1 时保留停用词，为 0 时去掉。
   返回词数，出错返回 -1。
   结果由本模块保存，下一次调用时被覆盖。 */
int text_split(const char *s, int keep_stopwords);

/* 读文本文件并切词。返回词数，出错返回 -1。 */
int text_split_file(const char *path, int keep_stopwords);

/* 取上一次切词的结果 */
int         text_count(void);
const char *text_surface(int i);   /* 第 i 个词的词形，已转小写 */
const char *text_stem(int i);      /* 第 i 个词的词干，停用词返回 NULL */
int         text_is_stop(int i);   /* 第 i 个词是否为停用词 */

#endif
