# Shell++ II Build

构建 Shell++ II 的 ARM NuttX 原生模块，并更新同级安装包仓库中的生成资源。

## 用法

将本仓库与 `shellpp-ii`、`shellpp-ii-installer` 放在同一目录。需要 macOS、Apple Clang、Rust 工具链（`rust-lld`）、Python 3、Node.js 和 `sips`。

```sh
./build.sh
```

默认构建目标为 s441 O63 固件 `3.100.028`，并生成第二阶段最小
`/dev/shellpp` Supervisor。它只验证已经反汇编确认的模块入口和
`register_driver` ABI；尚未实现 Lua 控制命令、原生应用或卸载。

```sh
# 仅在需要重新验证 ET_REL 入口 ABI 时使用。它会替换安装器中的模块。
S441_LOAD_PROBE=1 ./build.sh

# 默认值；生成最小 Supervisor。
S441_LOAD_PROBE=2 ./build.sh
```

- `out/3.100.028/shellpp_ii.bin`：已校验的原生模块。
- `../shellpp-ii-installer/_Lua/`：编辑器使用的模块和图标。
- `../shellpp-ii-installer/resources/_lua/_Lua/`：写入 `resource.bin` 的打包资源。

构建会在同步资源后重新生成安装器的 `resource.bin` 与 `hashCode`，因此该目录
中的安装器工程可直接交给既有表盘安装流程。

可通过 `CLANG`、`RUST_LLD`、`PYTHON` 和 `NODE` 环境变量覆盖工具路径。
