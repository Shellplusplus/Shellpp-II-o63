# Native Module 源码

> **仅支持 Xiaomi Watch S4 41mm / s441 / O63 / 3.100.028；**

本目录是 Shell++ II 的 s441 原生模块源码。唯一目标为 Xiaomi Watch S4 41mm / O63 / 3.100.028，对应 AP SHA-256 为 c1a738d70ff5284569439bbcfb1212a94f357cd81c6f715d7a7a34ef0155912a。

默认构建入口是 module/src/s441_supervisor_stage1.c。其他 native_app、native_ui 和旧 Supervisor 文件保留作调查及历史参考，不代表其他固件 ABI。

编译从同级 shellpp-ii-build 执行 ./build.sh。源码直接调用固件专用的 Native App、Activity Manager 和 Launcher 入口；地址与 descriptor 说明见 ../docs/firmware-abi.md。注册后的 descriptor 和引用数据必须位于永久可写存储，不能使用临时栈对象。
