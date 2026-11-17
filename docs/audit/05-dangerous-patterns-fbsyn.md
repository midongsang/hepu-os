# 5. 危险模式、静默成功假实现，以及 fbsyn 事故机制复盘

- 审计基线：`5f93c2d`；行号为该基线编号。
- main 复核状态：基线之后 main 已前进到 `61c53fa`。本报告在相关条目后标注 main 上的修复状态与提交哈希：
  - `0202461` 关掉两处常驻验收探针（桌面循环 SB-08、文本 shell autostart 四程序）；
  - `0de2133` 32 位三处认证假实现改为显式告警/声明未实现；
  - `3d0bd23` 安全启动三处空桩改为返回 `SB_ERR_NOT_IMPLEMENTED(-2)`；
  - `9e8f2a3` 64 位登录页三分支认证，经 `261a027`、合并提交 `a787a01` 合入 main；`10e42fa` 修正 OOBE/锁屏优先级；`53e53cb`、`61c53fa` 只动 l10n 兜底与 bar_height 默认值，未触及认证链。
  - 未被上述提交触及的条目，状态仍为基线结论。

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

## 5.4 认证 / 安全启动假实现

### 5.4.1 32 位登录/解锁忽略密码

基线（`5f93c2d`）行为：

| 函数 | 基线行号 | 基线行为 |
|------|----------|----------|
| `login_authenticate` | `kernel/arch/x86/login.c:76-91` | `(void)password;`；只要用户名存在即 `return 0`（认证通过）；默认 `auto_login=1`（`:29`）、`require_password=0`（`:32`） |
| `login_unlock_screen` | `login.c:151-168` | `(void)password;`；任意口令解锁 |
| `mu_private_unlock` | `kernel/arch/x86/multi_user.c:230-232` | `(void)password;`，私密空间解锁不校验 |

`login_authenticate` 被登录路径调用：`login.c:305`；`login_unlock_screen` 被解锁路径调用：`login.c:319`。

**main 状态（复核于 `61c53fa`，审计在当前树上读码复核，非合入实现）：仍未接线。三处保持 `0de2133` 的"显式告警 + 返回放行"状态；`3d0bd23..61c53fa` 区间无提交触及 `login.c` / `multi_user.c`。**
- 改动性质是"诚实化"而非"实现"：三处仍保留"未设密码即放行"的既有行为（32 位冒烟口径 `desk=1` 依赖它），但每次调用打一行 `[LOGIN]/[MULTI_USER] WARNING: ... NOT implemented`，不再静默假装成功。
- 61c53fa 行号：`login.c:85`（`login_authenticate`，`(void)password` `:87`，告警 `:88`，用户存在即 `return 0` `:100`）、`login.c:164`（`login_unlock_screen`，`(void)password` `:166`，告警 `:167`）、`multi_user.c:232`（`mu_private_unlock`，`(void)password` `:234`，告警 `:235`，`return 0` `:236`）。
- 并行存在但未接到桌面 UI 的内核 syscall 路径：`sys_user.c:103` `sys_verify_password` 做 sha256 真比对（`hash_password` `:118` + `memcmp` 32 字节 `:119`；未设密码 `:112-113` 放行）；仅被 `sys_switch_user` `:136`、`sys_unlock_user` `:229` 内部调用；syscall 号 213（`include/syscall.h:163`，表项 `syscall.c:233`）。`user/apps/` grep 无该号调用方（**待验证**：是否有运行时程序经内联 syscall 触发）。桌面登录/解锁 UI（本节三处）未接此路径。
- 缺口根因（提交说明）：32 位 `mu_user_t` 结构里没有密码/hash 字段；`sys_user.c` 的 `password_hashes[16][32]` 仅内存态、无持久化。硬接校验只会变成另一种伪装。真要实现，需先给用户结构加 hash 字段 + 持久化，属独立工程。
- 严重度：不再是"静默假成功"（调用方/用户能看到未实现告警），但**桌面 UI 认证仍然不存在**，任意口令都能进入；未达到可接受状态。

