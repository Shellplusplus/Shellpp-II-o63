# 固件 ABI

目标 AP 运行时 Thumb 入口：native_app_register_core 0x2C63B391；activitymanager_page_register 0x2C60E9E1；activitymanager_page_unregister 0x2C60D141；Activity activate 0x2C60F0F9；close 0x2C60F3AD；launcher_page_insert_icon 0x2C40EC9D；app_launcher_add(uint16_t) 0x2C412425；launcher_page_main_update_layout 0x2C414C89；register_driver 0x2C6FE05D。模块别名空间中的 0x0C63B391、0x0C412425 等不能与 AP 运行时地址混用。

App descriptor 固定 0x40 字节：+0x08 package_name，+0x0C app_path/resource，+0x10 uint16 app_id，+0x1C resolve_display_name(app*)，+0x20 可空私有槽；+0x30、+0x34、+0x3C 初始清零。Page descriptor 固定 0x74 字节：+0x00 prototype=0x3C2040D0，+0x10 page_name，+0x14 page_id，+0x16 app_id，+0x28=4，+0x29=4，+0x2A=2，+0x2C Activity API=0x3C203FF4，+0x34 lifecycle，+0x38 default context=0x3C2FE2D0，+0x40/+0x44 为管理器链表，+0x48 object API=0x3C2040D0。生命周期为 lifecycle(page,1,0) 和 lifecycle(page,2,0)，组合 ID 为 (app_id << 16) | page_id。

所有 descriptor 和引用数据必须为永久可写存储。0x3C2E94C0 是实测为空的 Quick App watcher/install loop；0x3C2E94B4 是 async wake handle，不是 loop。静态队列入口为 0x2C31AFD5、0x2C31B0CD，但当前成功路径使用同步 VFS/AP 上下文。

