# Fcitx5 插件

C++20 插件接入 Rust ABI、libime 拼音、五笔、候选窗口、热键及后台加载与语音。公开接口位于 include/cowtype2.h。构建与原生探针见 [BUILD](../docs/BUILD.md)。

拼音通过系统、万象、雾凇三层压缩词典和可选本地统计模型解码；权重、缓存格式和离线编译见[词库说明](../docs/DICTIONARIES.md)。

`gb18030/` 提供系统 iconv 内码转换，`engine/shortcuts.h` 集中派发功能快捷键，配置注册表为 `src/config/shortcuts.tsv`，见[组件说明](gb18030/README.md)。
