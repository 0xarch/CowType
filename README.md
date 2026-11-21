<div align="center">

<img src="assets/icons/hicolor/scalable/apps/cowtype.svg" width="96" alt="CowType 图标">

# CowType

CowType是基于 Fcitx5 的 Linux 拼音输入法。

支持拼音、五笔、语音输入。设计灵感来自 WowType ，后者则来自微信输入法。

</div>

## 中文支持

基于 libime 并支持以下特性：

1. 同时支持全拼简拼输入
2. 支持超长句输入和行内修改
3. 支持自动纠错字母(如v<->b, i<->u等)
4. 智能学习高频输入词语并优先展示
5. 支持经典五笔 86 输入

默认使用雾凇拼音和五笔词库，需要更多词汇时，还可以在设置里下载万象文本词库。
不支持云拼音。

## 语音输入

支持点按和长按快捷键触发语音输入。语音识别完全使用本地模型完成，支持配置自定义模型。

语音默认关闭，模型不随安装包附带。具体准备步骤见[语音模型说明](src-voice/paraformer/README.md)。

## GUI 设置

打开 CowType 设置，即可调整输入、按键、语音和词库选项。

- 用 F6 或单按 Shift 切换中英文。
- 用数字键选词，PageUp/PageDown 翻页；也可开启方括号、减号和等号翻页。
- 自选语音快捷键、长按方式，以及是否保留语音实例。
- 下载或移除可选词库，按需增加词汇。

## 开始使用

目前支持在 Arch Linux x86_64 上打包。先安装依赖，再下载项目并运行根目录脚本：

```sh
sudo pacman -S --needed base-devel git cmake ninja rust cargo fcitx5 libime boost gtk4 libadwaita python python-gobject
git clone https://github.com/0xarch/CowType.git
cd CowType
bash build-arch.sh
```

脚本会下载默认词库和编译依赖，完成编译与测试后，将安装包放在 `dist/`。请用普通用户运行，首次构建需要网络；不会下载语音模型或自动安装软件。

成功后，按实际生成的文件名安装。

常见问题和手动构建步骤和高级控制见[构建与打包说明](docs/BUILD.md)。

安装完成后需重新启动 Fcitx5 以加载插件。

打开应用菜单中的 **CowType 设置**，或运行 `cowtype-config` 调整偏好。

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
- [Zed](https://zed.dev)：代码编辑器
- [WowType](https://github.com/Gloridust/WowType)：灵感来源

## 许可证

本项目整体按 GPL3.0 许可证发布。

详细的代码、图标、数据与模型各自的许可和署名见[版权与第三方归属](docs/ATTRIBUTION.md)。
