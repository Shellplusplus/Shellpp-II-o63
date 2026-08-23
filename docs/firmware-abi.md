# 固件 ABI

目标 AP 运行时 Thumb 入口：native_app_register_core 0x2C63B391；activitymanager_page_register 0x2C60E9E1；activitymanager_page_unregister 0x2C60D141；Activity activate 0x2C60F0F9；close 0x2C60F3AD；launcher_page_insert_icon 0x2C40EC9D；app_launcher_add(uint16_t) 0x2C412425；launcher_page_main_update_layout 0x2C414C89；register_driver 0x2C6FE05D。模块别名空间中的 0x0C63B391、0x0C412425 等不能与 AP 运行时地址混用。

App descriptor 固定 0x40 字节：+0x08 package_name，+0x0C app_path/resource，+0x10 uint16 app_id，+0x1C resolve_display_name(app*)，+0x20 可空私有槽；+0x00/+0x04、+0x30、+0x34、+0x3C 初始清零。Page descriptor 固定 0x74 字节。当前最小页面预置 +0x00 prototype=0x3C2040D0、+0x10 page_name、+0x14 page_id、+0x16 app_id、+0x29=5、+0x2C Activity API=0x3C203FF4、+0x4C create callback、+0x64 cleanup callback。+0x28、+0x2A、+0x34 lifecycle、+0x38 context、+0x40/+0x44 管理器链表和 +0x48 object API 初始为零，由 Activity Manager/prototype 链维护。组合 ID 为 (app_id << 16) | page_id。

App/Page descriptor 必须位于模块生命周期内的永久可写存储；被引用字符串必须保持稳定。0x3C2E94C0 是可能为空的 Quick App watcher/install loop；0x3C2E94B4 是 async wake handle，不是 loop。静态队列入口为 0x2C31AFD5、0x2C31B0CD；loop 为空时当前实现使用同步 VFS/AP 调用上下文。当前 build marker 为 0x53494937。

页面显示使用已验证的 LVX Thumb 入口：label_create 0x2C4C9AF1、label_set_text 0x2C4CCB91、object_align 0x2C41C6F9、set_width 0x2C41C735、set_text_color 0x2C41CA11、set_text_align 0x2C41CF71。完整顺序见 call-flows.md。
