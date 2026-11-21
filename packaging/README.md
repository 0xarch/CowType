# 打包

Arch x86_64 用户可在项目根目录运行 `bash build-arch.sh`，自动准备词库和编译依赖、校验源码并调用 makepkg；生成的安装包在 dist/，不会自动安装。生产包名为 cowtype，不编译或运行开发测试；原生测试需在开发构建中显式设置 BUILD_TESTING=ON。

arch/PKGBUILD.in 是源码归档使用的模板。脚本生成带真实 SHA-256 的 build/arch/PKGBUILD 与 dist/PKGBUILD，便于手动构建。依赖安装和详细步骤见 [BUILD](../docs/BUILD.md)。不包含模型或个人数据。
