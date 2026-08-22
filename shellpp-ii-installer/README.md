# Lua 安装器与发布资源

> **仅可安装到 Xiaomi Watch S4 41mm（s441/O63）且系统版本为 3.100.028 的设备。**

这是 s441/O63 3.100.028 专用安装器工程。Lua 负责加载模块、恢复状态，并按 LOAD -> restore -> INSTALL 0 -> INSTALL 1 -> INSTALL 2 完成原生 App 与 Launcher 注册。

_Lua 是编辑器目录，resources/_lua/_Lua 是 manifest 实际打包目录，两份 main.lua 必须字节一致。原生模块和图标由 ../shellpp-ii-build/build.sh 生成并同步，随后重建 resource.bin 和 hashCode。

Lua 安装器当前已冻结。除非用户明确要求，不修改版本检测、文件检查、日志或加载流程。