### 5.4.2 64 位登录/锁屏：三分支校验已接线，含绕过分支

> **归属说明（谁在什么时候接的线）**：三分支认证是 main 既有提交合入的实现，不是审计改动。实现提交 `9e8f2a3`（feat(login)，父提交 `f3b3c78`）→ 经 `dec60c9`、合并提交 `261a027`（merge oobe）→ 由合并提交 `a787a01` 合入 main。本节行号与"仍在原位/可绕过"的判断，是审计在 `61c53fa` 工作树上 grep/读码复核所得；`53e53cb`、`61c53fa` 未触及该认证链。

状态机：`g_locked` 0=桌面 / 1=锁屏 / 2=登录（`hpdesk.c:4578`）；锁屏→登录 `:5740-5741`、`:7324-7325`；ESC 回锁屏 `:7335-7336`。

| 分支 | 证据（61c53fa） | 行为 |
|------|----------------|------|
| ① 未配置账户 | `hpdesk.c:4628-4631` `!auth64_is_configured()` → `return 1` | 放行进桌面，保证干净 rootfs 不被锁死；`is_configured` = `g_role != ROLE_NONE`（`auth64.c:316`），账户配置读失败/缺失时落入此分支 |
| ② 已配置校验通过 | `hpdesk.c:4632-4635` `auth64_verify_password(g_password)` → `return 1`；`auth64.c:321-327` sha256 比对 | `hs_login_unlock` 置 `g_locked=0`（`hpdesk.c:4645-4652`，`:4647`） |
| ③ 校验失败 | `hpdesk.c:4636-4637` → `return 0` | `hs_login_reject` 留 `g_locked=2`，清密码、置错误提示（`:4655-4659`），不退回锁屏 |

两个提交入口都接了同一校验：登录按钮点击 `hpdesk.c:5863-5868`；回车 `:7341-7346`。

**已确认的绕过面（接线后残留）：**
- 锁屏关闭开关：`hepu.lock=0` cmdline 或 `/system.conf` 含 `lock=0` → `lock_cfg_disabled()` 返 1（`hpdesk.c:4587-4601`），开机 `g_locked=0` 直进桌面（`:7624-7627`），整段锁屏/登录不出现；能改启动参数或 system.conf 即可绕过。
- 未配置账户分支（上表①）：配置缺失/损坏时降级为放行。
- 32 位编译分支：`hpdesk.c:4638-4640` `#else` 直接 `return 1` 解锁（32 位无 auth64，避免链接断链）。
- `auth64_deelevate`（`auth64.c:364`）回退角色不要求密码。

**旧结论作废**：基于 `5f93c2d` 与 `3d0bd23` 的"64 位登录/锁屏未接密码校验、verify 只被 elevate 调用"结论在 `9e8f2a3`（经 `261a027`、`a787a01` 合入 main）后已过期；当前为**已接线但可绕过**。

### 5.4.3 安全启动验签/验哈希为空桩，且未接启动链路

基线（`5f93c2d`）行为，`kernel/arch/x86/secure_boot.c`：

| 函数 | 基线行号 | 基线行为 |
|------|----------|----------|
| `sb_verify_stage` | `:49-63` | `:56` 硬编码 `SB_VERIFY_OK`，注释"简化: 所有阶段验证通过"；`result` 不可能不等于 OK，`boot_failed` 分支不可达 |
| `sb_verify_signature` | `:85-92` | `(void)data; (void)size; (void)signature;`；只查 key slot 是否存在/未吊销，**不验证签名**；key 在位即返回 0 |
| `sb_verify_hash` | `:94-99` | 忽略 data/size/expected_hash/algo，无条件 `return 0`（哈希匹配） |
| `sb_verify_certificate` | `:101-106` | 只查证书槽位 trusted/revoked |

调用点核查：`sb_verify_stage` / `sb_verify_signature` / `sb_verify_hash` / `sb_verify_certificate` / `sb_start_boot` / `sb_complete_boot` 在 `kernel/` 内除本文件外**无调用方**（grep 无命中）。基线状态是休眠框架 + 空验证两层叠加：即使将来接线，现有实现也会对任意输入放行。

