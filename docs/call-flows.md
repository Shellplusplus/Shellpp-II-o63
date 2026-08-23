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

当前 build marker 为 0x53494937。旧 Supervisor 驻留时必须拒绝继续执行并要求重启。

## 2. 控制协议

Lua 向 /dev/shellpp 写入 16 字节小端命令：

```text
+0x00 magic = 0x53505331
+0x04 command
+0x08 stage
+0x0C reserved = 0
```

Lua 随后读取 384 字节状态。主要状态值为 2=QUEUED、5=COMPLETED、15=FAILED。状态还包含 error、driver/app/launcher 标志、registration/launcher/queue 原始结果及队列状态。

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

NOTIFY 当前设置 loaded_notified；RESTORE 和 INSTALL 0 当前只确认完成；INSTALL 1 注册 App/Page；INSTALL 2 发布 Launcher。

## 4. INSTALL 1：App 与 Page 注册

```text
write_device(INSTALL, 1)
  -> APP_INSTALL_LOOP(0x3C2E94C0) 为空时同步 execute_direct_request()
  -> loop 存在时 uv_async_queue_init/submit
  -> execute_native_stage(INSTALL, 1)
  -> APP_REGISTER(&app, pages, 1)
     0x0C63B391 / 0x2C63B391
  -> APP_LOOKUP(0x00CD)
     0x0C639BA9 / 0x2C639BA9
  -> 未找到：-100
  -> package 不匹配：-101
  -> 匹配：app_registered=1
```

APP_REGISTER 返回寄存器没有稳定公开语义，registration_result 仅供诊断。成功以后验 lookup 和 package_name 匹配为准。

App descriptor 为永久静态 0x40 字节，关键输入是 package_name、图标路径、uint16 App ID 0x00CD 和显示名 callback。

Page descriptor 为永久可写 0x74 字节。当前只预置 prototype=0x3C2040D0、page_name、app/page ID、+0x29=5、Activity API=0x3C203FF4、+0x4C create 和 +0x64 cleanup。+0x28、+0x2A、lifecycle、context、管理器链表和 object API 初始为零，由 Activity Manager/prototype 链维护。

## 5. INSTALL 2：Launcher 发布

```text
write_device(INSTALL, 2)
  -> 再次验证 App ID/package
  -> app_launcher_add(0x00CD)
     0x0C412425 / 0x2C412425
  -> launcher_published=1
```

app_launcher_add 内部完成图标插入、刷新和布局更新。正常路径不能再重复调用底层 insert/layout。

## 6. 页面显示和销毁

首页 combined ID 为 (0x00CD << 16) | 0，即 0x00CD0000。Activity Manager 创建并拥有 root，随后调用 Page +0x4C：

```text
shellpp_home_create(page, root, start_data)
  -> label_create             0x2C4C9AF1
  -> set_width               0x2C41C735
  -> set_text_color          0x2C41CA11
  -> set_text_align          0x2C41CF71
  -> object_align            0x2C41C6F9
  -> label_set_text          0x2C4CCB91
```

销毁时调用 +0x64 shellpp_home_cleanup，只清除模块保存的 root/label 引用。root 由 Activity Manager 销毁。

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
15    pending_state=FAILED，需结合 error/register/launcher/queue
```

register=0 不能单独解释为注册失败；launcher=0 也必须结合当前阶段和 launcher_published 判断。
