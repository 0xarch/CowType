# 打包

Arch x86_64 用户可在项目根目录运行 `bash build-arch.sh`，自动准备词库和编译依赖、校验源码并调用 makepkg；生成的安装包在 dist/，不会自动安装。生产包名为 cowtype，不编译或运行开发测试；原生测试需在开发构建中显式设置 BUILD_TESTING=ON。

arch/PKGBUILD.in 是源码归档使用的模板。脚本生成带真实 SHA-256 的 build/arch/PKGBUILD 与 dist/PKGBUILD，便于手动构建。依赖安装和详细步骤见 [BUILD](../docs/BUILD.md)。不包含模型或个人数据。

## 多发行版 CI

`.github/workflows/linux-packages.yml` 在 dev/main 推送、PR 和手动运行时验证源码，准备一次固定版本的词库及 Cargo vendor，然后在 Arch、Fedora 45 和 Debian unstable 的 x86_64 容器内分别构建。Debian unstable 为滚动目标，不能保证与 Debian 稳定版兼容。支持范围以 Actions 实际依赖版本、安装及插件测试为准，不降低 Fcitx5/libime 的最低版本来绕过失败。

`tooling/package_linux.py` 为 Deb/RPM 提供原生打包：Deb 使用 dpkg-shlibdeps 推导 ELF 依赖，RPM 由 rpmbuild 推导；Python/GTK/libadwaita 依赖显式声明。Arch 复用现有 PKGBUILD。生产构建关闭测试，单独的开发构建负责 CTest；容器内安装后检查插件链接、图标、词库和隔离的输入法事件。不下载模型，不更改用户桌面配置。

所有构建产物先上传到 Actions artifacts；v* 标签与 Cargo.toml 版本一致、所有任务成功后，汇总原生包、离线源码、PKGBUILD 和 SHA256SUMS 到 Release。更新内容来自 CHANGELOG.md。普通分支与手动运行不会发布 Release；已有 Release 不会被自动覆盖。

架构上参考 WowType 的容器矩阵、共享数据 artifact、原生包安装校验和标签发布设计；脚本为 CowType 独立编写，未复制其打包实现。首轮不包含旧发行版、ARM64、Nix 或 macOS。

Ubuntu 暂不提供自动构建包：26.04 官方仓库的 Fcitx5 为 5.1.19，不满足当前 >= 5.1.23 要求。原因及自行构建要求见[构建说明](../docs/BUILD.md#ubuntu)。

Deepin 23、25 和 openSUSE Leap 16.0 已尝试官方容器构建，Fcitx5 分别为 5.1.11、5.1.12、5.1.13，均因低于最低要求而移出自动构建矩阵，暂不列入支持范围。镜像、试构建记录和自行构建要求见[构建说明](../docs/BUILD.md#deepin-和-opensuse-leap)。
