# 逆向调查记录

调查对象是已合法取得并解包的 O63 vela_ap.bin。工具通过 size/SHA-256 校验后进行只读扫描、符号提取和交叉引用分析。extract_vela_symbols.py 用于符号提取，firmware_xrefs.py 用于地址引用。

已确认：Native App 注册入口检查并复制 0x40 字节 App 记录，深拷贝关键字符串，建立内部链表并转换页面表；页面注册入口原地写入 +0x40/+0x44 链表指针；app_launcher_add(app_id) 内部完成图标插入和布局刷新。

Activity/LVX 页面创建链已经恢复，并已用于文件查看、CPU/内存显示和软重启。设备崩溃日志进一步证明 s441 modlib 对本地函数的 R_ARM_ABS32 重定位不会可靠保留 Thumb 位，因此所有交给 VFS、Activity Manager、LVX 事件系统和异步队列长期保存的模块回调都在运行时显式设置 bit 0；构建校验拒绝重新引入静态本地函数指针。

系统通知入口确认是 0x2C643DE5。进一步反汇编 0x2C643694 和 0x2C63B998 后确认：0x58 字节记录的 +0x00/+0x04 是用于查找、替换和去重的 64 位消息 UID，+0x08 是消息 ID。Shell++ 使用专用 UID 0x00CD0001；+0x0C/+0x10/+0x14 保存稳定来源、标题和正文，+0x1C/+0x20 留空使用固件默认图标，+0x48/+0x4C 分别是通知详情创建与清理回调。真机画面证明 +0x50=1 会触发尺寸不适合 466 圆屏的系统前台卡，因此当前固定为 0 并关闭 reminder listener，只保留通知中心记录。重新从函数边界解码确认 create_toast 的真实 Thumb 入口是 0x2C545E6D，而非落在前置 literal/padding 内的 0x2C545E69；其五参数 ABI 为 builder、user_data、clickable、context 和栈上传入的 style，builder 同步接收 `(root, 0)`，替换/销毁时接收 `(root, 1)`。当前 builder 使用固定 `220x52` 居中 label。

列表控件 `0x2C52E1A0` 的 TBH 分发表、菜单 helper `0x2C417E34`、圆屏两行页面 `0x2C4FD918` 以及完整 type-3 页面 `0x2C4961B8` 已从目标 AP 复核。`0x2C4FD918` 明确使用 type-8 row，初始化后不调用 set_size；`0x2C49620A..0x2C49633A` 则证明 type-3 页面在 row 之前依赖 `366x68` 和 `368x84` 等前置对象，因此不能只复制后半段并强制 row 为 `360x92`。`row_update` 的 prologue 同时证明完整 ABI 是六个总参数。当前 Shell++ 保留 type-8 row 的固件尺寸，并将 9 个页面的动态内容统一挂入标题下方 `366x300` 的可滚动 page-content；文件页每次显示四项并提供翻页，CPU/Memory 独立为统计页，About 为固定安全宽度文本页。`0x2C6C837D` 仍只作为特定 owner 的私有后处理，不作为通用 finalize。无重启 App/Launcher 反注册 ABI 仍未恢复；旧 10 Pro 地址不可迁移。
