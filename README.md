# Shell++ II s441

Created by Codex.

> **兼容性警告：本项目仅适用于 Xiaomi Watch S4 41mm（s441/O63）固件 3.100.028。其他 Watch S4 尺寸、其他机型或其他固件版本均不支持。**

Shell++ II 是 Xiaomi Watch S4 41mm（s441/O63）固件 3.100.028 专用的原生模块项目。目标 AP SHA-256：c1a738d70ff5284569439bbcfb1212a94f357cd81c6f715d7a7a34ef0155912a。

当前已在目标设备验证模块加载、Native App 注册和 Launcher 发布成功。项目使用该固件的硬编码 ABI，不兼容其他固件。

目录：shellpp-ii 为源码，shellpp-ii-build 为构建工具，shellpp-ii-installer 为 Lua 安装器和发布资源，tools 为只读固件分析工具，docs 为完整调查记录。

构建：

    cd shellpp-ii-build
    ./build.sh

构建会校验冻结的 Lua、编译模块、生成图标、同步原生资源并重建 resource.bin 与 hashCode。详细资料见 docs/README.md。
