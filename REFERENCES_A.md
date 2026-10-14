# A 部分参考资料与第三方代码

1. The Complete Works of William Shakespeare. MIT 网站。https://shakespeare.mit.edu/ 。本项目按该站点列出的戏剧及诗歌范围处理语料。
2. TheMITTech/shakespeare. https://github.com/TheMITTech/shakespeare 。固定版本：https://github.com/TheMITTech/shakespeare/tree/6b82db852c7322dd33e95db347f0ddfc812c6409 。原始下载日期 2026-10-09，文档整理日期 2026-10-14，时区 Asia/Shanghai。已保留正式文档及目录页的原始字节和 SHA256。
3. Porter, M. F. (1980). An algorithm for suffix stripping. Program, 14(3), 130–137. 作者的算法分发页面：https://tartarus.org/martin/PorterStemmer/ 。用于解释英语词干化和实现版本来源。
4. Martin Porter. Thread-safe ANSI C implementation, Release 4, 25 March 2014. https://tartarus.org/martin/PorterStemmer/c_thread_safe.txt 。于 2026-10-14 获取。作者分发页说明这些实现可免费用于任何目的；本项目保留原始作者注释，不另行宣称其为 MIT 或其他 SPDX 许可证。
5. Martin Porter. Official test vocabulary and expected stems. https://tartarus.org/martin/PorterStemmer/voc.txt ，https://tartarus.org/martin/PorterStemmer/output.txt 。共 23,531 对，作为词干化回归基准。

`third_party/porter/porter_original.c` 为下载的完整作者版本。`porter.c` 仅保留作者标注的 stemmer definition 模块部分，移除后面的命令行演示程序；算法本身未改写。`porter.h` 提供三个原有函数声明。编译时仅对该第三方模块关闭原有紧凑写法产生的 parentheses 与 misleading-indentation 警告；本项目 C 源码按 `-Wall -Wextra -Wpedantic -Werror` 编译。

停用词候选由本项目人工审查功能词后形成；最终停用词必须同时满足本语料实测 TF/DF 阈值，没有下载通用停用词列表代替语料分析。来源选择脚本、HTML 扫描器、分词和词频统计、阈值选择、封装接口、测试与接入程序由本项目编写。
