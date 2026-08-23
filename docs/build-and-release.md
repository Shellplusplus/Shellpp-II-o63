# 构建与发布

进入 shellpp-ii-build 执行 ./build.sh。脚本先校验两份冻结 Lua 和 manifest，再编译 s441_supervisor_stage1.c，生成 ARM relocatable ELF，运行 verify_elf.py，生成 Launcher icon，同步两个 Lua 资源目录，最后调用 repack_resource.py 重建 resource.bin 与 hashCode。

构建前会检查 manifest 仅包含 main.lua、shellpp_ii.bin、shellpp_ii_icon.bin，并检查两份 Lua 字节一致且匹配冻结 SHA-256。发布前应确认 AP SHA-256、ELF 校验、资源同步、resource.bin/hashCode 更新和设备冷启动后的 Native App/Launcher 状态。out、符号 JSON/H 和固件副本不是发布资源。逐步调用顺序见 call-flows.md。
