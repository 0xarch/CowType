# 构建、测试与打包

## Arch Linux：从源码生成安装包

如果只是想安装使用，从这里开始即可。

```sh
# 准备材料
sudo pacman -S --needed base-devel git cmake ninja rust cargo fcitx5 libime boost openssl librsvg gtk4 libadwaita python python-gobject
git clone https://github.com/0xarch/CowType.git
cd CowType
# 构建
bash build-arch.sh
```

请用普通用户运行脚本。
脚本先检查依赖，再准备默认雾凇、五笔词库和 Rust 编译依赖，最后调用 makepkg 编译和打包。生产构建不编译或运行测试；开发验证请使用下方的独立测试命令。
首次准备需要访问 GitHub 和 crates.io；后续会复用下载缓存。
可先用 `bash build-arch.sh --check` 检查环境，此命令不下载或编译。

成功后安装包在 `dist/`。使用 `pacman -U dist/<package>` 安装。

如果修改过 makepkg 的压缩格式，以 `dist/` 中实际生成的文件名为准。安装后重启 Fcitx5，在 Fcitx5 配置工具中添加 CowType 拼音或五笔。没有配置工具时可安装 `fcitx5-configtool`。设置界面使用 `cowtype-config` 打开，详见[使用说明](USAGE.md)。语音默认关闭，打包不会下载模型。

遇到问题时：

- **提示缺少依赖**：按脚本显示的 `sudo pacman -S --needed ...` 命令安装，再重新运行脚本。Arch 需要保持系统更新；不要只刷新软件源而不升级系统。
- **Rust 提示没有默认工具链**：如果使用 rustup，先运行 `rustup default stable`。
- **下载失败**：检查到 GitHub、crates.io 的网络连接，再重新运行；不需要清空整个构建目录。脚本会显示失败阶段。
- **源码清单不匹配**：未修改源码时请重新下载完整项目。开发者确认自己的改动后，按下文更新清单；脚本不会自动认可源码变化。
- **编译失败**：保留末尾错误和失败阶段，提交 Issue 时附上系统与依赖版本。打包成功不代表已经验收真实麦克风或桌面输入体验。

## 开发：单独构建与测试

需要 Linux、Rust/Cargo、Python 3.9+。原生构建还需 CMake、Ninja、C++20 工具链、Fcitx5 >= 5.1.23、libime >= 1.1.7 、Boost、OpenSSL 与 librsvg（rsvg-convert）；设置运行需要 GTK4、libadwaita 和 PyGObject。语音使用系统 ONNX Runtime 和 parecord，不内置模型。Paraformer 准备脚本要求 Python 3.10+，见组件说明。

首次运行离线命令前，先准备 Cargo 依赖（需要网络）：

```sh
cargo fetch --locked --manifest-path src-voice/Cargo.toml
```

```sh
export CARGO_TARGET_DIR="$PWD/target"
cargo build --offline --locked
cargo test --offline --locked --features native-ffi
cargo test --offline --locked --manifest-path src-voice/Cargo.toml
python3 -m unittest discover -s tests -p 'test_*.py'
python3 -m unittest discover -s src-voice/paraformer/tests -p 'test_*.py'
python3 -m unittest discover -s src-model-tools/text-models -p 'test_*.py'
```

`--offline` 需要依赖已缓存；首次配置时按锁文件获取依赖。核心库没有第三方 Rust crate，语音是单独的 Cargo 工程。所有修改使用项目目录内产物，测试不得读写用户配置或调用真实设备。

互译客户端使用 Python 标准库，无额外 HTTP 库或 Rust 依赖；可选桌面密钥环需要 libsecret 的 Secret 1 GI typelib 和用户自行提供的 Secret Service。Arch/Debian/Fedora/openSUSE 包名与配置步骤见[互译组件](../src-translate/README.md)。普通 CI 只使用合成 HTTP 服务和假翻译进程，不读取 `COWTYPE_TRANSLATE_KEY` 或调用外部翻译接口。

首次准备默认词典需要网络：

```sh
python3 src-data-tools/dictionaries/manage.py build-default --source-cache build/review --output build/data
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTING=ON
cmake --build build/native -j2
ctest --test-dir build/native --output-on-failure
```

构建自动从 scalable SVG 生成缺失或过期的多尺寸 PNG，产物位于构建目录的 `icons/hicolor/`，无需在源码树保存或校验 PNG。可单独运行 `cmake --build build/native --target cowtype-icons`。

已有输出可选择新目录并传 CMake 的 COWTYPE_DICTIONARY_DIR。原生探针使用隔离的 Fcitx5 路径、临时 XDG 目录与假语音进程；GTK 测试需要可用测试显示后端。真实模型测试和 tests/voice_resident.py 需另外提供模型，合成静音测试不代表麦克风与准确率验收。

## 源码清单与 Arch 包

手动打包前，除了上面准备的 `build/data` 词典，还需归档锁文件对应的 Rust 依赖：

```sh
cargo vendor --locked --versioned-dirs --manifest-path src-voice/Cargo.toml build/voice-vendor
```

准备好后，确认源码改动并更新清单，再打包。直接使用根目录脚本会自动准备依赖并校验清单。

```sh
./tooling/update-source-manifest.sh
python3 tooling/package_arch.py
cd build/arch
PKGDEST="$PWD/../../dist" makepkg --cleanbuild --force
```

