# 项目结构

| 路径 | 用途 |
|---|---|
| build-arch.sh | Arch x86_64 一键准备、生产构建与打包入口；安装包输出到 dist/ |
| src/ | Rust 本体：词典、候选、CLDR 表情符号匹配、组句、会话、学习、配置、i18n、C ABI 和 CLI |
| src-fcitx5/ | C++ Fcitx5 插件、libime 适配、候选显示、热键、配置和诊断；gb18030/ 和 unicode/ 提供独立码表输入，engine/shortcuts.h 派发统一功能快捷键；compat/ 集中处理不同 Fcitx5 版本的目录、转义解析及工厂导出；include/cowtype2.h 定义 ABI |
| src-settings/ | Python GTK4/libadwaita 设置程序、学习记忆查看与编辑、配置读写、启动器、桌面入口和文案读取 |
| src-voice/ | Rust 语音宿主、PulseAudio/PipeWire-Pulse 捕获、音频计量、会话与 IPC；paraformer/ 含 ONNX 后端、准备脚本、许可和测试 |
| src-data-tools/ | 公开词典的获取、转换、安装、移除与来源校验；manifest/sources.json 锁定词库版本和许可；emoji/ 维护 CLDR 关键词转换与来源 |
| src-model-tools/ | text-models/ 提供可选自由文字模型下载及校验；尚未接入文字候选重排 |
| src-translate/ | 可选 OpenAI 兼容互译：HTTP 客户端、独立服务配置、Secret Service Key 存储与单次私有管道进程 |
| src-scripts/ | Lua 候选桥接和日期/时间示例，通过 Fcitx imeapi 运行；脚本配置、索引和 GTK 管理页分别位于 src-settings/config/scripts.py、app/scripts.py，原生宿主为 engine/scripts.h |
| assets/icons/ | 只保留 scalable SVG；hicolor 多尺寸 PNG 在构建目录生成 |
| i18n/ | en_US.tsv、zh_CN.tsv 统一界面文案 |
| tests/ | Rust/Python 单元和集成测试、合成词典、原生 Fcitx5/C ABI 探针 |
| tooling/ | 文案编译、源码清单检查和 Arch 源码打包工具 |
| packaging/ | Arch、Debian 和 RPM 原生包模板 |
| .github/workflows/ | Linux 容器构建、测试、原生包与标签发布 |
| docs/ | 使用、构建、版权与来源说明 |
| LICENSES/ | 第三方许可文本；各组件还保留各自上游通知 |

根 Cargo.toml 构建本体，src-voice/Cargo.toml 构建语音宿主；CMakeLists.txt 构建和安装 Fcitx5、设置、数据工具与语音程序。运行时代码在 src/ 或 src-*/；维护脚本在 tooling/，生成物统一放 build/、target/、dist/。模型、日志、个人配置和用户诊断附件不作为项目源码。

学习记忆：`src/learning/edits.rs` 提供带版本冲突检查的私有编辑队列及单写者锁，`service.rs` 统一保存学习和设置修改；`src-settings/config/memory.py` 处理快照与请求，`app/memory.py` 提供分页编辑界面。

自定义词库：`src/custom/` 提供完整字母触发索引及后台文件更新，`src-settings/config/custom.py` 原子保存词条，`app/custom.py` 提供自动保存的分页编辑器。独立于系统/万象预编译词库和个人学习。

`src-fcitx5/engine/spelling.h` 提供独立有界的重复键/相邻换位完整词补充，复用 libime 公开音节与 trie 接口，不持久化输入、不建立另一份词库、不引入模型。

平台控制与上屏：`engine/dbus.h` 负责用户会话总线上的版本和中英文模式控制，复用当前焦点输入框的快捷键切换路径；不将输入正文暴露到总线。`engine/autospace.h` 根据可信 surrounding text 计算汉字与英数边界，由插件的统一正文上屏路径应用，不修改核心候选和学习记录；码点输入保留精确输出。

双拼：`engine/shuangpin.h` 管理 libime 内建或用户定义的方案，`LibimeDecoder` 使用同一词库和候选解码器处理双拼原始按键。Rust 会话维护原始键范围及选择，平台设置负责方案选择和路径；方案配置不进入平台无关的词库索引。
