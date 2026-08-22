# 构建工具

> **目标唯一固定为 Xiaomi Watch S4 41mm（s441/O63）固件 3.100.028。构建产物不适用于其他设备或版本。**

本目录只构建 s441/O63 3.100.028 原生模块，并生成安装器所需的图标和资源包。执行 ./build.sh。

依赖 macOS Apple Clang、可链接 ARM ELF 的 rust-lld、Python 3、Node.js 和 macOS sips；可用 CLANG、RUST_LLD、PYTHON、NODE 覆盖路径。S441_LOAD_PROBE=1 仅生成入口探针，默认 S441_LOAD_PROBE=2 生成正常模块。

脚本会验证安装器两份 Lua 和 manifest，编译并校验 ELF，生成图标，同步 shellpp_ii.bin 和图标，重建 resource.bin/hashCode。Lua 不由构建脚本修改。
