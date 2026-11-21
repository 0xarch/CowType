# 项目结构

| 路径 | 用途 |
|---|---|
| build-arch.sh | Arch x86_64 一键准备、生产构建与打包入口；安装包输出到 dist/ |
| src/ | Rust 本体：词典、候选、组句、会话、学习、配置、i18n、C ABI 和 CLI |
| src-fcitx5/ | C++ Fcitx5 插件、libime 适配、候选显示、热键、配置和诊断；include/cowtype2.h 定义 ABI |
| src-settings/ | Python GTK4/libadwaita 设置程序、配置读写、启动器、桌面入口和文案读取 |
| src-voice/ | Rust 语音宿主、PulseAudio/PipeWire-Pulse 捕获、音频计量、会话与 IPC；paraformer/ 含 ONNX 后端、准备脚本、许可和测试 |
| src-data-tools/ | 公开词典的获取、转换、安装、移除与来源校验；manifest/sources.json 锁定版本和许可 |
| src-model-tools/ | text-models/ 提供可选自由文字模型下载及校验；尚未接入文字候选重排 |
| src-translate/ | 翻译功能预留，当前未实现 |
| assets/icons/ | SVG 源图标及 hicolor 多尺寸 PNG |
| i18n/ | en_US.tsv、zh_CN.tsv 统一界面文案 |
| tests/ | Rust/Python 单元和集成测试、合成词典、原生 Fcitx5/C ABI 探针 |
| tooling/ | 文案编译、源码清单检查和 Arch 源码打包工具 |
| packaging/ | 发行版包模板；当前提供 Arch PKGBUILD.in |
| docs/ | 使用、构建、版权与来源说明 |
| LICENSES/ | 第三方许可文本；各组件还保留各自上游通知 |

根 Cargo.toml 构建本体，src-voice/Cargo.toml 构建语音宿主；CMakeLists.txt 构建和安装 Fcitx5、设置、数据工具与语音程序。运行时代码在 src/ 或 src-*/；维护脚本在 tooling/，生成物统一放 build/、target/、dist/。模型、日志、个人配置和用户诊断附件不作为项目源码。
