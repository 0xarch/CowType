# 2026-11-22 多系统、多架构容器构建报告

本轮在 GitHub Actions 的一次性容器中重试已有系统、暂缓系统及新增系统/架构，共 **14 项：6 项通过、8 项失败、0 项跳过**。8 项失败均发生在 Fcitx5 最低版本预检查，未进入完整依赖安装和源码编译；当前要求为 **Fcitx5 >= 5.1.23**，本轮未降低要求或添加第三方软件源。

- [完整试构建及每项日志](https://github.com/0xarch/CowType/actions/runs/44978388218)，源码提交 `3b1488d561e65199e648a6f0ae3e6c9ef30d91f2`。整体状态为失败，如实反映 8 项依赖不足；汇总任务成功完成，并非所有平台构建失败。
- [同一提交的常规四目标构建](https://github.com/0xarch/CowType/actions/runs/44978343510)全部通过。
- 后续将本轮已验证的两个 ARM64 目标加入[常规构建](https://github.com/0xarch/CowType/actions/workflows/linux-packages.yml)，与四个既有 x86_64 目标共同持续验证。

## 已有系统：全部通过

| 发行版 | 架构 | 容器镜像 | Fcitx5 | 结果 |
|---|---|---|---|---|
| Arch Linux | x86_64 | `archlinux:latest` | 5.1.23 | 通过 |
| Fedora 45 | x86_64 | `fedora:45` | 5.1.23 | 通过 |
| Debian unstable | x86_64 | `debian:unstable` | 5.1.23 | 通过 |
| openSUSE Tumbleweed | x86_64 | `opensuse/tumbleweed:latest` | 5.1.23 | 通过 |

## 新增系统

| 发行版 | 架构 | 容器镜像 | Fcitx5 | 结果与失败原因 |
|---|---|---|---|---|
| openKylin 3.0（huanghe） | x86_64 | `openkylin/openkylin:latest` | 5.1.19 | 失败：低于 5.1.23，暂不加入支持列表 |

`latest` 镜像中的 `/etc/os-release` 确认为 openKylin 3.0；名称不是对后续滚动镜像内容的保证。本轮未测试 openKylin ARM64。

## 暂缓系统：重试后仍暂缓

| 发行版 | 架构 | 容器镜像 | Fcitx5 | 结果与失败原因 |
|---|---|---|---|---|
| Ubuntu 26.04 | x86_64 | `ubuntu:26.04` | 5.1.19 | 失败：低于 5.1.23 |
| Deepin 23 | x86_64 | `linuxdeepin/deepin:23` | 5.1.11 | 失败：低于 5.1.23 |
| Deepin 25 | x86_64 | `linuxdeepin/deepin:25` | 5.1.12 | 失败：低于 5.1.23 |
| openSUSE Leap 16.0 | x86_64 | `registry.opensuse.org/opensuse/leap:16.0` | 5.1.13 | 失败：低于 5.1.23 |

这些结果只说明原生软件源在本次测试时未满足最低依赖。后续依赖更新后可重试；自行升级依赖的步骤见[构建文档](BUILD.md)。不将 Debian unstable 或 Tumbleweed 的包视为这些系统的兼容包。

## 新增架构

| 发行版 | 架构 | 容器镜像 | Fcitx5 | 结果与后续处理 |
|---|---|---|---|---|
| Ubuntu 26.04 | ARM64 | `ubuntu:26.04` | 5.1.19 | 失败：低于 5.1.23，暂缓 |
| Deepin 25 | ARM64 | `linuxdeepin/deepin:crimson-arm64` | 5.1.12 | 失败：低于 5.1.23，暂缓 |
| Debian unstable | ARM64 | `debian:unstable` | 5.1.23 | 通过，加入常规构建 |
| Fedora 45 | ARM64 | `fedora:45` | 5.1.23 | 通过，加入常规构建 |
| Deepin 25 | LoongArch64 | `linuxdeepin/deepin:crimson-loong64` | 5.1.12 | 失败：低于 5.1.23，暂缓 |

ARM64 在 GitHub 原生 `ubuntu-24.04-arm` runner 上运行目标发行版容器，没有交叉编译或 QEMU。LoongArch64 在 x86_64 runner 上启用 QEMU，实际运行镜像并确认 `uname -m` 为 `loongarch64`，成功安装原生 `loong64` 开发包后因依赖版本停止。它未被跳过，也未完成 CowType 编译，不能据此声称 LoongArch64 已受支持或存在架构编译错误。

Deepin 的 `:25` 标签仅覆盖本次使用的 amd64；其他架构采用其[上游容器构建流程](https://github.com/deepin-community/deepin-docker/blob/89c6874e827eaace16000eb403d1e1ef5ef07eff/.github/workflows/build-docker.yml)发布的 `crimson-arm64`、`crimson-loong64` 标签。

## 验证范围与产物

源准备任务运行 Rust 核心、语音宿主和 Python 组件测试，再准备锁定的雾凇/五笔词库及 Cargo vendor。6 个通过的目标均完成：

1. 使用发行版原生依赖进行开发构建；9/9 CTest 与 55 项 Python 测试通过。
2. 单独进行关闭开发测试的生产构建，生成 CowType 与 cowtype-voice 0.4.0 原生安装包。
3. 在同一目标容器中实际安装包，检查依赖链接、图标、数据、合成 Fcitx5 输入事件、假翻译进程异步交互及已安装设置程序的 GTK 启动。

测试使用隔离 XDG 路径及假语音/翻译进程，没有下载语音模型或调用真实麦克风、用户翻译服务；不代表真实桌面输入、音频设备或模型准确率验收。试构建失败的 8 项只安装了用于最低版本检查的依赖，不声称其余依赖已验证。

每个目标的 `platform-report-<发行版>-<架构>` artifact 保存镜像清单、系统/版本信息、阶段日志和结构化结果；汇总见 `platform-results`。通过的目标还提供 `trial-package-<发行版>-<架构>`，包括新增的：

- `trial-package-debianunstable-arm64`：`cowtype_0.4.0-1.debianunstable_arm64.deb`
- `trial-package-fedora45-arm64`：`cowtype-0.4.0-1.fedora45.aarch64.rpm`

Actions 产物有保留期限，下载需要登录。常规构建的对应 artifact 为 `package-debianunstable-arm64`、`package-fedora45-arm64`。当前 0.4.0 Release 附件保持原样，两个 ARM64 包将纳入下次发布的完整性检查；本轮不创建新版本、Tag 或 Release。
