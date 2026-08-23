# 故障记录

result=15 / error=-19 对应空的 Quick App watcher/install loop，不代表 Native App 注册入口失败；0x3C2E94C0 为空，0x3C2E94B4 是 async handle。卡在 Registering module apps 通常是等待不存在的 loop/consumer 或重复提交旧队列路径。

无图标曾由错误 descriptor 资源字段、伪报 Launcher 成功，以及误用查询/删除路径造成；s441 发布入口是 0x2C412425，参数为 uint16_t app_id。error=-100 表示 APP_REGISTER 后按 App ID 未找到记录，-101 表示该 ID 属于其他 package；register=0 不能单独解释为失败。删除文件不会立即撤销进程内存中的 App、Activity 和 Launcher 链表，因此当前 Uninstall/Clear Env 删除专属持久文件后自动重启。当前已实现模块加载、Native App 注册、Launcher 发布和最小 LVX 页面；系统通知提交 ABI、完整业务 UI 和无重启注销仍未确认。
