# Shell++ II Installer

s441 O63（固件 `3.100.028`）的 Shell++ II 安装表盘工程。

## Lua 文件目录

安装器 Lua 资源必须同时保留在以下两个目录，并保持内容一致：

- `_Lua/`：表盘编辑器使用的资源目录。
- `resources/_lua/_Lua/`：由 `resources/manifest.xml` 写入 `resource.bin` 的资源目录。

每个目录只能包含以下三个文件：

- `main.lua`
- `shellpp_ii.bin`
- `shellpp_ii_icon.bin`

## 所需内容

打包工程需要 `resource.bin`、`resources/manifest.xml`、`uidmap.map`、`capability.json` 和 `hashCode`。原生模块与图标由相邻 `shellpp-ii-build` 仓库执行 `./build.sh` 后更新；Lua 文件由安装器维护，构建脚本不会改写它。构建脚本会在资源同步后重建 `resource.bin` 与 `hashCode`。
