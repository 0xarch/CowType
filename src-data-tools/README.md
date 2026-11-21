# 数据工具

标准库 Python 工具取得锁定公开数据、校验并转换词典，不执行上游脚本。manifest/sources.json 记录版本、许可、作者和摘要。默认雾凇与五笔，万象由用户选择。构建入口见 [BUILD](../docs/BUILD.md)。

`dictionaries/compile.cpp` 提供原生离线 `cowtype-dictionary-compiler`，将已准备的 TSV 与系统词库合并为 libime 压缩缓存，不下载数据或模型。见[词库说明](../docs/DICTIONARIES.md)。

`emoji/` 维护随源码提供的 Unicode CLDR 表情/符号关键词索引及离线转换工具，来源和重现步骤见[组件说明](emoji/README.md)。
