# 5. 危险模式、静默成功假实现，以及 fbsyn 事故机制复盘

基线：`5f93c2d`。

## 5.1 判定口径

- **假实现（静默成功）**：函数契约是"执行操作并返回状态"，但函数体忽略全部参数、不做任何检查，直接返回成功值（0）。用户态/调用方无法从返回码区分"未实现"与"成功"。
- **失败当成功**：分配/映射/校验失败时返回的 0 与成功路径同值，或不区分错误口径。
- **忽略错误码**：初始化/系统调用返回值未检查。本节只列有代码证据的样本，不逐条定性为缺陷；尽力初始化（best-effort boot）与应查未查分开标注。

## 5.2 64 位系统调用空桩（返回 0 = 成功，已确认）

定义集中在 `kernel/arch/x86_64/syscall64_full.c`；包装层在 `:2608-2700` 段，多数直接把参数 `(void)` 掉后调空桩。

| syscall 桩 | 行号 | 函数体 | 风险 |
|------------|------|--------|------|
| `sys_shutdown` | `:795` | 忽略 fd/how，`return 0` | 进程以为关机/断连成功，实际未执行 |
| `sys_bind` | `:832` | 忽略 s/addr/len，`return 0` | 绑定未发生，后续 listen/connect 行为不可预期 |
| `sys_listen` | `:833` | 同上 | 同上 |
| `sys_getsockname` | `:834` | 同上 | 不写任何地址输出 |
| `sys_getpeername` | `:835` | 同上 | 同上 |
| `sys_link` | `:943` | 忽略 oldp/newp，`return 0` | 硬链接未建立，返回成功 |
| `sys_vfork` | `:961` | `return 0`（无子进程返回） | 调用方以为子进程已创建 |
| `sys_mlock` | `:1059` | 忽略 addr/len，`return 0` | 内存驻留未生效 |
| `sys_madvise` | `:1060` | 忽略 addr/len/advice，`return 0` | 建议未生效 |
| `sys_set_robust_list` | `:1187` | `return 0` | 健壮互斥锁列表未设置 |
| `sys_rseq` | `:1188` | `return 0` | rseq 未注册 |
| `sys_pselect6` | `:1254` | `return 0` | 不等待任何 fd |
| `sys_epoll_create` | `:1257` | 忽略 flags，`return 0` | 返回的 fd 无对应 epoll 实例 |
| `sys_epoll_ctl` | `:1258` | `return 0` | 注册/修改/删除均未生效 |
| `sys_epoll_wait` | `:1259` | `return 0` | 永远报 0 个就绪事件 |
| `sys_eventfd2` | `:1262` | `return 0` | 事件 fd 未创建 |
| `sys_sigprocmask` | `:1367` | `return 0` | 信号掩码未改动 |
| `sys_mremap` | `:1403` | `return 0`；包装 `:2651-2652` 忽略全部参数 | 地址未迁移，返回成功 |
| `sys_prlimit64` | `:1404` | `return 0`；包装 `:2654-2656` 忽略参数 | 资源限制未设置 |
| `sys_readlink` | `:1431-1434` | 忽略 path/buf/size，`return 0` | 不写缓冲，调用方读到旧内容 |
| `sys_fcntl` | `:1437` | `return 0`；包装 `:2663-2665` 忽略参数 | fd 标志未改 |
| `sys_clock_nanosleep` | `:1448` | `return 0`；包装 `:2669-2671` 忽略参数 | 立即返回，未睡眠 |

补充：包装层 `wrap_mremap`（`:2651`）、`wrap_prlimit64`（`:2654`）、`wrap_fcntl`（`:2663`）、`wrap_clock_nanosleep`（`:2669`）把六个参数全部 `(void)`，即使底层桩以后实现，参数也传不进去。

## 5.3 32 位系统调用空桩（已确认）

`kernel/arch/x86/sys_fs.c`：

| 桩 | 行号 | 函数体 |
|----|------|--------|
| `sys_chmod` | `:274-278` | 忽略全部参数，`return 0`，注释"原型阶段" |
| `sys_chown` | `:280-284` | 同上 |
| `sys_mount` | `:286-290` | 同上 |
| `sys_umount` | `:292-296` | 同上 |
| `sys_ioctl` | `:298-302` | 同上 |
| `sys_chdir` | `:320-324` | 同上，工作目录未切换 |
| `sys_fcntl` | `:338-342` | 同上 |

