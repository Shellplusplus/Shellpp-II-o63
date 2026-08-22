# Shell++ II

Shell++ II 的原生模块源码，当前唯一目标为 s441 O63 固件。

## 快速开始

在同一个父目录中拉取三个仓库。构建脚本依赖它们的相对位置：

```sh
git clone https://github.com/Shellplusplus/Shellpp-II-App.git shellpp-ii
git clone https://github.com/Shellplusplus/Shellpp-II-Build.git shellpp-ii-build
git clone https://github.com/Shellplusplus/Shellpp-II-install-Lua.git shellpp-ii-installer
```

构建环境需要 macOS、Apple Clang、Rust 工具链（提供 `rust-lld`）、Python 3、Node.js 和 `sips`。然后执行：

```sh
cd shellpp-ii-build
./build.sh
```

构建产物为 `out/3.100.028/shellpp_ii.bin`。默认构建的是 s441 第二阶段最小
Supervisor，只注册 `/dev/shellpp` 以验证已确认的加载 ABI；它不会执行 Lua
命令或安装原生应用。脚本会同时更新相邻 `shellpp-ii-installer` 仓库中的原生
模块和图标资源。打包器负责随后更新 `resource.bin` 与 `hashCode`。
