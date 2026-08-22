# 逆向调查记录

调查对象是已合法取得并解包的 O63 vela_ap.bin。工具通过 size/SHA-256 校验后进行只读扫描、符号提取和交叉引用分析。extract_vela_symbols.py 用于符号提取，firmware_xrefs.py 用于地址引用。

已确认：Native App 注册入口检查并复制 0x40 字节 App 记录，深拷贝关键字符串，建立内部链表并转换页面表；页面注册入口原地写入 +0x40/+0x44 链表指针；app_launcher_add(app_id) 内部完成图标插入和布局刷新。

尚未完全恢复：系统通知真实提交 ABI、无重启删除 ABI、完整 Activity/LVGL page factory、图标节点公共结构及所有业务 callback 的通用签名。旧 10 Pro 地址不可迁移。字符串地址只是 literal/data 引用，不能直接当作函数入口。

