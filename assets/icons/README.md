# 图标

仓库只维护 `hicolor/scalable/apps/cowtype.svg`；来源与署名见[归属说明](../../docs/ATTRIBUTION.md)。PNG 为构建产物，不提交源码，也不列入源码摘要清单。

CMake 的 `cowtype-icons` 目标默认随构建运行，使用 `rsvg-convert` 生成 16、22、24、32、48、64、128、256 像素的 PNG，输出到构建目录的 `icons/hicolor/`。已存在且未过期的文件直接复用；缺失文件自动补齐，SVG 更新后自动重新生成。构建需安装 librsvg（Arch 包名），无需手动生成图标。

```sh
cmake --build build/native --target cowtype-icons
```

安装时同时提供 SVG 和生成的 PNG。应用图标名为 `cowtype`，供 Fcitx5、设置启动器和关于窗口使用。
