
## 项目结构：

## 构建和使用
项目构建：
```
mingw32-make main
```
或者
```
make main
```


构建方法：
```
.\build\search.exe build `
    --tokens data\tokens.tsv `
    --documents data\documents.tsv `
    --index data\index
```

| 参数          | 含义                                     |
| ------------- | ---------------------------------------- |
| `--tokens`    | 必填，输入 token TSV 文件                |
| `--documents` | 可选，文档编号、标题、来源和正文路径清单 |
| `--index`     | 必填，索引输出目录                       |
| `--stops`     | 可选，构建时额外过滤的停用词清单         |
由于tokens构建时已应用stops， stops可不用


查看索引信息：
```
.\build\search.exe info
```
单词查询， 至少出现两次， 使用停用词 
.\build\search.exe word king `
    --t 2 `
    --index data\index 
    --stops data/test/demo_stopwords.txt
```

多词查询：
```
.\build\search.exe multi king queen love `
    --tau 0.5 `
    --index data\index
```

连续短语查询：
```
.\build\search.exe phrase "to be or not to be"
```

.\build\search.exe multi king queen run `
    --tau 0.5 `
    --index data\index  `
    --stops data/test/demo_stopwords.txt `
    --csv


## 测试
倒排索引正确性测试：
```
./run_A.cmd
```



阈值测试：
```
.\tests\query\run_threshold_tests.ps1
```