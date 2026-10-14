# A 部分接口与交接说明

## 共同规则

1. 核心代码为 C11。所有输出为 UTF-8 无 BOM，换行 LF，TSV 首行为列名。
2. 分词严格使用 `[A-Za-z]+` 并转为 ASCII 小写。数字、标点、撇号、连字符、非 ASCII 字符均作为分隔符。例如 `King's` → `king, s`，`ne'er` → `ne, er`，`ice-cold` → `ice, cold`。这是一项显式简化，索引和查询必须使用同一实现。
3. position 是**清洗正文的原始词序号**，从 0 开始，在去停用词前计数；不是 HTML 行号，也不是过滤后输出的行号。
4. 先按规范化原词形检查正式停用词，再对剩余词做 Porter 词干化。形态归并是词干化，不是语义词形还原，例如 `does` 的词干为 `doe`。
5. 保留句内和行间的完整词序列，用于短语核验；不对相邻的不同词位去重。普通多词查询需要由 C 对有效词干去重。
6. 本版规则标识为 `ads-a-v1`；`data/preprocessing.json` 包含停用词、来源清单及 tokens 的 SHA256。停用词或分词变化后必须重建索引。

## 成员二接入

正式文件：

```text
data/documents.tsv
doc_id<TAB>work_title<TAB>source_url<TAB>clean_path

data/tokens.tsv
doc_id<TAB>position<TAB>stem
```

其中 `<TAB>` 表示真实制表符。文档按 ID 升序排列，tokens 按文档 ID 和原始词位升序输出，ID 范围为 1 至 912。`clean_path` 相对项目根目录，文档清单保留来源和作品信息。

可直接调用现有 B 接口：

```c
AdsIndex *index = NULL;
AdsIndexStatus status = ads_index_build_from_tokens(
    "data/tokens.tsv", NULL, "data/documents.tsv", &index);
/* 检查 status；保存、查询后调用 ads_index_close(index)。 */
```

这里第二个参数必须传 `NULL`，因为 tokens 已经完成按原词形去停用词。实际例子：默认规则保留 `having` 并归为词干 `have`；保留 `its` 并归为 `it`。如果 B 再拿原词形停用词表过滤词干，就会误删这些合法位置。

`src/preprocess/build_index_from_a.c` 是完整调用示例。复现后的默认实验索引位于 `build/index_balanced`，包含 912 篇文档、14,590 个词干、468,772 个位置。B 原有源码与测试未修改。

## 成员三接入

包含头文件：

```c
#include "preprocess/AdsPreprocess.h"
```

共同编译的模块为 `src/preprocess/AdsPreprocess.c` 与 `third_party/porter/porter.c`，并加 `-I includes`。不要同时编译保留作溯源的 `porter_original.c`，它含作者的演示 main。

普通查询示例：

```c
AdsPreprocessor *pp = NULL;
AdsTokenList query = {0};
if (ads_pp_create("config/stopwords.txt", &pp) != ADS_PP_OK) {
    /* 数据错误，不能伪装成没有结果。 */
    return 3;
}
if (ads_pp_process(pp, "runs and queens", 0, &query) != ADS_PP_OK) {
    ads_pp_destroy(pp);
    return 3;
}
/* query 为 run[0]、queen[2]；对 query.tokens[i].stem 查询 B 的索引。 */
ads_pp_tokens_free(&query);
ads_pp_destroy(pp);
```

`ads_pp_tokenize` 只分词，不去停用词、不词干化；可用于检查 word 模式的原始输入词数。`ads_pp_process` 的第三个参数为 0 时过滤停用词，结果中仍保留原始 position。参数为 1 时保留全部词，并标注 `is_stopword`；停用词的 `stem` 等于原词形。

短语处理流程：

1. 对查询调用 `ads_pp_process(pp,text,1,&query)`，保留停用词及完整偏移。
2. 至少找一个非停用词作为倒排索引锚点。全停用词短语按 C 约定提示不支持。
3. 由锚点的原始词位与查询偏移计算候选起点。
4. 通过文档清单读取清洗正文，再调用 `ads_pp_process(pp,body,1,&document)`。
5. 对候选窗口逐词核验：停用词比较 `surface`，实词比较 `stem`，同时保证位置和长度。`king and queen` 与 `king the queen` 必须得到不同判断。
6. 释放每个 `AdsTokenList`；每个线程使用自己的 `AdsPreprocessor`，同一个 context 可依次重复使用。

多词匹配比例的分母是去停用词后**不同词干**的个数。先处理全停用词输入，避免除以零。word、multi、phrase 的参数与结果行为继续遵循成员三原有说明。

原清单与全文读取路径以项目根目录为基准，不能把 `clean_path` 相对 `build/index_balanced` 解释。正式文档数为 912；如果查询实现使用 `MAX_DOC` 静态数组，应保证容量够用，或改为动态存储。

## 可以直接核对的正式查询

以下是默认停用词策略下，调用统一预处理和 B 索引得到的词干匹配文档数：

| 输入 | 词干 | 文档数 |
| --- | --- | ---: |
| king | king | 377 |
| queen | queen | 187 |
| love | love | 604 |
| runs | run | 184 |
| walked | walk | 143 |
| not | not | 814 |
| having | have | 140 |
| its | it | 20 |

它们用于接入核对，尚未施加 C 的最低词频 t 或多词比例 τ；原词形 DF 与词干匹配文档数可能不同，原因是词干归并了多个词形。

## 下一步各自需要做什么

B 可以用正式 tokens 替换测试输入，继续其索引结果说明、性能分析及项目集成。

C 需要接入共享模块、正式文档清单和 B 的索引，完成单词、多词与连续短语查询；然后在固定默认索引上做 t、τ 实验。此次提供的 ZIP 里只有 C 的接口说明，A 的联调程序没有替代其完整查询程序。

三人合并 `report/member_A.md` 的相关章节，保持报告中的分词、停用词、词干化和阈值定义与实现一致。
