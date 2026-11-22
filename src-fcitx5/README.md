# Fcitx5 插件

C++20 插件接入 Rust ABI、libime 拼音、五笔、候选窗口、热键及后台加载与语音。公开接口位于 include/cowtype2.h。构建与原生探针见 [BUILD](../docs/BUILD.md)。

拼音通过可选系统词库、万象、雾凇三层压缩词典（系统默认关闭）和可选本地统计模型解码。`engine/libime.h` 维护有/无纠错的纯查询工作区，选项相同时复用追加和退格的图状态；按完整消费、纠错标记、精确读音及来源组织候选，纠错或多词最佳路径触发完整字面句回补（模式切换清空图）。输入框组合和选择由 Rust 会话隔离，工作区不学习或继承上文。权重、排序、缓存格式和离线编译见[词库说明](../docs/DICTIONARIES.md)。

`gb18030/` 提供系统 iconv 内码转换，`engine/shortcuts.h` 集中派发功能快捷键，配置注册表为 `src/config/shortcuts.tsv`，见[组件说明](gb18030/README.md)。

`unicode/` 是独立的 Unicode 标量输入模块，拥有独立模式和快捷键，不依赖 GB18030 或 iconv，见[Unicode 说明](unicode/README.md)。

`engine/translate.h` 通过非阻塞私有管道启动单次翻译子进程，将 HTTP 和 Secret Service 操作隔离在 Fcitx5 事件循环之外。`TranslateHotkey` 使用同一快捷键注册表；按字段保留输入、请求与候选状态，确认后提交，并在取消、失焦和配置变更时丢弃结果，见[互译说明](../src-translate/README.md)。

`compat/paths.h` 集中封装 Fcitx5 的目录接口差异：CMake 根据构建环境版本选择，>= 5.1.19 使用 `StandardPaths`，5.1.11–5.1.18 使用 `StandardPath`，返回统一的 `std::filesystem::path`。原生探针共享此封装并继续拒绝非隔离目录；不通过绕过隔离测试来兼容旧系统。插件运行时最低要求为 5.1.11，依赖中声明同一基线；按各发行版原生库分别构建，不承诺新系统产物可在旧系统直接安装。

`compat/strings.h` 封装系统词典文本的转义字段解析。>= 5.1.20 使用上游 tokenizer；更早版本使用本项目的边界扫描配合旧版已有的 `unescapeForValue`，保留引号、空格、反斜线及换行转义。此 API 的实际引入版本为 5.1.20，因此与目录 API 的 5.1.19 分界分别管理。`fcitx-api-compatibility` 测试检查新旧分支解析相同的合成字段。

`compat/addon.h` 按上游宏是否存在选择新版带插件名的工厂导出或旧版原始导出，原生探针使用同一符号选择；插件发现测试仍通过 Fcitx5 自己的加载器验证。
