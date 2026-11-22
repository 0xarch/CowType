# 设置界面

Python GTK4/libadwaita 设置，包含输入、按键、语音、互译和词库五页。运行 `python3 src-settings/app/main.py --page input`，安装后使用 cowtype-config。语音和可选词典只在用户操作后下载；互译页配置 Bilibili 或自定义 OpenAI 兼容服务、目标语言及环境变量/密钥环 Key，显式保存服务设置或测试接口。详见 [USAGE](../docs/USAGE.md) 和[互译组件](../src-translate/README.md)。

启用“表情与符号候选”后显示肤色设置：可开关选中表情后的二级肤色选择，并通过 👍👍🏻👍🏼👍🏽👍🏾👍🏿 六个互斥单选按钮选择默认肤色；开关关闭后仍可配置默认肤色。