**main 状态（`3d0bd23` 改为显式失败；复核于 `61c53fa`：`3d0bd23..61c53fa` 区间 `secure_boot.c` 无提交触及）：三处空桩显式返回未实现，真实验签/验哈希仍未做。**
- 新增 `#define SB_ERR_NOT_IMPLEMENTED (-2)`（`secure_boot.c:49`），"接口存在但算法未实现，绝不返回 0 冒充通过"。
- `sb_verify_signature`（main `:93`）、`sb_verify_hash`（main `:105`）：打 `[SECBOOT] WARNING` 后返回 `SB_ERR_NOT_IMPLEMENTED`（`:102`、`:111`），不再返回 0 冒充通过。
- `sb_verify_stage`（main `:51`）：度量（`sb_measure`）仍真做；非严格模式警告后继续启动（返回 0，不改变现有启动行为），严格模式置 `boot_failed=1` 并返回 `-2`（`:62-68`）。
- 缺口仍在：模块没有任何签名/哈希算法实现；`sb_verify_certificate` 未在本次改动范围内。框架目前仍无外部调用方，接上链路前必须先补真实验证。

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

### 5.7.2 探针/测试程序在启动路径上的状态（含 main 复核）

**已关闭（`0202461`，复核于 `3d0bd23`）：**

- 桌面主循环常驻浮点探针：基线 `desktop64.c:296-314` 每 5 帧跑一次 `sb08_fpu_probe()`、每 200 轮打印 ok/fail；main 改为 `if (0)` 默认关闭（`desktop64.c:298`），复核 SB-08 时把 0 改 1。
- 文本 shell 兜底 autostart：基线 `shell64.c:3464-3488` 每次进 shell 连跑 `hello/sec_test/forktest/comptest`；main 改为 `const int autostart_selftest = 0; if (autostart_selftest) { … }`（`shell64.c:3463-3464`），程序本体与命令保留，需要时手动执行。

**仍在启动路径上（基线与 main `3d0bd23` 均未关闭）：**

- `kernel_main_64.c:1339-1342`：autorun 清单无条件跑 `hello/sec_test/clone_test/ps`；`sec_test` 在电池与 autorun 重复执行。
- `kernel_main_64.c:1365`：`/bin/no_such_uproc.elf` noent 取证探针；`:1369`：`/bin/comptest.elf` argv 取证探针。

**编译期开关、默认关闭（非默认运行）：**

- 自检电池内 19 个 ELF（fbdesc/fbsyn/statprobe/fdstress/mmprobe/rwprobe/sec_test/hello_linux/dynhello/dynhello2/hello_static/crash_jump/glibc_test/hello2-4/hello_c2/pie_probe/linux_pie_demo/forktest）整段包在 `#if defined(HEPU_BOOT_SELFTEST) && HEPU_BOOT_SELFTEST` 内；宏只在 `make BOOT_SELFTEST=1` 时由 `kernel/arch/x86_64/Makefile:230-232` 定义，默认不定义。quiet 实例串口日志实测 `PIEPROBE/FBSYN/FBDESC/STATP` 命中数 = 0。
- 注意：开关默认值只在 Makefile 里，源码注释又写"默认关"，两处来源分散、容易被误读为默认开；建议在 Makefile 该行补注释说明默认值来源（不影响代码行为）。

**不属于持续负载、不列入待修：**

- `kernel_main_64.c:1374-1378` 的 BSP 浮点自检只在启动期跑一次，是 SMP/FXSAVE 可用证据；AP 侧由 kstat 探针并发对照。`0202461` 明确保留这一处，不构成常驻负载。

## 5.8 待验证项

