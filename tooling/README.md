# 维护工具

development.py 检查项目源码清单和文档链接，`--refresh` 更新 SOURCE-MANIFEST.json；没有冻结文件或历史分支约束。根 README.md 只登记路径，不校验内容哈希与字节数，仍检查链接并收入源码归档。package_arch.py 校验源码、词库和 vendored 依赖后生成离线 Arch 源码归档；compile_i18n.py 编译统一文案。命令见 [BUILD](../docs/BUILD.md)。

确认源码改动后，运行 `./tooling/update-source-manifest.sh` 可一次完成清单更新与校验。脚本使用自身位置定位项目，也可从其他工作目录调用；任一步失败即返回非零状态。

`icons.cmake` 从 scalable SVG 增量生成构建目录内的 hicolor PNG；这些派生文件不参与源码清单、摘要校验或源码归档。

`prepare_release.py` 检查成功构建收集的四发行版安装包、源码和 PKGBUILD，拒绝缺失、空文件、符号链接或额外附件，并生成 SHA256SUMS。发布工作流在此检查通过后才创建 Tag 和 Release；`release_notes.py` 从当前版本的 CHANGELOG 提取发布说明。
