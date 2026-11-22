# 互译

独立 Python 组件，通过 OpenAI 兼容的 `POST /chat/completions` 接口执行文本翻译。Fcitx5 插件使用私有管道启动 `cowtype-translate` 单次进程，网络和密钥环操作不进入输入法事件循环，也不进入 Rust 基本输入核心。只使用 Python 标准库发送请求；可选密钥环通过已有 PyGObject 调用 libsecret。

## 服务与语言

设置“互译”页启用功能，默认关闭，统一快捷键默认为 F10。可选服务：

- **Bilibili 翻译**：`https://index-translate.bilibili.com/v1`，可选模型 `Index-Translate-2B`、`Index-Translate-9B`、`Index-Translate-35B-A3B`（默认），公共服务不需要 Key。根据官方示例使用单条 user 消息，并传 `chat_template_kwargs.enable_thinking=false`。
- **自定义 OpenAI 兼容接口**：在设置中填写基础地址（例如 `https://example.com/v1`）或完整 `chat/completions` 地址及模型名称。使用标准消息、非流式返回和 Bearer Key；不添加 Bilibili 专属字段。

默认让模型自动判断来源语言：中文译为英语，其他语言译为简体中文。也可固定译成中文、英语、日语、韩语、法语、德语、西班牙语、俄语、葡萄牙语、意大利语、阿拉伯语、越南语、泰语或印度尼西亚语。固定目标时来源语言同样由模型判别。不会自动检测后换服务，也不会失败后改用其他接口。

地址必须为 HTTPS；本机回环地址可使用 HTTP，便于本地兼容服务。拒绝 URL 账号密码、查询参数和片段，Key 仅放在 Authorization 请求头。禁用 HTTP 重定向，不自动重试。

## 输入交互

- 有拼音/五笔组合时，F10 翻译当前高亮候选；已选部分和未消耗的原始后缀一并保留。预览不提交或学习，Esc 恢复组合。
- 没有组合、但应用通过 Fcitx5 surrounding text 提供非空选区时，只翻译选区。确认前重新核对选区内容及位置；选区变化则拒绝提交。应用不提供选区时不会读取剪贴板或整段周边文本。
- 都没有时，进入翻译输入模式。中文模式可继续拼音/五笔选词，选中的文字暂存而不上屏；先切换到英文模式再进入，可直接输入含空格的英文。Enter 发送。选择固定目标语言后同样适用。
- 请求中不自动上屏。译文作为一个候选显示，1/空格/回车或鼠标确认后提交，Esc 或再次按翻译快捷键取消。等待或显示结果时普通编辑键取消翻译并恢复原组合；无原组合的翻译草稿取消后丢弃。
- 失焦、输入法切换、重置、配置重载、进入敏感输入框或切换其他功能时取消请求并丢弃迟到回复。其他输入框不会收到此请求的结果。

翻译结果不写入选词学习；输入模式中用户主动选择的原始拼音/五笔候选沿用普通选词行为。翻译快捷键与中英文、语音、GB18030、Unicode 共用同一选项范围和重叠检测。旧配置若已占用 F10，新增的翻译绑定迁移为禁用，不覆盖既有绑定。

## 设置与 Key

启用开关及 `TranslateHotkey` 在 `settings-v1.ini`，非敏感服务设置在 `$XDG_CONFIG_HOME/cowtype/translate-v1.json`，权限 0600。服务配置需显式点击“保存翻译配置”，不会把 Key 写入此文件。

自定义接口的 Key 来源可选：

1. 不使用 Key，适合不需要鉴权的本地服务。
2. **环境变量 `COWTYPE_TRANSLATE_KEY`**：优先读取当前进程环境；从桌面菜单打开设置、未继承变量时，通过会话 D-Bus 定位当前用户的 Fcitx5，只读取其中的同名 Key。查询前后核对服务归属，不扫描其他进程，也不写入配置文件。修改终端变量不会自动改变已经运行的 Fcitx5 环境；若两处都没有 Key，需从已导出该变量的终端启动设置/Fcitx5，或使用密钥环。
3. **桌面密钥环**：在设置的密码输入框填写 Key，通过 libsecret 的 Secret Service 接口保存到桌面默认集合；加密存储由桌面密钥环管理。需要可用、已解锁的 Secret Service 提供者（例如 GNOME Keyring 或支持该接口的 KWallet）及 Secret typelib。无可用密钥环时直接报错，不回退到明文文件或自制加密。Key 与 API 完整端点摘要绑定，修改接口后不会把旧服务的 Key 发给新服务。留空保留该接口已存 Key，也可单独删除。

密钥环依赖：Arch 为 `libsecret`；Debian 为 `gir1.2-secret-1`；Fedora 为 `libsecret`；openSUSE 为 `typelib-1_0-Secret-1`。这些是可选依赖，环境变量模式不需要它们。

运行时不读取 `COWTYPE_TRANSLATE_API` 或 `COWTYPE_TRANSLATE_MODEL`。开发者可在临时 XDG 目录把这两个测试变量显式写入临时服务配置，再调用后端验证；不修改用户配置，不保存测试 Key。设置的“测试接口”按钮会显式发送固定测试句 `Hello, world!`，只显示成功或错误，不展示或保存结果。

请求仅在快捷键发送或显式点击测试按钮时发出。输入与输出不写入日志、缓存、命令行参数或学习词库。源文最大 32 KiB、响应最大 256 KiB、译文最大 64 KiB；截断结果、控制字符和 thinking 内容会报错。HTTP 超时 25 秒，插件总截止时间 35 秒，取消时结束子进程。

## 来源与验证

API 参考：[Bilibili Index-Translate 官方调用示例](https://github.com/bilibili/Index-Translate/blob/5bc437dffea3a7605604e70e6a459c44e6fea19f/inference/llm/call_api.py)，项目许可证 Apache-2.0；此处仅参考公开协议和参数，不复制或分发其脚本、模型或权重。CowType 实现为原创 GPL-3.0-or-later，具体记录见 [SOURCES](../docs/SOURCES.json)。

`tests/test_translate.py` 使用本机合成 HTTP 服务和内存密钥环替身，测试请求、重定向、错误脱敏、凭据绑定及配置；`tests/native/translate.cpp` 使用假翻译子进程覆盖异步候选、选区和焦点生命周期。CI 不调用公共翻译服务、不读取真实 Key 或桌面密钥环。2026-11-22 已单独验证 Bilibili 公共服务能返回译文；当前提供的自定义测试配置返回 HTTP 503，其指定模型未出现在接口返回的模型列表中，未将其记为真实翻译成功，也未自动替换模型。没有进行完整桌面应用或真实密钥环存储验收。