1. 上述空桩 syscall 是否有用户态程序实际调用并依赖返回成功：需对照 `user/apps/` 调用点与 syscall 号表，跑一次回归看是否有静默失效的功能。
2. 64 位 `MAP_PHYS` 建表路径是否可被用户态 syscall 触发（帧缓冲映射是否只对 owner 开放）：需读 `wrap_fb_acquire` 与 mmap `MAP_PHYS` 的权限检查链确认。
3. fbsyn 关闭后，release→桌面首帧之间的显存残留窗口在当前留屏 magic 流程下是否还会暴露：需 QEMU 录制 boot 全流程复核。

## 5.9 必须马上修（按严重度，含 main `61c53fa` 复核状态）

| 序 | 问题 | 基线证据 | main 状态（61c53fa） | 剩余动作 |
|----|------|----------|-----------|----------|
| 1 | **32 位桌面认证无真实校验**：登录/解锁/私密空间三处忽略密码，任意口令进入 | `login.c:76-91,151-168`；`multi_user.c:230-232` | **仍未接线**：`0de2133` 改为显式告警/声明未实现，返回仍放行；`3d0bd23..61c53fa` 无改动（`login.c:85-101,164-170`；`multi_user.c:232-237`）。内核另有 syscall 级 `sys_verify_password`（`sys_user.c:103-126`）未接桌面 UI | 给 `mu_user_t` 加 hash 字段 + 持久化，再把桌面 UI 接到真校验；在此之前告警必须保留，不得回退成静默 return 0 |
| 2 | **安全启动无真实验签/验哈希**：空桩曾恒返回通过 | `secure_boot.c:49-99` | **已改为显式失败（`3d0bd23`）**：签名/哈希返回 `-2`，stage 严格模式返回 `-2`；算法仍未实现 | 接入真实签名/哈希算法；`sb_verify_certificate` 同样待核；接线前不得认为有启动完整性保护 |
| 3 | **64 位登录已接线，但残留绕过面**：三分支校验由 `9e8f2a3` 接入（经 `261a027`、`a787a01` 合入 main）；未配置账户放行、`lock=0` 开关直进桌面、32 位 `#else` 直解 | `auth64.c:321,357`；`desktop64.c:242-246` | **已接线但可绕过**（61c53fa 复核原位）：`hpdesk.c:4622-4642` 登录提交调 `auth64_verify_password`；绕过点 `:4587-4601,7624-7627`（锁屏开关）、`:4628-4631`（未配置放行）、`:4638-4640`（32 位直解） | 评估未配置账户分支在非首启场景是否应收紧；`lock=0` 开关的来源与权限收口；`a_streq`（`auth64.c:35-38`）为早退逐字符比较、非恒定时间，如需抗时序侧信道改为恒定时间比较；`deelevate` 回补密码校验 |
| 4 | **生产启动路径仍跑测试程序**：autorun 四程序 + 两个 uproc 取证探针 | `kernel_main_64.c:1339-1342,1365,1369` | **未关闭**（`61c53fa` 复核仍在无条件 autorun 块内） | 移入 `HEPU_BOOT_SELFTEST` 类编译期/运行时开关，或仅在诊断镜像执行；去掉重复的 `sec_test` |
| 5 | **一批 syscall 空桩返回成功**：mremap/prlimit64/fcntl/clock_nanosleep/epoll_*/link/vfork/… | `syscall64_full.c:795-1448`；32 位 `sys_fs.c:274-342` | **未修** | 未实现桩应返回 `-ENOSYS`（或对应负错误码），不得 `return 0`；修 `wrap_*` 丢失参数问题 |
| 6 | **fb release 不清显存**：探针像素残留到桌面首帧 | `syscall64_full.c:2620-2628` | **未修** | release 或桌面首帧前加显存回读/清屏断言 |
| 7 | **能力句柄只增不减**：`cap_revoke`/`cap_list_domain` 不可达 | `capability.c:111,193`；`capability64.c:117,146` | **未修** | 补设备回收时的能力撤销路径，或明确标注不支持并从接口移除 |

已排除出清单：桌面循环 SB-08 常驻探针、shell autostart 四程序（`0202461` 已默认关闭）；启动期一次性 BSP 浮点自检（保留，属 SMP/FXSAVE 证据，非持续负载）。
