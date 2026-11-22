# WowType 网格候选面板实现笔记

检查本机同级 WowType 仓库，版本 `ebbe4c88d52f7ca6ddf27c4b062f913b79ad269b`，所读文件无工作区修改。本次只分析设计，没有向 CowType 接入网格面板或复制其实现。

## Linux：复用 Fcitx 列表拼接网格

[fillGridPanel](https://github.com/Gloridust/WowType/blob/ebbe4c88d52f7ca6ddf27c4b062f913b79ad269b/fcitx5-addon/src/wowtype_engine.cpp#L2066) 使用竖排 `CommonCandidateList`，每个 `WowtypeGridRow` 是一整行文本而非单个词。一行固定 6 列，每屏 6 行，最多显示 36 个词；不创建额外 GTK、Qt 或独立弹出窗口。

一屏内先统计各列最长候选的估算显示宽度，再用全角/半角空格补齐。当前光标行显示全角数字 １–６，当前词加〔〕；原候选文本格式保留。宽度函数把 ASCII 算作 1、其他字符通常算作 2，并特殊处理肤色、变体选择符、ZWJ、旗帜序列，避免把每个码点都算成一个表情。

依据：`fcitx5-addon/src/wowtype_engine.cpp` 中的 `displayWidth`（521）、`padTo`（560）、`WowtypeGridRow`（483）、`fillGridPanel`（2071）。这是按文本宽度估算的兼容方案，不是字体整形后的像素级二维布局；字体、比例字形及前端渲染可能影响列对齐。

## 会话与按键

Rust 核心的 `core/src/session.rs` 定义 `GRID_COLS=6`、`GRID_ROWS=6`，维护全局候选序号 `grid_cursor`。平台层通过会话 view/ABI 同步光标、页码、展开状态和候选代数；只有候选代数变化才重新取得核心候选数据，并非每次移动光标都重查词库。

- 左/右移动一个候选，上/下按 6 列移动；末行不足时落到有效末项。
- PageUp/PageDown 按 36 项移动；数字 1–6 选择当前光标行相应列，空格选择当前候选。
- 普通候选展开时，在第一行按上或按 Esc 收起；打字、退格、回车及标点先收起，再按普通输入处理。
- 独立表情/符号面板有自己的搜索和关闭路径：英文字母搜索，退格删搜索词，空词再退格关闭；第一行按上不收起，回车或空格确认。

依据：[grid_key](https://github.com/Gloridust/WowType/blob/ebbe4c88d52f7ca6ddf27c4b062f913b79ad269b/core/src/session.rs#L2277)、同文件 1580 附近的表情工具面板处理、`core/include/wowtype_core.h` 的 view 定义。两种面板交互相似但并非完全相同。

`WowtypePanelList` 把 Fcitx 前端的上一页、下一页、候选移动及 setPage 转成会话按键，避免 UI 页码与会话光标脱节。它还检查列表是否仍是输入框当前显示的列表，持有跟踪式输入框引用，并在可能重建列表的操作期间保留列表对象，防止过期回调继续操作。

## 鼠标限制

[selectGridRow](https://github.com/Gloridust/WowType/blob/ebbe4c88d52f7ca6ddf27c4b062f913b79ad269b/fcitx5-addon/src/wowtype_engine.cpp#L2116) 只接收到行号：如果点的是光标所在行，就提交当前光标项；点其他行则提交该行第一个词。Linux 方案没有逐格点击命中信息。不能把它描述成每个格子都能独立点击的完整二维控件。

macOS 的 `macos/Sources/App/CandidatePanel.swift` 使用自绘 AppKit/NSView，为候选计算独立矩形；`layOutGrid` 是实际格子布局。这个平台实现不同于 Linux，不适合作为 CowType 的 Linux 接入路径直接移植。

## CowType 可以参考的部分

可参考“核心维护二维导航、平台只负责呈现”和“前端翻页必须回写会话”的职责划分；布局列数、单屏行数与数字选择也应使用同一份状态定义。肤色子选择可以复用同一套网格导航，但其返回上级及输入保留应继续遵守 CowType 现有会话语义。

若继续复用 Fcitx 标准候选 UI，文本行网格更容易覆盖现有前端，但需要接受行级鼠标选择和近似宽度的限制。若要求准确逐格点击与像素对齐，则需要能暴露独立单元格的前端或自绘候选 UI，并处理焦点、光标位置、Wayland/X11 定位、缩放和无障碍；仅设置候选列表的横排/竖排不能得到这些能力。
