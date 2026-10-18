# ADS Mini Search Engine

面向莎士比亚全集的简易搜索引擎，包含词频统计、停用词筛选、词干化、倒排索引构建和单词、多词、短语查询。

## 项目结构

| 路径 | 内容 |
| --- | --- |
| `config/` | 来源与提取方式清单、人工功能词候选及各策略的停用词表 |
| `data/` | 原始网页、清洗正文、tokens、文档清单、样例数据和正式索引 |
| `includes/` | `index`、`preprocess`、`search` 模块的公共头文件 |
| `src/` | 各模块实现；`src/main.c` 为命令行入口 |
| `scripts/` | 语料准备、预处理联调、独立验证和规模实验脚本 |
| `tests/` | C 测试程序及查询阈值测试脚本 |
| `results/` | 验证日志、统计数据和阈值实验结果 |
| `third_party/` | Porter 词干化实现及溯源材料 |
| `docs/` | 接口约定及复现说明 |
| `report/` | 各成员报告 |
| `build/` | 编译生成的程序、对象文件和实验临时产物；不纳入 Git |
| `Makefile` | 查询主程序的编译规则 |
| `run_A.cmd` | A 部分及 A/B 联调流程的 Windows 入口 |

## 编译主程序

需要支持 C11 的 GCC、GNU Make 和 PowerShell。以下命令均在项目根目录执行，示例采用 Windows PowerShell 语法。

```powershell
mingw32-make main
```

如果 GNU Make 的命令名为 `make`，则使用 `make main`。输出程序为 `build/search.exe`；修改源码后再次执行即可增量编译。Makefile 只负责构建主程序，测试由下文的脚本执行。

## 构建倒排索引

准备好 `data/tokens.tsv` 和 `data/documents.tsv` 后执行：

```powershell
.\build\search.exe build `
    --tokens data\tokens.tsv `
    --documents data\documents.tsv `
    --index data\index
```

| 参数 | 含义 |
| --- | --- |
| `--tokens` | 必填。带表头的 TSV，三列为 `doc_id`、`position`、`stem` |
| `--documents` | 可选。带表头的 TSV，四列为 `doc_id`、`work_title`、`source_url`、`clean_path` |
| `--index` | 必填。索引输出目录；父目录须存在，已有索引文件会被覆盖 |
| `--stops` | 可选。对输入词干额外执行停用词过滤；省略或传 `-` 表示不过滤 |

正式 `data/tokens.tsv` 已由 A 完成分词、停用词过滤和词干化，应省略构建时的 `--stops`，避免按原词形停用词表再次过滤词干。`config/sources.tsv` 是预处理来源清单，不能替代 `--documents` 所需的文档清单。

输出目录包含 `metadata.tsv`（版本和统计）、`postings.tsv`（词干、文档编号、词频和位置）、`documents.tsv`（文档信息）。词位从 0 开始，停用词过滤后不重新编号。

## 查询

查询默认使用 `data/index` 和 `config/stopwords.txt`。可通过 `--index`、`--stops` 指定其他索引和配套停用词表。构建正式索引时省略 `--stops`，查询时仍需停用词表来处理用户输入。

查看索引规模：

```powershell
.\build\search.exe info
```

### 单词查询

`--t N` 表示词干在文档中至少出现 N 次，默认为 1。下面查询 `king` 至少出现两次的文档：

```powershell
.\build\search.exe word king `
    --t 2 `
    --index data\index `
    --stops config\stopwords.txt
```

### 多词查询

`--tau R` 表示“文档命中的不同有效查询词干数 / 查询中不同有效词干总数”的下限，取值为 0 到 1，默认为 1。查询先去停用词，再对词干去重；即使阈值为 0，也只返回至少命中一个有效词干的文档。

```powershell
.\build\search.exe multi king queen love `
    --tau 0.50 `
    --index data\index `
    --stops config\stopwords.txt
```

上例有三个不同有效词干，至少命中两个才会返回。

### 连续短语查询

短语查询读取清洗正文核验连续词序，实词比较词干，停用词比较原词形，不使用 `--t` 或 `--tau`。因此索引应包含文档正文路径，并从项目根目录运行；全部由停用词组成的短语暂不支持。

```powershell
.\build\search.exe phrase "to be or not to be" `
    --index data\index `
    --stops config\stopwords.txt
```

### CSV 输出

追加 `--csv` 可获得便于脚本处理的结果：

```powershell
.\build\search.exe multi king queen run `
    --tau 0.50 `
    --index data\index `
    --stops config\stopwords.txt `
    --csv
```

CSV 无表头，字段依次为：模式、查询文本、阈值、返回文档数、以分号分隔的文档 ID、查询耗时（秒）。短语查询的阈值字段为空。索引加载日志输出到标准错误，CSV 输出到标准输出；查询耗时不包含索引加载时间。

## 测试与复现

### 预处理及倒排索引正确性

原始网页位于 `data/raw/shakespeare/`。运行完整流程：

```powershell
.\run_A.cmd
```

该入口调用 `scripts/run_A.ps1`，编译相关程序，执行预处理、停用词策略实验、样例 tokens 和索引比对、B 部分接口测试，以及正式语料的索引保存、重载和词位核对。日志和统计保存在 `results/preprocess/`。

该流程将默认策略的验证索引保存到 `build/index_balanced/`，不会更新 `data/index/`。若要用最新预处理结果运行下方阈值测试，需再执行上文的主程序编译和正式索引构建命令。

独立验证与词汇规模实验可另行运行（需先完成上述流程）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/verify_A.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/benchmark_A.ps1
```

### 查询阈值实验

确保 `build/search.exe` 和正式索引 `data/index/` 已生成，然后运行：

```powershell
.\tests\query\run_threshold_tests.ps1
```

共 24 组测试，使用 `config/stopwords.txt`：

- 单词 `king`、`love`、`run`，分别测试 `t=1、2、5、10、20`。
- 多词 `king queen crown`、`love death life`、`run walk speak`，分别测试 `tau=0.30、0.50、1.00`。
- 检查文档数量是否符合当前语料基准、文档 ID 是否重复，以及高阈值结果是否为低阈值结果的子集。

结果写入 `results/query/threshold_results.csv`，包含查询、阈值、返回数量、完整文档 ID 和查询耗时。基准数量对应当前 912 篇文档及默认停用词策略；更换语料或策略后应重新核对。

## 进一步阅读

- [A 部分使用说明](README_A.md)
- [接口与交接说明](docs/A_接口与交接说明.md)
- [A 部分实现与复现步骤](docs/A_实现与复现步骤.md)
- [索引接口](includes/index/AdsIndex.h)
- [B 部分报告](report/report_B.md)
- [A 部分参考资料与第三方来源](REFERENCES_A.md)
