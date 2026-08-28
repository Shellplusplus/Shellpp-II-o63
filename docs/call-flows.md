# 调用流程

> 仅适用于 Xiaomi Watch S4 41mm（s441/O63）固件 3.100.028。目标 vela_ap.bin SHA-256：c1a738d70ff5284569439bbcfb1212a94f357cd81c6f715d7a7a34ef0155912a。

本文记录当前代码实际执行的调用链。报告中的 0x2C... 是 AP 运行时 Thumb 地址；ET_REL 模块源码使用对应的 0x0C... 模块别名；0x3C... RAM 对象保持运行时地址。Lua 不直接调用固件地址，固件 ABI 调用集中在 s441_supervisor_stage1.c。

## 总体结构

```text
Lua 安装器
  -> insmod shellpp_ii.bin
  -> /dev/shellpp 控制协议
  -> Supervisor
       -> Native App registry
       -> Activity Manager / Page registry
       -> Launcher
       -> LVX 页面
```

## 1. 模块加载

```text
Run
  -> supervisor_present()
  -> /dev/shellpp 不存在时执行 insmod
  -> NuttX modlib 加载 ARM ET_REL
  -> module_initialize(modinfo)
  -> register_driver('/dev/shellpp', &fops, 0666)
     模块别名 0x0C6FE05D / 运行时 Thumb 0x2C6FE05D
  -> driver_registered = 1
  -> Lua read_status() 校验 magic、status ABI、build marker
```

当前 build marker 为 0x53494937。模块驻留 RAM，原生 UI 二进制更新后必须先重启设备再 Run；本轮按用户要求没有修改冻结的 Lua 安装器，因此没有通过更改 Lua build marker 实现热替换。

## 2. 控制协议

Lua 向 /dev/shellpp 写入 16 字节小端命令：

```text
+0x00 magic = 0x53505331
+0x04 command
+0x08 stage
+0x0C reserved = 0
```

Lua 随后读取 384 字节状态。主要状态值为 2=QUEUED、5=COMPLETED、15=FAILED。状态还包含 error、driver/app/launcher 标志、registration/launcher/queue 原始结果及队列状态。诊断区记录：`+0x60` 通知结果，`+0x64` 页面创建次数，`+0x68` 点击次数，`+0x6C` 最后操作，`+0x70` 最后页面，`+0x74` 最后结果，`+0x78` 通知请求，`+0x80/+0x84` 原生 row 创建/失败次数，`+0x88/+0x8C` 通知提交次数/消息地址，`+0x90..+0x9C` 最近 root/title/content/row，`+0xA0` reminder listener 状态，`+0xA4` toast 结果，`+0xA8/+0xAC/+0xB0` toast builder 创建/失败/root。

## 3. Run 和 INSTALL 阶段

每个命令由独立 LuaLVGL timer callback 发出：

```text
LOAD
  -> CMD_NOTIFY_LOADED
  -> CMD_RESTORE_AFTER_BOOT
  -> CMD_INSTALL 0
  -> CMD_INSTALL 1
  -> CMD_INSTALL 2
```

NOTIFY 与 INSTALL 1/2 共用 Supervisor 的 AP/UI 调度路径；当固件 App-install loop 存在时通过队列执行，O63 未建立该可选 loop 时沿同步 LuaLVGL/VFS AP UI 上下文执行。NOTIFY 只设置 notification_requested；INSTALL 1 注册 App/Page；INSTALL 2 发布 Launcher，并在同一 App/UI 事务中调用 notification_insert 和独立 create_toast。消息使用专用 UID 0x00CD0001，+0x50=0，因此只写入通知中心而不弹出会占满圆屏的前台通知卡；reminder listener 保持关闭。独立 toast 不依赖通知 host。通知中心提交失败记录 -102，toast builder 未同步创建固定标签记录 -103。RESTORE 和 INSTALL 0 当前只确认完成。

## 4. INSTALL 1：App 与 Page 注册

```text
write_device(INSTALL, 1)
  -> APP_INSTALL_LOOP(0x3C2E94C0) 为空时同步 execute_direct_request()
  -> loop 存在时 uv_async_queue_init/submit
  -> execute_native_stage(INSTALL, 1)
  -> APP_REGISTER(&app, pages, 9)
     0x0C63B391 / 0x2C63B391
  -> APP_LOOKUP(0x00CD)
     0x0C639BA9 / 0x2C639BA9
  -> 未找到：-100
  -> package 不匹配：-101
  -> 匹配：app_registered=1
```

APP_REGISTER 返回寄存器没有稳定公开语义，registration_result 仅供诊断。成功以后验 lookup 和 package_name 匹配为准。

App descriptor 为永久静态 0x40 字节，关键输入是 package_name、图标路径、uint16 App ID 0x00CD 和显示名 callback。

