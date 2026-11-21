<div align="center">

<img src="assets/icons/hicolor/scalable/apps/cowtype.svg" width="96" alt="CowType 图标">

# CowType

CowType(牛子输入法) 是基于 Fcitx5 的自由的 Linux 拼音输入法，面向所有需求的用户。
CowType 主体受 WowType 启发，并旨在用完全自由的代码消除 WowType 逆向破解微信输入法[^audit]带来的项目持续性与法律风险的同时，提供对等或更优的功能性。

支持拼音、五笔、码表、语音输入。除获取资源外，日常使用的所有数据完全离线处理。

</div>

## 中文支持

基于 libime 并支持以下特性：

1. 同时支持全拼简拼输入和拼音预测
2. 支持超长句输入和行内修改、部分上屏
3. 支持邻键纠错(如v<->b, i<->u等)
4. 支持顺序纠错(缺失、重复等)
4. 支持动态学习高频词语并优先展示
5. 支持经典五笔 86 输入
6. 基于 Unicode CLDR 的表情和符号关键词匹配
7. 更顺手的长词组-词组-单字排列方式
8. 通过统计模型提高长句预测准确性

纠错模型基本覆盖大部分使用场景，主观体验与 WowType 基本相同，多重错误纠错能力略弱于 WowType。

尚不支持 WowType 的互译功能。

拼音优先使用本机 Fcitx/libime 系统词库，并以雾凇词库补充；需要更多词汇时，可在设置里下载万象文本词库。
支持词库预编译缓存，详见[词库说明](docs/DICTIONARIES.md)。五笔使用五笔词库。

本地统计语言模型默认关闭。开启后长句准确率可达 90%/50词 以上。
> 准确率受语句复杂度等多因素影响，实际使用时可能无法达到预期效果。

## 高级特性

支持输入 `u` 后自动接 Unicode 码自动匹配码表。
可通过快捷键显式进入 Unicode 码表模式（默认关闭）。

支持 GB18030-2022 码表输入，默认按 `F7` 进入 GB18030 码表模式。

## 语音输入

基于 ONNX + Paraformer 提供低占用高质量语音输入体验。

支持点按和长按快捷键触发语音输入。语音识别完全使用本地模型完成，支持配置自定义模型。

语音默认关闭，模型不随安装包附带。
设置页面可一键下载并启用阿里达摩院 `paraformer-zh-streaming` 模型(开放授权)，自备模型步骤见[语音模型说明](src-voice/paraformer/README.md)。

## GUI 设置

[![Please do not theme this app](https://stopthemingmy.app/badge.svg)](https://stopthemingmy.app)

打开 CowType 设置或运行 `cowtype-config` 即可调整输入、按键、语音和词库选项。

- 用 F6 或单按 Shift 切换中英文。
- 用数字键选词，PageUp/PageDown 翻页；也可开启方括号、减号和等号翻页。
- 自选语音快捷键、长按方式，以及是否保留语音实例。
- 下载或移除可选词库，按需增加词汇。
- 支持横竖排列。

## 开始使用

### 预编译包

GitHub Actions 已验证以下 Linux x86_64 目标的构建、安装包和插件测试：

| 发行版 | 安装包 |
|---|---|
| Arch Linux | `.pkg.tar.zst` |
| Fedora 45 | `.rpm` |
| openSUSE Tumbleweed | `.rpm` |
| Debian unstable | `.deb` |

在 [Releases](https://github.com/0xarch/CowType/releases) 下载对应的发布包，
或在 [Actions](https://github.com/0xarch/CowType/actions/workflows/linux-packages.yml) 中打开成功的运行，从 **Artifacts** 下载（下载 Actions 产物需要登录 GitHub）定期主线构建。
注：主线构建不保证总是新于发布包。

发布包均经过实际测试，确保基本功能正常。主线构建可能在部分情况下存在问题。

请使用本发行版的软件包管理器安装对应的包。Debian unstable 和 openSUSE Tumbleweed 都是滚动目标，安装包面向各自的原生依赖；Deb 包不保证兼容 Debian 稳定版或 Ubuntu，Tumbleweed 的 RPM 不保证兼容 Leap 或 Fedora。

由于 Ubuntu 26.04 的 Fcitx5 版本过于老旧，Ubuntu 目前不在自动构建范围内，版本要求与自行构建方式见[Ubuntu 构建说明](docs/BUILD.md#ubuntu)。

Deepin 23、25 和 openSUSE Leap 16.0 的官方容器也因 Fcitx5 版本不足暂不提供预编译包，实测版本与自行构建要求见[构建说明](docs/BUILD.md#deepin-和-opensuse-leap)。

### Arch Linux

除了预编译包以外， Arch Linux 用户可以直接从本仓库构建最新代码。

先安装依赖，再下载项目并运行根目录脚本：

```sh
sudo pacman -S --needed base-devel git cmake ninja rust cargo fcitx5 libime boost openssl librsvg gtk4 libadwaita python python-gobject
git clone https://github.com/0xarch/CowType.git
cd CowType
bash build-arch.sh --check
bash build-arch.sh
```

脚本下载并校验默认词库和编译依赖，完成生产构建后将安装包放在 `dist/`。请用普通用户运行，首次构建需要网络；不会下载语音模型或自动安装软件。生产构建不编译或运行开发测试，开发者可按[构建与打包说明](docs/BUILD.md)单独运行完整测试。

成功后按实际生成的包名使用 `sudo pacman -U` 安装。Fedora、Debian 的原生打包流程和自动发布规则见[打包说明](packaging/README.md)；其他环境的依赖、手动构建与高级选项见[构建文档](docs/BUILD.md)。

安装或升级完成后需重新启动 Fcitx5 以加载插件。打开应用菜单中的 **CowType 设置**，或运行 `cowtype-config` 调整偏好。

## 参与

欢迎[提交 Issue](https://github.com/0xarch/CowType/issues)。反馈时附带系统、桌面环境和复现步骤；
分享日志前请检查是否包含个人信息。

参与开发可从[项目结构](STRUCTURES.md)和[构建与打包说明](docs/BUILD.md)开始。

## 致谢

- [Fcitx5](https://github.com/fcitx/fcitx5)：本项目依赖的输入法框架
- [libime](https://github.com/fcitx/libime)：本项目依赖的拼音系统
- [雾凇拼音](https://github.com/iDvel/rime-ice)：词库和英文词表
- [Rime 五笔字型](https://github.com/rime/rime-wubi)：五笔 86 码表
- [万象拼音](https://github.com/amzxyz/rime_wanxiang)：可选词库
- [Unicode CLDR](https://cldr.unicode.org/)：emoji 的中英文关键词
- [FunASR](https://github.com/modelscope/FunASR)：当前 Paraformer 实现参考
- [fcitx5-gb18030](https://github.com/openkylin/fcitx5-gb18030)：GB18030 内码输入实现参考
- [Zed](https://zed.dev)：代码编辑器
- [WowType](https://github.com/Gloridust/WowType)：灵感来源

## 许可证

本项目整体按 GPL3.0 许可证发布。

详细的代码、图标、数据与模型各自的许可和署名见[版权与第三方归属](docs/ATTRIBUTION.md)。

[^audit]: 粗略审计报告可参见 <https://0xarch.github.io/posts/wowtype-audit>
