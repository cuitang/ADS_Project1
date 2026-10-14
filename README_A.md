# ADS Project 1 成员一交付

本版本接入 2026 年 10 月 14 日提供的 `ADS_Project1-master.zip`。A 部分的正式语料、C 预处理模块、词频统计、停用词分析、测试和报告内容已完成，并与包内成员二的倒排索引联调。

## 正式成果

| 项目 | 结果 |
| --- | ---: |
| 正式文档 | 912 |
| 戏剧场景 | 754，来自主页列出的 37 部戏剧 |
| 十四行诗 | 154 |
| 其他诗作 | 4 |
| 正文词次 | 909,915 |
| 不同原词形 | 23,524 |
| 默认停用词 | 89 |
| 过滤后的索引词位 | 468,772 |
| 接入 B 后的不同词干 | 14,590 |

## 运行

将 A 提交包的内容按原目录结构放进已有项目根目录，在该根目录运行：

```powershell
.\run_A.cmd
```

需要 GCC，脚本先查找 PATH，再尝试本机的 `C:\msys64\ucrt64\bin\gcc.exe`。也可指定：

```powershell
.\run_A.cmd -Compiler '你的gcc.exe完整路径'
```

脚本编译和运行所有 A 程序、A 测试、包内 B 测试，以及 A/B 全量联调和三组停用词实验。`build` 目录会生成可执行文件、临时 tokens 和实验索引；实际提交数据与实验记录放在 `data`、`config`、`results/preprocess`。

可另外运行独立统计复核和 Bonus 的词汇规模实验：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/verify_A.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/benchmark_A.ps1
```

正式计算由 C 程序完成：清洗、分词、TF/DF、停用词选择、词干化和 tokens 生成。PowerShell 负责本地来源清单、编译运行、独立复核、实验调度与记录。

## 先看哪些文件

| 文件或目录 | 用途 |
| --- | --- |
| `docs/A_接口与交接说明.md` | B/C 必须阅读的输入格式、函数及接入规则 |
| `docs/A_实现与复现步骤.md` | 各步命令和实现原理 |
| `includes/preprocess/AdsPreprocess.h` | 查询端共享的 C 接口 |
| `src/preprocess` | A 的模块、命令行和 A/B 接入示例 |
| `data/documents.tsv` | 正式文档 ID 和正文路径 |
| `data/clean` | 912 篇清洗正文 |
| `data/tokens.tsv` | B 可直接读取的正式三列词位文件 |
| `data/preprocessing.json` | 规则版本与配置校验值 |
| `config/stopwords.txt` | 默认正式停用词表 |
| `config/sources.tsv` | 确定文档范围的来源与提取方式 |
| `results/preprocess` | 完整词频、阈值对比、测试、来源和实验记录 |
| `report/member_A.md` | 可合并到最终报告的 A 部分中文内容及 Bonus |
| `REFERENCES_A.md` | 语料与词干化引用、第三方代码说明 |

原来的四篇小样例及 B/C 文件保持原样。正式文档与样例分开使用各自的文档清单，不能混合两个清单的 ID。

## 交接重点

B 使用 `data/tokens.tsv` 和 `data/documents.tsv`，建索引的停用词参数传 `NULL`：A 已先去停用词，再词干化。

C 链接共享 C 模块，以 `config/stopwords.txt` 初始化；单词及多词查询使用 `ads_pp_process(...,0,...)`，短语核验使用 `ads_pp_process(...,1,...)` 保留完整词序列。文档路径相对项目根目录。

提供的压缩包没有 C 的查询源码，所以本交付验证的是共享预处理和 B 的真实索引；完整查询程序及其 t、τ 实验由 C 接入后继续完成。
