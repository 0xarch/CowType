# 打包

Arch x86_64 用户可在项目根目录运行 `bash build-arch.sh`，自动准备词库和编译依赖、校验源码并调用 makepkg；产物在 dist/，不会自动安装。生产包名为 cowtype，不编译或运行开发测试；原生测试需在开发构建中显式设置 BUILD_TESTING=ON。依赖与步骤见 [BUILD](../docs/BUILD.md)。

arch/PKGBUILD.in 是源码归档模板。脚本生成带真实 SHA-256 的 build/arch/PKGBUILD 与 dist/PKGBUILD；归档包含固定词库和 Cargo vendor，不包含模型或个人数据。

## 多发行版 CI

`.github/workflows/linux-packages.yml` 在 dev/main 推送、PR 和手动运行时验证源码并准备一次离线输入，然后构建：

| 系统版本 | x86_64 | ARM64 | LoongArch64 | 格式 |
|---|---|---|---|---|
| Arch Linux | 原生 | — | — | pkg.tar.zst |
| Fedora 45 | 原生 | 原生 | — | RPM |
| Debian unstable | 原生 | 原生 | — | Deb |
| openSUSE Tumbleweed | 原生 | — | — | RPM |
| Ubuntu 26.04 | 原生 | 原生 | — | Deb |
| Deepin 23 | 原生 | — | — | Deb |
| Deepin 25 | 原生 | 原生 | QEMU 验证 | Deb |
| openSUSE Leap 16.0 | 原生 | — | — | RPM |
| openKylin 3.0 | 原生 | — | — | Deb |

共 13 个原生组合和 1 个 QEMU 组合。ARM64 使用 GitHub `ubuntu-24.04-arm` 原生 runner；runner 的 Ubuntu 系统不代表容器产物是 Ubuntu 包。LoongArch64 在 x86_64 runner 上注册 QEMU，再用 `linuxdeepin/deepin:crimson-loong64` 的原生用户空间、编译器与 dpkg 构建。只在全部验证通过后上传 `package-<目标>-<架构>`。

Fcitx5 的兼容基线为 5.1.11；目录 API、词典字段解析和插件工厂差异集中在 `src-fcitx5/compat/`，详见[版本兼容](../docs/BUILD.md#fcitx5-版本兼容)。其他实际依赖要求保持。Deepin 原生 Rust 1.81 过旧时，CI 在容器的 `/opt/cowtype-rust/` 使用经 SHA-256 校验的固定 rustup 1.28.2 准备 Rust 1.88.0 minimal，保留原生 Fcitx5/libime/GTK，不更换系统软件源。

`tooling/package_linux.py` 为 Deb/RPM 提供原生打包：Deb 使用 dpkg-shlibdeps 推导 ELF 依赖，RPM 由 rpmbuild 推导；Python/GTK/libadwaita 依赖显式声明，openSUSE 使用原生 typelib 包名。Arch 复用 PKGBUILD。生产构建关闭测试，单独开发构建运行 10 项 CTest 与 Python 检查。容器实际安装后验证插件链接、图标、词库、隔离输入法事件、假翻译进程和已安装设置界面的 GTK 启动。Deb 验证仅安装必要依赖并重试下载，可选推荐包不作为安装验收前提；不会忽略必要依赖或跳过验证。不下载模型，不更改用户桌面。

Tumbleweed 使用 `opensuse/tumbleweed:latest`，Leap 16.0 使用 `registry.opensuse.org/opensuse/leap:16.0`；RPM 文件分别带 `opensusetumbleweed`、`opensuseleap16.0` 后缀，CI 通过 zypper 安装再执行 rpm 验证。最小容器显式安装 DejaVu 字体以便无头 GTK 渲染。Debian unstable 与 Tumbleweed 是滚动目标，包面向各自原生依赖；所有包都应按系统和架构分别选择，不能视为跨发行版兼容包。

Debian 包内部版本保留 `-1~<目标>`，发布文件名使用 `-1.<目标>`，避免特殊字符规范化导致 SHA256SUMS 文件名不一致。ARM64 在 Deb 文件名中为 `arm64`、RPM 为 `aarch64`；Deepin LoongArch64 的 Deb 架构名为 `loong64`。

## 发布

先更新版本号和 CHANGELOG，推送 dev/main 触发 `Linux packages`。全部任务成功后，在同一提交上手动运行 `Publish verified release` 并填写成功 run ID。发布任务验证构建来源、分支和提交一致，再收集 14 个系统/架构原生包及离线源码、PKGBUILD，检查附件齐全、生成 SHA256SUMS，最后创建 Tag 和 Release。发布说明取自 CHANGELOG。

普通构建不创建 Tag 或 Release；发布复用该提交已验证的产物，已有标签或 Release 不会自动覆盖。Release 提供全部 14 个目标的预编译包，附带离线源码、PKGBUILD 和 SHA256SUMS。

## 手动平台试构建

`.github/workflows/platform-trials.yml` 可通过 `gh workflow run platform-trials.yml --ref dev` 重试全部 14 个组合。也可传 `-f targets=deepin23-x86_64,openkylin-x86_64` 只重试指定项；筛选必须属于固定矩阵，不能注入任意镜像或命令。不同筛选的运行可并行。

试构建先检查镜像与架构，再安装原生 Fcitx5 开发包检查兼容基线，通过后复用完整依赖、编译、测试、打包和安装流程。镜像不可用或架构缺失会记录跳过并保留日志；其余阶段失败保持失败状态。LoongArch64 使用 QEMU，ARM64 原生运行。

每项上传 `platform-report-<目标>-<架构>`，包含镜像清单、系统/版本、阶段日志和 result.json；通过项上传 `trial-package-<目标>-<架构>`。汇总即使存在失败也会执行，输出 `platform-results` 和 Actions Summary，只列本次请求的目标。Actions 产物有保留期限，下载需登录。

