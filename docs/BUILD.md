# 构建、测试与打包

## Arch Linux：从源码生成安装包

如果只是想安装使用，从这里开始即可。

```sh
# 准备材料
sudo pacman -S --needed base-devel git cmake ninja rust cargo fcitx5 libime boost gtk4 libadwaita python python-gobject
git clone https://github.com/0xarch/CowType.git
cd CowType
# 构建
bash build-arch.sh
```

请用普通用户运行脚本。
脚本先检查依赖，再准备默认雾凇、五笔词库和 Rust 编译依赖，最后调用 makepkg 编译、测试和打包。
首次准备需要访问 GitHub 和 crates.io；后续会复用下载缓存。
可先用 `bash build-arch.sh --check` 检查环境，此命令不下载或编译。

成功后安装包在 `dist/`。使用 `pacman -U dist/<package>` 安装。

如果修改过 makepkg 的压缩格式，以 `dist/` 中实际生成的文件名为准。安装后重启 Fcitx5，在 Fcitx5 配置工具中添加 CowType 拼音或五笔。没有配置工具时可安装 `fcitx5-configtool`。设置界面使用 `cowtype-config` 打开，详见[使用说明](USAGE.md)。语音默认关闭，打包不会下载模型。

遇到问题时：

- **提示缺少依赖**：按脚本显示的 `sudo pacman -S --needed ...` 命令安装，再重新运行脚本。Arch 需要保持系统更新；不要只刷新软件源而不升级系统。
- **Rust 提示没有默认工具链**：如果使用 rustup，先运行 `rustup default stable`。
- **下载失败**：检查到 GitHub、crates.io 的网络连接，再重新运行；不需要清空整个构建目录。脚本会显示失败阶段。
- **源码清单不匹配**：未修改源码时请重新下载完整项目。开发者确认自己的改动后，按下文更新清单；脚本不会自动认可源码变化。
- **编译或测试失败**：保留末尾错误和失败阶段，提交 Issue 时附上系统与依赖版本。打包成功不代表已经验收真实麦克风或桌面输入体验。

## 开发：单独构建与测试

需要 Linux、Rust/Cargo、Python 3.9+。原生构建还需 CMake、Ninja、C++20 工具链、Fcitx5 >= 5.1.23、libime >= 1.1.7 与 Boost；设置运行需要 GTK4、libadwaita 和 PyGObject。语音使用系统 ONNX Runtime 和 parecord，不内置模型。Paraformer 准备脚本要求 Python 3.10+，见组件说明。

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

首次准备默认词典需要网络：

```sh
python3 src-data-tools/dictionaries/manage.py build-default --source-cache build/review --output build/data
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build/native -j2
ctest --test-dir build/native --output-on-failure
```

已有输出可选择新目录并传 CMake 的 COWTYPE_DICTIONARY_DIR。原生探针使用隔离的 Fcitx5 路径、临时 XDG 目录与假语音进程；GTK 测试需要可用测试显示后端。真实模型测试和 tests/voice_resident.py 需另外提供模型，合成静音测试不代表麦克风与准确率验收。

## 源码清单与 Arch 包

手动打包前，除了上面准备的 `build/data` 词典，还需归档锁文件对应的 Rust 依赖：

```sh
cargo vendor --locked --versioned-dirs --manifest-path src-voice/Cargo.toml build/voice-vendor
```

准备好后，确认源码改动并更新清单，再打包。直接使用根目录脚本会自动准备依赖并校验清单。

```sh
python3 tooling/development.py --refresh
python3 tooling/development.py
python3 tooling/package_arch.py
cd build/arch
PKGDEST="$PWD/../../dist" makepkg --cleanbuild --force
```

源码清单在 tooling/SOURCE-MANIFEST.json，可随正常代码修改更新，不再绑定阶段性快照或冻结组件。它仍排除用户日志、模型、Git 元数据及构建目录，并验证文件摘要和文档链接。Arch 源码归档另行校验词库的锁定输入/输出和 build/voice-vendor 依赖摘要。发行包不包含模型。