`kernel/arch/x86/sys_fb.c`（失败口径与成功同值）：

- `sys_fb_acquire` `:57`：`if (!phys) return 0;`
- `:60`：`if (!virt) return 0;`——映射失败返回 0；32 位返回口径里 0 同时被"无 fb/失败"使用，调用方难区分。

## 5.4 认证 / 安全启动假实现（已确认，最高优先级）

### 5.4.1 32 位登录/解锁忽略密码

| 函数 | 行号 | 行为 |
|------|------|------|
| `login_authenticate` | `kernel/arch/x86/login.c:76-91` | `(void)password;`；只要用户名存在即 `return 0`（认证通过）；默认 `auto_login=1`（`:29`）、`require_password=0`（`:32`） |
| `login_unlock_screen` | `login.c:151-168` | `(void)password;`；任意口令解锁 |
| `mu_private_unlock` | `kernel/arch/x86/multi_user.c:230-232` | `(void)password;`，私钥保护解锁不校验 |

`login_authenticate` 被登录路径调用：`login.c:291`。即 32 位登录界面接的是这个空桩。

### 5.4.2 安全启动验签/验哈希为空桩，且未接启动链路

`kernel/arch/x86/secure_boot.c`：

| 函数 | 行号 | 行为 |
|------|------|------|
| `sb_verify_stage` | `:49-63` | `:56` 硬编码 `SB_VERIFY_OK`，注释"简化: 所有阶段验证通过"；`result` 不可能不等于 OK，`boot_failed` 分支不可达 |
| `sb_verify_signature` | `:85-92` | `(void)data; (void)size; (void)signature;`；只查 key slot 是否存在/未吊销，**不验证签名**；key 在位即返回 0 |
| `sb_verify_hash` | `:94-99` | 忽略 data/size/expected_hash/algo，无条件 `return 0`（哈希匹配） |
| `sb_verify_certificate` | `:101-106` | 只查证书槽位 trusted/revoked |

调用点核查：`sb_verify_stage` / `sb_verify_signature` / `sb_verify_hash` / `sb_verify_certificate` / `sb_start_boot` / `sb_complete_boot` 在 `kernel/` 内除本文件外**无调用方**（grep 无命中）。当前是休眠框架 + 空验证两层叠加：即使将来接线，现有实现也会对任意输入放行。

## 5.5 映射/分配失败处理复核（未发现"随机页当成功"）

- 64 位匿名 mmap：`kernel/arch/x86_64/fs_mm_sys.c:182-211`，逐页分配失败走 `goto fail` 回滚已映射页；`:207-208` `phys == 0` 判失败。
- `MAP_PHYS` 路径 `:117-127`：半途失败撤页；但**未校验物理地址范围是否为可用 RAM/MMIO**，任意物理地址都会被建表（设计用途是帧缓冲映射，风险是可映射未授权物理内存）——这是缺校验，不是分配失败被当成功。
- 32 位 `sys_mem.c:512-585` B2 探针读回 0x100000 映像页校验；`sys_fs.c:309-313` 有用户指针校验失败返回 `-1` 的反例（与空桩并存）。

## 5.6 忽略错误码的初始化调用点（样本，已确认未判返回值）

`kernel/kernel_main_64.c`（多数标注"尽力初始化"，但返回值未存、未记失败）：

| 行号 | 调用 |
|------|------|
| `:1124` | `ahci64_init()` |
| `:1138` | `uart64_init(0)` |
| `:1143` | `ac97_init()` |
| `:1148` | `hsipc_init()` |
| `:1153` | `hs_services_init()` |
| `:1161-1164` | `hs_lock_init/animation_init/theme_engine_init/plugin_init` |
| `:1170` | `reg64_init()` |
| `:1172` | `gps64_init()` |
| `:1174` | `l10n64_init()` |
| `:1176` | `drv_mgr64_init()` |
| `:1213` | `net_hal_init()` |
| `:1217` | `socket64_init()` |
| `:1221` | `font64_init()`；`:1225` `window64_init()`；`:1229` `fm64_init()`；`:1233` `syssettings64_init()`；`:1237` `notes64_init()`；`:1241` `term64_init()`；`:1245` `appstore64_init()`；`:1249` `rtc64_init()` |