源码清单在 tooling/SOURCE-MANIFEST.json，可随正常代码修改更新，不再绑定阶段性快照或冻结组件。它仍排除用户日志、模型、Git 元数据及构建目录，并验证文件摘要和文档链接。根目录 README.md 仅登记路径，不校验内容哈希或字节数；其文件存在性、文档链接检查和源码归档保留。Arch 源码归档另行校验词库的锁定输入/输出和 build/voice-vendor 依赖摘要。发行包不包含模型。

`tooling/update-source-manifest.sh` 会先更新清单，再重新校验；可从任意工作目录通过脚本路径运行。仅检查而不更新时仍使用 `python3 tooling/development.py`。

## 多发行版与 GitHub Actions

Arch、Fedora、openSUSE Tumbleweed、Debian 容器构建与打包入口见[打包说明](../packaging/README.md)。常规矩阵包含四个 x86_64 目标及 Fedora 45、Debian unstable 两个 ARM64 目标，ARM64 使用原生 runner。自动构建保留原生依赖版本检查，开发测试与生产包分开构建。新增及暂缓系统的手动试构建流程、容器镜像和完整结果见[平台报告](PLATFORM-TRIALS-2026-11-22.md)。

## openSUSE Tumbleweed

已支持 x86_64 原生 RPM；使用 Tumbleweed 当前官方软件源，不能将其视作 Leap 或 Fedora 的兼容包。源码构建可先安装依赖：

```sh
sudo zypper refresh
sudo zypper install --no-recommends gcc-c++ make cmake ninja rust cargo libime-devel boost-devel \
  libopenssl-devel rsvg-convert fcitx5 fcitx5-devel python3 python3-gobject python3-gobject-Gdk \
  gtk4-tools typelib-1_0-Gtk-4_0 typelib-1_0-Adw-1 dejavu-fonts rpm-build git pkgconf-pkg-config \
  dbus-1 findutils file ca-certificates
```

按上文准备固定版本的词库、Cargo vendor 并更新源码清单后，可用普通用户生成生产包：

```sh
python3 tooling/package_linux.py --source . --format rpm --distro opensusetumbleweed --libdir lib64 --output dist
```

生成的包可用 `sudo zypper install ./dist/cowtype-*.opensusetumbleweed.x86_64.rpm` 安装。Fcitx5 插件安装到 `/usr/lib64/fcitx5`。Python 通用包名由 Tumbleweed 的对应版本包提供，打包工具为 GTK/libadwaita 显式声明 typelib 运行依赖。

## Ubuntu

Ubuntu 暂不列入自动构建支持范围。2026-11-22 的 GitHub Actions 重试中（x86_64 和 ARM64），Ubuntu 26.04 官方仓库提供 Fcitx5 5.1.19，而 CowType 当前需要 Fcitx5 >= 5.1.23，编译前的依赖检查因版本不足停止。Debian unstable 生成的 Deb 包面向其原生库版本，不应直接视作 Ubuntu 兼容包。

需要在 Ubuntu 自行构建的用户，应自行准备满足版本要求的 Fcitx5 运行库及开发包，确保它们的版本与安装路径一致，并满足 libime >= 1.1.7、GTK >= 4.10、libadwaita >= 1.5、OpenSSL、Boost、librsvg、Rust/Cargo（语音依赖要求 Rust >= 1.88）等条件。Debian 系的 libime 开发包拆分为 `libimecore-dev` 与 `libimepinyin-dev`，并没有本项目可用的统一 `libime-dev` 包。

准备依赖后，按上面的通用 CMake 开发步骤构建并运行测试，指定适合本机 Fcitx5 的 `CMAKE_INSTALL_LIBDIR`。不要仅修改最低版本检查来绕过配置失败；本项目不自动添加软件源或替换系统 Fcitx5。Ubuntu 的依赖升级、原生打包和桌面兼容性由使用者自行处理。

## Deepin、openSUSE Leap 和 openKylin

2026-11-22 的 [GitHub Actions 容器重试](https://github.com/0xarch/CowType/actions/runs/44978388218) 中，以下镜像都在安装原生开发包后的 Fcitx5 最低版本预检查处失败，未进入完整编译，因此暂不提供自动构建包：

| 发行版 | 容器镜像 | 实际 Fcitx5 版本 | CowType 最低要求 |
|---|---|---|---|
| Deepin 23 | `linuxdeepin/deepin:23` | 5.1.11 | 5.1.23 |
| Deepin 25 | `linuxdeepin/deepin:25` | 5.1.12 | 5.1.23 |
| openSUSE Leap 16.0 | `registry.opensuse.org/opensuse/leap:16.0` | 5.1.13 | 5.1.23 |
| openKylin 3.0 | `openkylin/openkylin:latest` | 5.1.19 | 5.1.23 |

这些版本记录对应本次试构建，不代表后续软件源永远不变。自行构建需要先准备满足要求且版本一致的 Fcitx5 运行库和开发包，并核对 libime、GTK、libadwaita 及 Rust 等其他依赖；不要降低版本检查来绕过失败。Debian unstable 的 Deb 包或其他发行版的 RPM 不应视作这些系统的兼容包。

Deepin 25 的 ARM64（`linuxdeepin/deepin:crimson-arm64`）与 LoongArch64（`linuxdeepin/deepin:crimson-loong64`）镜像均存在且可运行，原生 Fcitx5 同为 5.1.12，因依赖不足继续暂缓；不能据此断言其架构代码编译失败。完整 14 项结果、验证边界和日志入口见[本轮报告](PLATFORM-TRIALS-2026-11-22.md)。
