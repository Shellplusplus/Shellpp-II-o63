# 集成与运行顺序

成功路径是：安装器加载 s441 模块，经 /dev/shellpp 依次发送 NOTIFY、RESTORE、INSTALL 0、INSTALL 1、INSTALL 2。模块在可用的同步 VFS/原生上下文或已初始化队列中注册永久 App/Page descriptor，最后调用 app_launcher_add(app_id) 发布 Launcher 项目。各阶段由独立 Lua timer callback 分开，使系统获得处理上一阶段的机会。

APP_REGISTER 的返回寄存器不能单独当作 App 注册状态；当前使用 APP_LOOKUP(app_id) 和 package_name 做后验确认。设备重启是清理驻留模块和注册状态的可靠边界。Uninstall/Clear Env 只删除 Shell++ 专属持久文件并重启，不尝试运行时摘除 App、Activity 或 Launcher 链表。完整调用链见 call-flows.md。
