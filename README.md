项目结构：
- includes： 头文件， 与src一一对应
- src: 源代码， 按模块分类
- tests: 后续测试代码存放处
- data: 示例文件 sample， 测试文件test 统一往这里丢 
- config： 后续考虑一些路径配置往这里写， 现阶段应该暂时无用

后续考虑配置makefile， 但现阶段完成度暂时不需要makefile？

概念设计：
## clean 设计
1. 停用词：
   阈值定义为 "所有文档中出现比例"、 "出现次数"。允许指定排除哪些词或者不排除哪些词。即配置停用词规则




## index 设计
### 从tokens创建倒排索引：
读入 expected_tokens.tsv：
```tsv
doc_id	position	stem
1	1	king
1	2	run
1	4	walk
```
约定：
  doc_id：文档的唯一编号，不要求连续。
  position：从 0 开始，表示过滤停用词之前的词位；过滤后不重新编号。
  stem：A 已经完成规范化、词干化的词项，B 直接使用。
  同一个文档中的同一个词位只能有一条记录。
  文件使用 UTF-8，第一行为固定表头，各字段以制表符分隔。


读入 demo_stopwords.txt：
```txt
a
the
and
```
要求 一行一个 stem

再读入文档信息 documents.tsv：
```
doc_id	work_title	source_url	clean_path
1	Sample D1	sample://D1	data/sample/D1.txt
2	Sample D2	sample://D2	data/sample/D2.txt
3	Sample D3	sample://D3	data/sample/D3.txt
4	Sample D4	sample://D4	data/sample/D4.txt
```

### 读取和保存倒排索引
目前暂定读取和保存一个目录， 其下面有三个文件：
metadata.tsv ： 这个版本的索引相关信息
postings.tsv ： 索引的数据， 同一文档不同位置用逗号隔开
documents.tsv ： 文档id 和 文档名 的一一对应， 如需要可考虑加入文档链接等额外信息。