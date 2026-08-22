# 集成与运行顺序

成功路径是：安装器加载 s441 模块，模块在可用的同步 VFS/原生上下文中注册 App descriptor，注册永久 page descriptor，最后调用 app_launcher_add(app_id) 发布 Launcher 项目。阶段顺序为 LOAD -> restore boot intents -> INSTALL 0 -> INSTALL 1 -> INSTALL 2。各阶段由 Lua timer 分开，使系统获得处理上一阶段的机会。

module_initialize 返回值和 page callback 返回值都不能当作 App 注册状态。设备重启是清理驻留注册状态的可靠边界。未确认删除 ABI 前，删除 /data/shellpp-ii 或清环境不能视为注销内存中的 App、Activity 和 Launcher 链表。

