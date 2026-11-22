# 测试

Rust 测试覆盖查询、会话、学习、五笔与 ABI；Python 测试覆盖配置、数据管理、设置与源码清单；native/ 为隔离 Fcitx5/C 探针。fixtures/ 是合成小词典，不代表生产词频或准确率。运行方式见 [BUILD](../docs/BUILD.md)。测试必须使用临时 XDG 目录，不读取个人模型、日志或配置；真实模型测试需单独明确配置。

`test_translate.py` 使用合成回环 HTTP 和内存密钥环替身，不读取真实 Key 或桌面密钥环。`native/translate.cpp` 通过 `fake-translate.py` 验证异步请求、候选确认、选区暂缓行为及焦点生命周期；隔离启动器强制使用假翻译进程。真实外部 API 必须另行显式测试，不能将这些探针记作公网 API 或完整桌面验收。
