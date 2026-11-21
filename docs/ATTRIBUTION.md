# 版权、许可与第三方归属

CowType 自身实现使用 GPL-3.0 许可。

CowType 图标：本项目使用经过修改的 WowType 图标。原图标来自 [WowType](https://github.com/Gloridust/WowType)，由原项目按 MIT or Apache-2.0 分发而在本项目中使用。MIT 和 Apache 2.0 许可证文本见 `LICENSES/`。
- Rust Paraformer 前端和流式编排参考 FunASR 公开 MIT 实现，文件标识、版权和原许可保留在 `src-voice/paraformer/runtime/NOTICE.md` 与 `LICENSES/LICENSE.FunASR`。此许可不代表所有 FunASR 模型许可。
- 可选 Qwen3 文字模型的官方 Apache-2.0 许可保留在 `LICENSES/LICENSE.Qwen3`；模型不随项目分发，来源及摘要见 `src-model-tools/text-models/catalog.json`。
- rime-ice、rime-wubi、可选万象数据各自保留许可、作者、Credits 与输入文件摘要，见 `src-data-tools/manifest/sources.json` 和每份生成数据的 `source/`、`manifest.json`。程序许可不能代替数据许可。
- libime、Fcitx5、GTK/libadwaita、Boost、ONNX Runtime 及 Rust/Python 依赖分别适用上游许可，发布时保留所需通知。公开参考版本与语音依赖记录见 SOURCES.json。

PUBLIC-SOURCES.json 记录部分公开资源的核实范围和限制。新增第三方实现、数据或模型均保留其真实来源与适用许可。
