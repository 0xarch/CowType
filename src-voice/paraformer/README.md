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

## 常驻内存

CPU 后端保留编码器、解码器和权重预打包，但关闭 ORT arena 与 memory pattern。流式输入形状随块变化，释放推理输出后让临时缓冲区交回系统分配器；每轮结束后继续调用已有的 `malloc_trim`。不改变模型权重、量化、线程数或识别算法，不卸载常驻模型，也不修改系统大页配置。

2026-11-21，使用本机已显式选择的双图模型（编码器约 607 MiB、解码器约 218 MiB）独立进程对照：短时静音后 RSS 从 883.4 MiB 到 881.4 MiB；5/15/5 秒多轮合成音频后，从 886.4 MiB 到 882.4 MiB，启动就绪约 16–24 ms，结束约 354–429 ms，合成输入输出一致。以上属于真实模型的合成数据测试，未验证真实语音准确率；合成输入不能覆盖所有实际识别形状或高水位。

本机用户报告的原进程 RSS 约 1.14 GiB 未在上述独立测试中复现，因此本次是保守的缓冲区保留优化，不能承诺数百 MiB 的常态收益。关闭权重预打包的对照虽降低加载峰值，但结束延迟增加，本次保留预打包。实际常驻最低占用仍主要受未量化模型权重大小限制。
