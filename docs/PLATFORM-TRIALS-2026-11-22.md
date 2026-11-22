# 2026-11-22 多系统、多架构容器构建报告

引入 Fcitx5 兼容层后，**14 个目标在同一次常规 GitHub Actions 中全部完成编译、测试、打包、安装和插件验证：14 通过、0 失败、0 跳过**。其中 13 个为原生 x86_64/ARM64 构建，1 个为 QEMU 下的 Deepin LoongArch64。

- [13 个原生目标全部通过](https://github.com/0xarch/CowType/actions/runs/44986227966)，提交 `dd62532409bf043bf00f408cb239d739994ab108`。
- [全部通过的最终 14 目标构建](https://github.com/0xarch/CowType/actions/runs/44987013285)，提交 `de1dedba6fb50bbdd93cbba6b6a4561468182a6a`。
- [兼容层前的首轮试构建](https://github.com/0xarch/CowType/actions/runs/44978388218)：6 项通过、8 项因当时统一要求 Fcitx5 >= 5.1.23 而停止、0 项跳过。该历史版本门槛已由兼容实现替代。

## 全部目标

| 类别 | 系统版本 | 架构 | 镜像 | Fcitx5 | 结果 |
|---|---|---|---|---|---|
| 已有 | Arch Linux | x86_64 | `archlinux:latest` | 5.1.23 | 通过 |
| 已有 | Fedora 45 | x86_64 | `fedora:45` | 5.1.23 | 通过 |
| 已有 | Debian unstable | x86_64 | `debian:unstable` | 5.1.23 | 通过 |
| 已有 | openSUSE Tumbleweed | x86_64 | `opensuse/tumbleweed:latest` | 5.1.23 | 通过 |
| 原暂缓 | Ubuntu 26.04 | x86_64 | `ubuntu:26.04` | 5.1.19 | 通过 |
| 原暂缓 | Deepin 23 | x86_64 | `linuxdeepin/deepin:23` | 5.1.11 | 通过 |
| 原暂缓 | Deepin 25 | x86_64 | `linuxdeepin/deepin:25` | 5.1.12 | 通过 |
| 原暂缓 | openSUSE Leap 16.0 | x86_64 | `registry.opensuse.org/opensuse/leap:16.0` | 5.1.13 | 通过 |
| 新系统 | openKylin 3.0（huanghe） | x86_64 | `openkylin/openkylin:latest` | 5.1.19 | 通过 |
| 新架构 | Ubuntu 26.04 | ARM64 | `ubuntu:26.04` | 5.1.19 | 通过 |
| 新架构 | Deepin 25 | ARM64 | `linuxdeepin/deepin:crimson-arm64` | 5.1.12 | 通过 |
| 新架构 | Debian unstable | ARM64 | `debian:unstable` | 5.1.23 | 通过 |
| 新架构 | Fedora 45 | ARM64 | `fedora:45` | 5.1.23 | 通过 |
| 新架构 | Deepin 25 | LoongArch64 | `linuxdeepin/deepin:crimson-loong64` | 5.1.12 | 通过（QEMU） |

全部请求的镜像和对应架构都存在，0 项因镜像缺失而跳过。`latest`、unstable 和 Tumbleweed 表示本次运行时的内容，不保证将来软件源版本不变；本轮未扩展到 openKylin ARM64 或 Deepin 23 ARM64。

ARM64 使用 GitHub 原生 `ubuntu-24.04-arm` runner，运行目标发行版自己的容器，未交叉编译或模拟。LoongArch64 在 x86_64 runner 上通过 QEMU 运行 Deepin 原生 `loong64` 用户空间；这只代表模拟环境验证，不代表龙芯实体机、音频设备或模型性能验收。

Deepin `:25` 标签在本次测试中仅提供 amd64；其他架构使用其[上游发布流程](https://github.com/deepin-community/deepin-docker/blob/89c6874e827eaace16000eb403d1e1ef5ef07eff/.github/workflows/build-docker.yml)提供的 `crimson-arm64`、`crimson-loong64`。

## 先前失败原因及处理

| 影响范围 | 实际原因 | 处理与验证 |
|---|---|---|
| Ubuntu、Deepin、Leap、openKylin | 原统一版本门槛 5.1.23 阻止进入编译，掩盖了可兼容的 API 差异 | 基线调整为 5.1.11；按实际接口可用性集中封装，保留完整编译和安装测试 |
| Fcitx5 < 5.1.19 | 没有新 `StandardPaths` 目录 API | `compat/paths.h` 使用旧 `StandardPath`，统一路径类型与顺序；原生测试仍拒绝访问隔离根目录以外的路径 |
| Fcitx5 < 5.1.20（包含 5.1.19） | 没有 `consumeMaybeEscapedValue` 字段解析接口 | `compat/strings.h` 使用本项目边界扫描与旧版已有的 `unescapeForValue`；测试引号、空格、反斜线、换行和中文字段 |
| Deepin 23 | 没有 `FCITX_ADDON_FACTORY_V2` 工厂宏 | `compat/addon.h` 按宏是否存在选择新旧导出，探针共享符号选择，并通过框架真实插件发现验证 |
| Deepin 各架构 | 原生 Rust 1.81 过旧，核心使用的 `is_none_or` 与语音依赖最低 Rust 1.88 不满足 | 仅在一次性 CI 容器内准备固定 Rust 1.88.0 minimal；rustup 1.28.2 按架构内置 SHA-256 校验，系统库保持原生 |
| 较老 libime（Deepin 1.1.8/1.1.10） | 原始文本回退候选没有拼音编码，却被传给词典来源查询，导致 `invalid full pinyin` | 跳过无编码节点的来源查询；词典与输入交互测试覆盖旧库 |
| Ubuntu、openKylin | libc 严格检查将忽略 `write` 返回值视为编译错误 | 检查写入结果，处理短写和 EINTR；保留 `-Werror` |
| openKylin | 默认行内预编辑偏好关闭，文本在候选窗显示，原测试仅检查行内显示 | 测试显式设置合成输入框偏好，并同时验证行内与候选窗两种路径；产品仍遵循用户偏好 |
| openKylin 安装阶段 | 官方 `archive.build.openkylin.top` 无法连接，下载 Qt 等可选推荐包失败 | 安装验证仅安装必要依赖，保留下载重试；未更换软件源、忽略必要依赖或跳过安装 |
| LoongArch64/QEMU | 原测试固定等待假语音进程 200ms、假翻译进程 400ms，模拟运行启动较慢导致误报 | 对异步结果使用最长 10s 的实际状态等待，保留修饰键长按时序断言；本地以 650ms 假翻译响应验证。最终模拟环境结果以上表为准 |

只有目录 API 以 5.1.19 为分界；字段解析 API 实际在 5.1.20 才加入，工厂导出则直接检查宏存在性。没有把“>= 5.1.19”错误地当作所有新接口都可用。最低版本与包依赖同步调整，运行时代码无需调用 5.1.23 新增的 `updatePreedit(bool)`。

## 检查和产物

常规构建先在共享源任务中运行 Rust 核心、语音宿主、Python 数据/模型/设置测试，准备锁定的雾凇、五笔及 Cargo vendor。每个目标在自己的发行版容器中：

1. 使用原生 Fcitx5、libime、GTK 等库进行开发构建，运行 10 项 CTest 和 55 项 Python 测试。
2. 另外进行关闭开发测试的生产构建，生成 CowType 与 cowtype-voice 0.4.0 原生包。
3. 实际安装包，再验证 ELF 链接、图标、数据、合成 Fcitx5 事件、假翻译进程和已安装设置程序的 GTK 启动。

测试使用隔离 XDG 路径及合成输入、假语音/翻译进程，不访问用户词典、桌面配置、真实麦克风或翻译服务；不下载语音或统计模型，不代表真实模型准确率或硬件性能验收。生产包不包含测试、Rust 工具链、容器或模型。

成功的常规构建在 Actions 的 **Artifacts** 提供 `package-<目标>-<架构>`。Debian/Ubuntu/Deepin/OpenKylin 使用 Deb 的 `amd64`、`arm64`、`loong64` 文件架构名；Fedora ARM64 的 RPM 文件架构名为 `aarch64`，下载时按目标发行版和架构选择，不能跨发行版视作兼容包。LoongArch64 已完整通过，对应 `package-deepin25-loongarch64`，包名为 `cowtype_0.4.0-1.deepin25_loong64.deb`。

手动试构建还提供 `platform-report-<目标>-<架构>`（镜像清单、版本、阶段日志、result.json）、`platform-results` 汇总和通过目标的 `trial-package-*`。失败阶段及具体日志保留在对应运行中；Actions 产物有保留期限，下载需登录。

以上记录对应版本发布前的 0.4.0 开发构建；0.4.1 发布会重新构建并验证全部 14 个目标，再将对应版本的预编译包、离线源码、PKGBUILD 和 SHA256SUMS 提供于 [Release](https://github.com/0xarch/CowType/releases/tag/v0.4.1)。构建方式见[BUILD](BUILD.md)，工作流使用见[打包说明](../packaging/README.md)。
