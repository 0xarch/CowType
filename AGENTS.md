# CowType 开发规范

CowType 是 Linux Fcitx5 输入法，包含 Rust 核心、C++ 平台插件、GTK4/libadwaita 设置和本地 Paraformer 语音。

具体内容阅读以下文件：
- README.md：项目介绍
- STRUCTURES.md：代码结构
- docs/BUILD.md：构建步骤
- 各组件自己的README：也包含了简单介绍

开发规范：
- 本体代码在 src/，设置在 src-settings/；Fcitx5、语音、数据工具、模型工具分别位于 src-fcitx5/、src-voice/、src-data-tools/、src-model-tools/。新组件使用 src-<component>/，先明确职责与依赖。平台 UI 不进入核心库。
- 图标在 assets/icons/；Paraformer 在 src-voice/paraformer/；文字模型准备工具在 src-model-tools/text-models/。它们按普通项目代码维护，保留必要上游许可与通知。
- UI 文案统一在 i18n/en_US.tsv 和 zh_CN.tsv；诊断用英文，不记录按键、输入正文、识别结果、个人词条或密钥。
- 仅支持 Linux。模型由用户显式下载和选择，不随源码或安装包分发；基本输入不依赖语音、翻译或文字增强模型。可选后端失败应可诊断，不隐式切换设备或远端服务。
- 外部代码、数据和模型需记录来源、版本、许可及必要摘要，保留真实署名。来源见 docs/SOURCES.json、docs/PUBLIC-SOURCES.json 和词库 manifest；修改功能时同步更新相关说明。
- 不提交模型、个人配置/日志、缓存或构建产物；测试使用临时 XDG 目录，避免修改用户桌面、词典或音频设备。
- 按改动运行 Rust、Python 或 CMake/CTest 检查。测试与源码清单命令见 docs/BUILD.md；打包前更新并校验 tooling/SOURCE-MANIFEST.json。不得将 mock、编译通过写成真实麦克风或完整桌面验收。
- 不擅自改写 Git 历史；每个功能或修复使用独立提交；推送和发布遵循用户当前授权。
- 同一轮多个任务，除非用户要求，否则按顺序进行。

发布流程：
1. 更改版本号及相关内容、CHANGELOG
2. 推送新版本号并触发 GitHub Actions
3. GitHub Actions 全部构建通过后收集各发行版构建产物
4. 发布 Tag 和 Release ，描述文本附带 CHANGELOG ， Release Assets 附带各发行版构建产物
