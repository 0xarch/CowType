# 维护工具

development.py 检查项目源码清单和文档链接，`--refresh` 更新 SOURCE-MANIFEST.json；没有冻结文件或历史分支约束。根 README.md 只登记路径，不校验内容哈希与字节数，仍检查链接并收入源码归档。package_arch.py 校验源码、词库和 vendored 依赖后生成离线 Arch 源码归档；compile_i18n.py 编译统一文案。命令见 [BUILD](../docs/BUILD.md)。

确认源码改动后，运行 `./tooling/update-source-manifest.sh` 可一次完成清单更新与校验。脚本使用自身位置定位项目，也可从其他工作目录调用；任一步失败即返回非零状态。

`icons.cmake` 从 scalable SVG 增量生成构建目录内的 hicolor PNG；这些派生文件不参与源码清单、摘要校验或源码归档。

`prepare_release.py` 检查成功构建收集的四发行版、六个架构组合的安装包、源码和 PKGBUILD，拒绝缺失、空文件、符号链接或额外附件，并生成 SHA256SUMS。发布工作流在此检查通过后才创建 Tag 和 Release；`release_notes.py` 从当前版本的 CHANGELOG 提取发布说明。

`platform_trials.py` 定义手动试构建矩阵、检查容器镜像的平台可用性、记录各阶段结果并生成汇总；`platform-probe.sh` 仅在一次性容器内安装原生 Fcitx5 开发包并检查最低版本。完整依赖、开发测试、生产打包和安装验证复用 `ci-linux.sh`。试构建记录见[平台报告](../docs/PLATFORM-TRIALS-2026-11-22.md)。

`ci-rust.py` 仅用于一次性 CI 容器：系统 Rust < 1.88 时，从 Rust 官方下载固定 rustup 1.28.2（按架构内置 SHA-256 校验），在 `/opt/cowtype-rust/` 安装固定 Rust 1.88.0 minimal 工具链。`ci-linux.sh` 在后续阶段显式复用该目录；已有足够新编译器时不下载。不会更新开发者桌面、替换系统库或下载模型。

平台试构建可通过 `gh workflow run platform-trials.yml --ref dev -f targets=deepin23-x86_64,openkylin-x86_64` 仅重试指定组合；默认 `all`。筛选值必须属于固定矩阵，不接受任意镜像/脚本，汇总只列请求的目标；不同筛选的运行可同时进行。
