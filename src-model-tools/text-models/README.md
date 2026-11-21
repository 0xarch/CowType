# 用户选择的自由文字模型

整句增强/候选重排是可选能力，基础输入无需模型。当前提供自由模型资源获取，不实现重排器，不内置模型权重。

## 当前可下载项

`qwen3-0.6b-q8` 对应官方 `Qwen/Qwen3-0.6B-GGUF`，固定 revision 为 `32d6327dd2a5b42f7ce0fe5e6b6f25346b0ee8f9`，文件 `Qwen3-0.6B-Q8_0.gguf` 为 639446688 字节。官方 LICENSE 为 Apache-2.0，保留原文于 LICENSE.Qwen3；文件摘要、大小及来源见 catalog.json。核实的是官方 ModelScope 固定版本；Hugging Face 页面仅留官方引用，当前不假设两站 revision 相同或提供未核实回退。

这是通用 causal LM，不能保证适合输入法候选重排、在输入法中即插即用。未来需实现候选评分/重排适配，评估中文质量、延迟、内存、取消和隐私，并给用户选择是否启用。下载成功不代表新宿主支持该模型。

## 依赖与用法

准备脚本只需 **Python 3.9+ 标准库**；下载需要 HTTPS CA 证书、网络、可写目录和约 640 MB 空间。NixOS 不需要特殊 Python 二进制依赖；未来运行模型还需另行安装并适配自由运行时，如 llama.cpp，不能将“下载检查”理解成运行时检查。脚本不安装依赖、不执行模型、不启动服务、不调用输入引擎。

```sh
# 只检查本地依赖、目录计划、许可和固定清单，不联网或写入
python3 src-model-tools/text-models/download_text_model.py --check

# 用户确认许可后下载并验证 LICENSE、大小和 SHA-256
python3 src-model-tools/text-models/download_text_model.py --accept-license

# 自选新目录；已有文件摘要不匹配时拒绝覆盖
python3 src-model-tools/text-models/download_text_model.py --accept-license --output ./my-text-model

# 可选：记录供未来宿主使用的“未启用”选择，不执行推理
python3 src-model-tools/text-models/download_text_model.py --accept-license --select
```

`--disable-download` 等同 `--check`。默认输出到 `$XDG_DATA_HOME/cowtype/models/qwen3-0.6b-q8`，未设置时使用 `~/.local/share`。没有 `--accept-license` 不会开始下载。失败时清理此次暂存目录；不替换已有不同内容。成功目录包括模型、LICENSE、SOURCE.json，不自动加入 Git 或发行包。

`--select` 是显式可选操作，只在配置不存在时创建 `$XDG_CONFIG_HOME/cowtype/text-model.json`（默认 `~/.config`），权限 0600，内容为 format、enabled=false、backend、model、license、repository、revision、sha256。不会覆盖现有选择，不会启用推理。新宿主必须在检查适配能力/资源之后，通过用户动作启用；选择失败时已验证的模型仍留在输出目录。

新增模型先核验官方资产许可、不可变 revision、文件大小与摘要，更新来源记录和 catalog，再评测和适配；不能添加来源或许可不明的权重。默认目录只收 GGUF 自由文本模型；原始 PyTorch 权重导出与语音 Paraformer 准备是不同流程。
