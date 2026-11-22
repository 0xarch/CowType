# CLDR 表情与符号数据

`keywords.tsv` 是随源码维护的离线关键词索引，来源为 Unicode CLDR 48 的简体中文、英文 annotations 和 annotationsDerived。Unicode Emoji 17.0 的 emoji-test 数据用于恢复 CLDR 去掉的 U+FE0F，以及保留完整的 ZWJ、肤色等序列。未出现在 emoji-test 的普通符号按 CLDR 原字符保留。

来源版本、URL、大小和 SHA-256 锁定在 `sources.json`；数据遵循 Unicode-3.0，真实署名和许可见根目录 `LICENSES/Unicode-3.0.txt`。转换程序为 CowType 原创，未使用音译模型或上游可执行代码。

重新准备时，按 sources.json 的相对路径将原始文件放入独立目录，再执行：

```sh
python3 src-data-tools/emoji/prepare.py --source-dir build/review/cldr
python3 src-data-tools/emoji/prepare.py --source-dir build/review/cldr --check
```

程序在转换前校验全部输入摘要；不会联网。正常构建直接使用随源码提供的索引，无需重新下载。关键词统一小写、忽略 ASCII 空白和标点；同一关键词下，完整名称优先于普通关键词，去重并确定排序。表情/符号序列不做这种文本归一化。

运行时在 Rust 会话层补充候选，因此原生 libime 与五笔都能复用。索引只在启用后首次查询时建立，全进程共享；关闭时不查询索引。最多补充 12 个表情/符号家族，以完整中文候选或完整英文关键词匹配，不通过前缀或纠错猜测扩展。表情选词保留原输入消费长度，不作为个人词条学习。

`aliases.tsv` 是 CowType 自行维护的常用方向符号别名，单独记录作者、许可与摘要，不冒充 CLDR 原始数据；例如“右”“右箭头”“向右箭头”优先匹配 →。重新生成同时读取此文件。

运行时先把只相差肤色修饰符的序列合并为一个表情族，再分配 12 项总名额并去重，不允许肤色变体先占满额度。不同完整关键词轮流分配名额，符号仍紧随对应词语。

`EmojiSkinTonePicker` 默认开启：选择表情族后展示实际存在的肤色序列，再确认上屏；Esc/退格返回原候选，输入正文不提前消费。关闭后按 `EmojiDefaultSkinTone`（0–5，默认 0，依次为 👍👍🏻👍🏼👍🏽👍🏾👍🏿）直接展示和提交。默认肤色也用于选择开启时的代表项和初始光标。肤色家族从固定版本 emoji-test 的官方名称和真实序列生成到 `tone-families.tsv`，只去除名称中的肤色限定词来关联同族，不删除序列中的 ZWJ 或变体选择符；握手等单字符/ZWJ 编码不同的样式也合为一项。多人混合肤色按实际有效序列分页，不虚构或删减组合。若某种默认肤色没有对应序列，回退到该族首个有效序列。表情及肤色选择均不进入个人词条学习。
