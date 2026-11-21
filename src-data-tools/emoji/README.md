# CLDR 表情与符号数据

`keywords.tsv` 是随源码维护的离线关键词索引，来源为 Unicode CLDR 48 的简体中文、英文 annotations 和 annotationsDerived。Unicode Emoji 17.0 的 emoji-test 数据用于恢复 CLDR 去掉的 U+FE0F，以及保留完整的 ZWJ、肤色等序列。未出现在 emoji-test 的普通符号按 CLDR 原字符保留。

来源版本、URL、大小和 SHA-256 锁定在 `sources.json`；数据遵循 Unicode-3.0，真实署名和许可见根目录 `LICENSES/Unicode-3.0.txt`。转换程序为 CowType 原创，未使用音译模型或上游可执行代码。

重新准备时，按 sources.json 的相对路径将原始文件放入独立目录，再执行：

```sh
python3 src-data-tools/emoji/prepare.py --source-dir build/review/cldr
python3 src-data-tools/emoji/prepare.py --source-dir build/review/cldr --check
```

程序在转换前校验全部输入摘要；不会联网。正常构建直接使用随源码提供的索引，无需重新下载。关键词统一小写、忽略 ASCII 空白和标点；同一关键词下，完整名称优先于普通关键词，去重并确定排序。表情/符号序列不做这种文本归一化。

运行时在 Rust 会话层补充候选，因此原生 libime 与五笔都能复用。索引只在启用后首次查询时建立，全进程共享；关闭时不查询索引。最多补充 12 项，以完整中文候选或完整英文关键词匹配，不通过前缀或纠错猜测扩展。表情选词保留原输入消费长度，不作为个人词条学习。
