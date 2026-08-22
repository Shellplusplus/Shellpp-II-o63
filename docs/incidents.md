# 故障记录

result=15 / error=-19 对应空的 Quick App watcher/install loop，不代表 Native App 注册入口失败；0x3C2E94C0 为空，0x3C2E94B4 是 async handle。卡在 Registering module apps 通常是等待不存在的 loop/consumer 或重复提交旧队列路径。

无图标曾由错误 descriptor 资源字段、伪报 Launcher 成功，以及误用查询/删除路径造成；s441 发布入口是 0x2C412425，参数为 uint16_t app_id。删除文件不会撤销系统进程内存中的 App、Activity 和 Launcher 链表。当前已验证模块加载、Native App 注册和 Launcher 发布；系统通知 ABI、完整 Activity/LVGL 页面工厂及无重启注销仍未确认。

