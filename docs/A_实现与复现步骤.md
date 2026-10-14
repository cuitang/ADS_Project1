# A 部分实现与复现步骤

## 文件流程

```text
固定版本的原始网站网页
  → prepare_corpus.ps1 依据主页入口选择唯一来源并生成 sources.tsv
  → ads_prepare clean 由 C 提取正文，生成 912 个 clean 文本与 documents.tsv
  → ads_prepare count 由 C 统计原词形 TF、DF 和两个占比
  → ads_prepare select 由 C 生成三组停用词和决策表
  → ads_prepare emit 由共享 C 模块输出过滤后的原始词位和词干
  → B 的现有接口构建、保存和重新加载真实倒排索引
```

## 选定语料范围

`scripts/prepare_corpus.ps1` 从原始首页读取 37 个戏剧目录，只收录各目录列出的场景链接；诗歌目录中的十四行诗按 154 首单独收录，另收录 4 篇诗作。最终为 754 个戏剧场景与 158 个诗歌文档，共 912 篇。排除整剧页、导航页及仓库中的旧版重复目录。

文档按来源相对路径的 Ordinal 顺序排序，再分配 1 至 912 的 ID。`config/sources.tsv` 保存网页、正文路径、提取方式和特殊起始标记；`results/preprocess/raw_checksums.tsv` 保存 951 个已归档网页的校验值，包括 912 个正文来源与 39 个目录来源。数据版本固定为 `6b82db852c7322dd33e95db347f0ddfc812c6409`。

## C 正文提取

戏剧正文从 blockquote 中提取，包含台词和舞台说明，排除说话人标签、作品标题、场景标题和导航。该站点部分 HTML 缺少结束标签，因此扫描器还把新台词锚点与导航 table 作为正文边界；不能只按开始结束标签深度机械截取。

诗歌一般提取 blockquote；Venus and Adonis 从首节 `EVEN as the sun` 开始，排除前置题献；Rape of Lucrece 取诗节，排除前置题献与 Argument；Elegy 使用 table 中的诗行，排除编辑说明与题献。按课程指定网站的作品目录收录 Elegy，不额外推断其作者归属。

扫描器处理大小写标签、带引号的属性、注释、script/style、换行标签和常见实体。源文件保留原始字节；清洗输出采用 ASCII 子集的 UTF-8，非 ASCII 源字符作为分隔符，避免把 ISO-8859-1 原始字节写成非法 UTF-8。此规则与当前英文语料配套，若换用其他语种或版本须重新设计并重建索引。

## C 分词与统计

分词使用 `[A-Za-z]+`，逐词转小写。每篇清洗文档的第一词为 position 0，去停用词后不改变位置。

原词形统计使用开放寻址哈希表，装载率约 0.7 时扩容。每个词保存 TF、DF 和 last_doc。文档按 ID 递增读取，同一文档内反复遇到某词只增加 TF；第一次遇到该文档中的词时才增加 DF。统计结束后对词项指针按 TF 降序、同频字母序排序输出。

计数和词位采用 uint64_t，文档 ID 与 B 的 uint32_t 兼容。正文和 token 数组按文档分配并释放，词典在内存中保存整套词汇；这不是外存无限规模方案。

## 单步执行

`run_A.cmd` 会完成编译。编译完成后，在根目录可以逐步执行：

```powershell
.\build\ads_prepare.exe clean config/sources.tsv data/documents.tsv
.\build\ads_prepare.exe count data/documents.tsv results/preprocess/word_counts.tsv
.\build\ads_prepare.exe select results/preprocess/word_counts.tsv config/function_words.txt config results/preprocess
.\build\ads_prepare.exe emit data/documents.tsv config/stopwords.txt data/tokens.tsv results/preprocess/document_stats.tsv
.\build\build_index_from_a.exe data/tokens.tsv data/documents.tsv build/index_balanced
```

`emit` 的停用词路径可用 `-` 表示明确不去停用词。命名的停用词文件不存在时会报错，不会自动退回空表。空文本会生成零个词，错误与空结果分开处理。

## 验证范围

- `test_preprocess.c` 验证 HTML 边界、实体、标签内小于号、缺失闭合标签、大小写、长词、空输入、标点、停用词位置和词干碰撞；逐词对照 Porter 官方 23,531 组输入输出。
- 使用已有四篇样例，C CLI 的 tokens 与人工预期逐行一致；A 的输出经 B 保存后，postings 也与人工预期一致。
- 保留并运行 B 原有测试，包括格式错误、重复位置、空索引及持久化验证。
- 正式索引重新加载后，扫描全部 912 篇文档，对 12 个查询词核对文档号和每个位置，包括 `having → have`、`its → it` 的碰撞回归。
- `verify_A.ps1` 使用独立 .NET 正则，逐一核对全部 23,524 个原词形 TF/DF、912 篇文档的过滤前后计数、全部来源校验值及清洗文本重复情况。

`results/preprocess/test_results.json`、`independent_verification.json` 与 `run_A.log` 是实际运行记录。完整查询端源码不在用户提供的 ZIP 中，因此没有宣称验证 C 的全部查询功能。
