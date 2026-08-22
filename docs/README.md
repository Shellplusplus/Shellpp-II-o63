# s441 开发调查

> **适用范围：Xiaomi Watch S4 41mm（s441/O63）固件 3.100.028。**

本文档记录 Shell++ II 针对 Xiaomi Watch S4 41mm / s441 / O63 / 3.100.028 的已确认结论。目标 AP SHA-256：c1a738d70ff5284569439bbcfb1212a94f357cd81c6f715d7a7a34ef0155912a。

- firmware-abi.md：固件入口、descriptor 布局和硬编码地址。
- reverse-engineering.md：符号提取、反汇编证据和未确认接口。
- integration.md：模块加载、Native App 注册和 Launcher 发布顺序。
- build-and-release.md：编译、资源同步和发布检查。
- incidents.md：设备测试故障及原因。

范围严格限于上述固件；不要将地址或 ABI 迁移到其他设备或版本。
