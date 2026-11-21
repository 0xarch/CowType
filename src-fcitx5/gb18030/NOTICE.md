# 上游参考与署名

参考项目：fcitx5-gb18030，https://github.com/openkylin/fcitx5-gb18030

审阅版本：e9d69af71876d832da4929ebe98b019705123454。

上游 `src/gb18030.cpp` 和 `src/gb18030.h` 的真实版权与许可声明为：

> Copyright 2023 KylinSoft Co., Ltd.
>
> This program is free software: you can redistribute it and/or modify it under
> the terms of the GNU General Public License as published by the Free Software
> Foundation, either version 3 of the License, or (at your option) any later
> version.

审阅到的上游根 `LICENSE` 实际为 LGPL-3.0 文本，与上述源码头不同。CowType 没有复制上游实现或重新标注上游文件许可；本组件为 CowType 独立实现，采用 GPL-3.0-or-later。这里记录参考关系并保留真实源码作者，不将上游代码当作 CowType 原创。详细文件摘要见 docs/SOURCES.json。

转换调用系统 libc iconv，其许可随系统运行库提供；未复制 glibc 实现、标准全文或外部映射表。
