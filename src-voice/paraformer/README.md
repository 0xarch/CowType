# Paraformer ONNX 后端与模型准备

runtime/ 包含 Rust 特征前端、模型加载和流式识别，通过相邻语音宿主集成。官方 FunASR 参考、版权和 MIT 许可见 runtime/NOTICE.md 与 LICENSE.FunASR。专属错误消息在 messages.json，宿主统一文案在根 i18n/。

模型由用户显式获取，发行包不含权重。准备入口需要 Linux、Python 3.10+ 标准库、HTTPS CA 证书、网络和可写数据目录；自动环境准备使用 uv 或 venv/ensurepip，导出需要兼容 Python、CPU PyTorch、FunASR、ONNX 等，脚本会预检和安装隔离环境依赖。系统 ONNX Runtime 单独安装；NixOS 的导出轮子可能需要动态链接支持。

```sh
python3 src-voice/paraformer/download_paraformer.py --help
python3 src-voice/paraformer/download_paraformer.py --check
python3 src-voice/paraformer/download_paraformer.py
```

默认按顺序准备环境、依赖、下载、导出并选择模型；可用 --disable-download、--disable-export、--disable-select、--disable-* 关闭步骤。ModelScope 为默认源，失败回退到固定 Hugging Face 快照。原始 .pt/.pth 是导出输入，必须使用可信权重，不能直接交给 ONNX Runtime 或改名成 ONNX。当前是 encoder/decoder 双图接口，配置、词表、CMVN 与外部张量需配套。

ModelScope 固定模型卡声明 Apache-2.0、无独立 LICENSE 文件；不能用 FunASR 的 MIT 代码许可解释权重许可。详细来源及核实限制见 docs/PUBLIC-SOURCES.json，文件版本和摘要以下载器为准。准备成功不等于完成真实设备或识别准确率验收。
