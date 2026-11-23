# D-Bus 控制接口

CowType 的 Fcitx5 插件在用户会话总线上注册以下控制端点：

- 总线名：`org.cowtype.Input`
- 对象路径：`/org/cowtype/Input`
- 接口：`org.cowtype.Input1`

插件随 Fcitx5 加载；接口没有独立的 D-Bus 激活服务。Fcitx5 未运行或插件未加载时，调用返回总线服务不存在的错误。总线连接或注册失败只产生诊断，基本输入保持可用；已有名称不会被强行替换。

| 方法 | 参数 | 返回值 |
|---|---|---|
| `Version` | 无 | CowType 构建版本字符串 |
| `Mode` | 无 | 当前焦点 CowType 输入框的 `zh` 或 `en` |
| `SetMode` | 字符串 `zh`、`en` 或 `toggle` | 操作完成后的 `zh` 或 `en` |

`SetMode` 使用与中英文快捷键相同的切换路径，正在组合的输入按该路径提交；重复设置当前中英文状态不提交、不改变组合。该接口只控制中文与英文状态，不启动语音、翻译、GB18030 或 Unicode 模式，不切换当前应用的输入法。密码、敏感或禁用输入法的输入框拒绝控制。

接口使用当前有焦点且已激活 CowType 的输入上下文。无焦点或当前输入法为其他输入法时，`Mode` 和 `SetMode` 返回 `org.cowtype.Input.Error.Inactive`；受限输入框返回 `org.cowtype.Input.Error.Unavailable`；不支持的模式返回 `org.freedesktop.DBus.Error.InvalidArgs`。`Version` 不要求输入框焦点。

## 脚本调用

```sh
# 查询模式
gdbus call --session --dest org.cowtype.Input --object-path /org/cowtype/Input \
  --method org.cowtype.Input1.Mode
# 设置英文（例如编辑器进入普通模式）
gdbus call --session --dest org.cowtype.Input --object-path /org/cowtype/Input \
  --method org.cowtype.Input1.SetMode en
# 设置中文
gdbus call --session --dest org.cowtype.Input --object-path /org/cowtype/Input \
  --method org.cowtype.Input1.SetMode zh
```

脚本应处理输入框切换期间的 `Inactive` 错误。接口不读取或发布输入正文、候选、周围文本、个人记忆、翻译密钥或语音内容，不广播这些内容。访问权限沿用用户 session bus：同一用户会话中的程序可请求模式切换。

## 验证

`fcitx-dbus-api` 原生测试使用 `dbus-run-session` 启动私有总线和隔离 XDG 目录，检查版本、查询、切换、幂等设置、非法参数、焦点和受限输入框。其他原生探针使用无效的测试总线地址，避免注册到桌面用户会话。测试不访问图形界面。
