# 图标

源文件为 hicolor/scalable/apps/cowtype.svg，来源与署名见 [归属说明](../../docs/ATTRIBUTION.md)。修改 SVG 后可用 rsvg-convert 生成 PNG：

```sh
for s in 16 22 24 32 48 64 128 256; do
  rsvg-convert -w "$s" -h "$s" assets/icons/hicolor/scalable/apps/cowtype.svg -o "assets/icons/hicolor/${s}x${s}/apps/cowtype.png"
done
```

应用图标名为 cowtype，供 Fcitx5、设置启动器和关于窗口使用。
