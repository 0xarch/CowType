# GB 双拼兼容映射

`shuangpin-gb.h` 的 GB/T 34947-2017 映射来自 libime `src/libime/pinyin/shuangpindata.h`，提交 `34ee1fde75e79ebbf91e6f739f37b14777d634f8`，作者 CSSlayer (Weng Xuetian)，按 LGPL-2.1-or-later 分发。CowType 将表格转为 libime 自定义方案语法，以兼容缺少 GB 内建方案的 libime 版本；不修改拼音映射。

源地址：https://github.com/fcitx/libime/blob/34ee1fde75e79ebbf91e6f739f37b14777d634f8/src/libime/pinyin/shuangpindata.h

许可证全文见项目 `LICENSES/LGPL-2.1-or-later.txt`。其他双拼方案由系统 libime 自身提供。
