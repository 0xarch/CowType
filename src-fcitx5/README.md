# Fcitx5 插件

C++20 插件接入 Rust ABI、libime 拼音、五笔、候选窗口、热键及后台加载与语音。公开接口位于 include/cowtype2.h。构建与原生探针见 [BUILD](../docs/BUILD.md)。

拼音通过系统、万象、雾凇三层压缩词典和可选本地统计模型解码；权重、缓存格式和离线编译见[词库说明](../docs/DICTIONARIES.md)。
