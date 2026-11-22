# 打包

Arch x86_64 用户可在项目根目录运行 `bash build-arch.sh`，自动准备词库和编译依赖、校验源码并调用 makepkg；生成的安装包在 dist/，不会自动安装。生产包名为 cowtype，不编译或运行开发测试；原生测试需在开发构建中显式设置 BUILD_TESTING=ON。

arch/PKGBUILD.in 是源码归档使用的模板。脚本生成带真实 SHA-256 的 build/arch/PKGBUILD 与 dist/PKGBUILD，便于手动构建。依赖安装和详细步骤见 [BUILD](../docs/BUILD.md)。不包含模型或个人数据。

## 多发行版 CI

`.github/workflows/linux-packages.yml` 在 dev/main 推送、PR 和手动运行时验证源码，准备一次固定版本的词库及 Cargo vendor，然后在 Arch、Fedora 45、openSUSE Tumbleweed 和 Debian unstable 的 x86_64 容器内分别构建。Tumbleweed 和 Debian unstable 为滚动目标，不保证与 Leap 或 Debian 稳定版兼容。支持范围以 Actions 实际依赖版本、安装及插件测试为准，不降低 Fcitx5/libime 的最低版本来绕过失败。

`tooling/package_linux.py` 为 Deb/RPM 提供原生打包：Deb 使用 dpkg-shlibdeps 推导 ELF 依赖，RPM 由 rpmbuild 推导；Python/GTK/libadwaita 依赖显式声明，openSUSE 使用原生 typelib 包名。Arch 复用现有 PKGBUILD。生产构建关闭测试，单独的开发构建负责 CTest；容器内安装后检查插件链接、图标、词库和隔离的输入法事件，并在 GTK Broadway 可用时启动已安装的设置程序。不下载模型，不更改用户桌面配置。

Tumbleweed 使用 `opensuse/tumbleweed:latest`，产物为 `cowtype-<版本>-1.opensusetumbleweed.x86_64.rpm`；CI 通过 zypper 安装本地 RPM，再执行 `rpm -V` 和插件验证。最小容器显式安装 DejaVu 字体，以便 Pango/GTK 进行无头窗口测试。已通过的[容器打包与安装验证](https://github.com/0xarch/CowType/actions/runs/44925306514)使用 Fcitx5 5.1.23、libime 1.1.17、GTK 4.24.1、libadwaita 1.10.0；这不是完整桌面或真实麦克风验收。

发布时先更新版本号和 CHANGELOG，推送 dev/main 触发 `Linux packages`。全部任务成功后，在同一提交上手动运行 `Publish verified release`，填入成功构建的 run ID。发布任务验证构建来源、分支和提交一致，再收集四个发行版的原生包及离线源码、PKGBUILD，检查附件齐全并生成 SHA256SUMS，最后才创建 Tag 和 Release。发布说明取自 CHANGELOG。普通构建不创建 Tag 或 Release，发布复用已验证的产物；已有标签与 Release 不会被自动覆盖。

Debian 包的内部版本保留 `-1~debianunstable`，发布文件名使用 `-1.debianunstable`，避免上传后特殊符号被规范化而与 SHA256SUMS 中的文件名不一致。

架构上参考 WowType 的容器矩阵、共享数据 artifact、原生包安装校验和标签发布设计；脚本为 CowType 独立编写，未复制其打包实现。首轮不包含旧发行版、ARM64、Nix 或 macOS。

Ubuntu 暂不提供自动构建包：26.04 官方仓库的 Fcitx5 为 5.1.19，不满足当前 >= 5.1.23 要求。原因及自行构建要求见[构建说明](../docs/BUILD.md#ubuntu)。

Deepin 23、25 和 openSUSE Leap 16.0 已尝试官方容器构建，Fcitx5 分别为 5.1.11、5.1.12、5.1.13，均因低于最低要求而移出自动构建矩阵，暂不列入支持范围。镜像、试构建记录和自行构建要求见[构建说明](../docs/BUILD.md#deepin-和-opensuse-leap)。