对照：网卡初始化是少数判返回值的路径（`:1187-1188` `rtl8139_init()/e1000_init()` 返回 0 判定）。

## 5.7 fbsyn 事故机制复盘（代码层面，非管理原因）

事故结论来自提交 `57aba61`（"加载屏留到桌面 + 拔掉开机自检的程序把屏幕刷成绿红"）与当前代码交叉核对。fbsyn 程序：`user/apps/fbsyn.c`；自启点：`kernel/kernel_main_64.c:918-927`（5f93c2d 上已 `if (0)` 关闭）。

### 5.7.1 为什么花屏会长期留在开机画面上

1. **开机自动 exec 通道不回收探针副作用**：电池/autorun 只以 `user_process_exec` 返回 `<0` 判断"程序不在/启动失败"（`kernel_main_64.c:904-1079`、`:1339-1369`）；程序跑完后画了什么、是否改写显存、输出判据是什么，内核不做回读断言。"跑起来了"即视为通道正常。
2. **探针直写帧缓冲，痕迹留在 VRAM 而非内核内存**：`fbsyn.c:86-90` 经 acquire 映射整屏写左半纯绿 `0x0000FF00`、右半纯红 `0x00FF0000`；`:103` 忙等 2 秒后 release。`wrap_fb_release`（`syscall64_full.c:2620-2628`）只清 owner 簿记（`desk_fb_owner_release`），**不清屏、不恢复任何像素**。画面残留在显存，等桌面第一帧覆盖。
3. **内核侧填充探针抓不到用户态写入**：提交记录说明，内核所有大块填充都加了一次/计数式探针仍无命中；最终靠 monitor 轮询显存 + `info registers` 抓到 `RIP=0x400003f0, CPL=3`（用户态），`R9=0x1e00`（1920 宽 pitch）才定位到程序。污染来源在用户态地址/上下文内，内核内存探针天然不可见。
4. **垫底清屏把症状遮掉一半**：`gfx64.c:1516` 深蓝 `gfx64_clear(0x000080)` 在留屏分支之外会先垫底；提交说明记录"被深蓝垫底盖掉一半，只剩 2 秒，一直没被认出来"。症状从 6 秒缩到 2 秒，被误判为首帧技术债。
5. **自检程序与发布启动同一路径，没有清单隔离**：电池程序和产品启动共用 `kernel_main_64.c` 同一段 exec 序列；`HEPU_BOOT_SELFTEST` 宏是后补的开关（`:880-894` 注释承认"约定没落地"），fbsyn 还需要额外一道 `if (0)`（`:918`）才真正停跑。
6. **电池汇总只统计异常计数，不看视觉/输出判据**：`:1084-1095` 汇总只累加 `g_user_exception_count` 中的非预期异常；探针把屏幕画花不属于异常，不进入任何计数。
7. **退出后无显存回读/断言**：release 路径与桌面首帧之间没有"显存应等于启动底色"的校验步骤；任何用户态残留都依赖桌面首帧碰巧整屏覆盖。

### 5.7.2 同类残留仍在当前代码中的位置

- `desktop64.c:296-314`：桌面主循环每 5 帧跑一次 `sb08_fpu_probe()`，每 200 轮打印 ok/fail；注释 `:295` 自述"验收结束后可以删掉"。
- `kernel_main_64.c:1339-1342`：autorun 无条件跑 `hello/sec_test/clone_test/ps`；`sec_test` 在电池与 autorun 重复执行。
- `shell64.c:3464-3488`：文本兜底路径连跑 `hello/sec_test/forktest/comptest`，`comptest` 返回值未判（`:3488`）。
- 电池内 19 个 ELF 在 `BOOT_SELFTEST=1` 构建下仍会整批自动执行（报告 01 §1.2）。

## 5.8 待验证项

1. 上述空桩 syscall 是否有用户态程序实际调用并依赖返回成功：需对照 `user/apps/` 调用点与 syscall 号表，跑一次回归看是否有静默失效的功能。
2. 64 位 `MAP_PHYS` 建表路径是否可被用户态 syscall 触发（帧缓冲映射是否只对 owner 开放）：需读 `wrap_fb_acquire` 与 mmap `MAP_PHYS` 的权限检查链确认。
3. fbsyn 关闭后，release→桌面首帧之间的显存残留窗口在当前留屏 magic 流程下是否还会暴露：需 QEMU 录制 boot 全流程复核。