Page descriptor 为永久可写 0x74 字节。当前按 O63 内置模板预置 prototype=0x3C2040D0、page_name、app/page ID、+0x28=0、+0x29=5、+0x2A=0、Activity API=0x3C203FF4、+0x38/+0x48 为零、+0x4C create 和 +0x64 cleanup。注册器会补齐缺省状态与 object API；lifecycle 与管理器链表由 Activity Manager 管理。

## 5. INSTALL 2：Launcher 发布

```text
write_device(INSTALL, 2)
  -> 再次验证 App ID/package
  -> app_launcher_add(0x00CD)
     0x0C412425 / 0x2C412425
  -> launcher_published=1
  -> notification_insert(&record)
     0x0C643DE5 / 0x2C643DE5
  -> 返回非 NULL 消息对象
  -> create_toast(builder, NULL, clickable=0, NULL, style=1)
     0x0C545E6D / 0x2C545E6D
  -> builder(root, 0) 创建 220x52 居中 label
  -> 两者成功：loaded_notified=1
  -> 返回 NULL：error=-102
  -> toast 未创建：error=-103
```

app_launcher_add 内部完成图标插入、刷新和布局更新。正常路径不能再重复调用底层 insert/layout。

## 6. 页面显示和销毁

首页 combined ID 为 (0x00CD << 16) | 0，即 0x00CD0000。Activity Manager 创建并拥有 root，随后调用 Page +0x4C：

```text
home_create(page, root, start_data)
  -> page_title_create(root, "Shell++ II", mode=0, NULL, NULL)
  -> page_content_create(root, 366, 300)
  -> content 相对 title 向下 8 px，重新启用 helper 默认清除的 SCROLLABLE
  -> row_create(content)
  -> row_init(row, NULL, primary, NULL, 0, 0, type=8)
  -> event_add(row, callback, CLICKED=7, context)
  -> 第一项相对 content 顶部居中
  -> 后续项相对上一项 OUT_BOTTOM_MID +8
```

销毁时调用 +0x64 cleanup，只清除模块保存的 root/title/content/row 引用。root 与子对象由 Activity Manager 销毁。

所有页面共用 `366x300` 的滚动内容容器，操作项使用 O63 固件原生 type-8 row；应用不覆盖 row_init/row_update 计算出的宽高。页面层级为 `Home -> Files/System`、`System -> Performance/Device`、`Performance -> CPU/Memory`、`Device -> About/Soft reboot`。目录页一次创建四个文件/目录条目和 Previous/Next 两个翻页行，超出 300 px 的内容由容器滚动；隐藏项不参与当前可见链的相对定位。文件正文使用 `332x120` label，CPU 与 Memory 各自使用 `332x64` 固定统计 label 和 Refresh 行，About 使用 `332x206` 左对齐 label。动态文本只引用模块永久缓冲区或静态字符串。

通知发布时，Supervisor 先写入带 Thumb bit 的 `+0x48 open_detail(message, root)` 与 `+0x4C close_detail(message, context)`，再以 `foreground_alert=0` 调用 notification_insert。详情页由通知宿主管理 root，内部使用圆屏标题和原生 “Open Shell++ II” row。随后调用五参数 create_toast；toast builder 在固件传入的 root 内创建 `220x52` 的居中文本标签，销毁回调只清除模块引用。

## 7. Uninstall 和 Clear Env

当前不调用运行时 App/Launcher 反注册 ABI。两个按钮删除 Shell++ 专属持久文件并逐项验证，然后重启，由 NuttX 清除 RAM 中的模块、App、Page 和 Launcher。Clear Env 保留二次确认。

```text
/data/shellpp-ii
/data/shellpp-ii-supervisor.log
/data/term_out.txt
/data/h69_dbg.txt
/data/h71_obj.txt
/data/h72_res.txt
/data/h74_read.txt
```

不会删除 /data/log、/data/offlinelog、/data/apps.json 等共享数据。删除失败或仍有路径存在时，不报告成功。

## 8. 构建和打包

```text
build.sh
  -> 读取 targets/s441-o63.env
  -> 校验两份 Lua、冻结 SHA-256 和 manifest
  -> clang 编译 Supervisor
  -> ld.lld 生成 ARM ET_REL shellpp_ii.bin
  -> verify_elf.py 校验入口、section、relocation、BSS 和地址
  -> sips + make_icon_bin.js 生成图标
  -> 同步编辑目录和实际打包目录
  -> repack_resource.py 更新 resource.bin 和 hashCode
```

## 9. 错误解释

```text
-19   App-install queue 不可用
-22   命令或参数无效
-95   Supervisor 运行时卸载被拒绝；当前 Lua 不发送该命令
-100  APP_REGISTER 后按 App ID 未查到记录
-101  App ID 0x00CD 属于其他 package
-102  notification_insert 返回 NULL
15    pending_state=FAILED，需结合 error/register/launcher/queue
```
-100可能是设备问题，恢复出厂大概率会解决问题。
register=0 不能单独解释为注册失败；launcher=0 也必须结合当前阶段和 launcher_published 判断。
-103  前台 toast builder 未成功创建固定标签
