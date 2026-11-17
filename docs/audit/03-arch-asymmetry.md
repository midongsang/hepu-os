# 3. 双架构不对称清单（32 位 i386 multiboot / 64 位 x86_64 UEFI）

基线：`5f93c2d`。判定分两类：
- **缺失**：一侧有完整能力路径，另一侧无对应实现或仅占位字段。
- **双份实现**：两侧各有一份独立代码，不是缺失，但行为可能漂移，需对照维护。

## 3.1 能力 / 协议 / 初始化步骤不对称表

| # | 能力/步骤 | 64 位证据 | 32 位证据 | 判定 | 影响面 |
|---|-----------|-----------|-----------|------|--------|
| 1 | 引导器留屏 magic（`0x6010` "HLDY"） | 读取：`kernel/arch/x86_64/gfx64.c:51`（宏 `HEPU_HOLD_SCREEN_MAGIC 0x484C4459`）、`:1384-1388`（命中则关早期 fb 文本镜像并置 `g_hold_bootscreen=1`）、`:1501-1513`（不清屏、用 LD_BG 重画）；写入方：`tools/hepboot/uefi/hepboot_uefi.c:1925-1931` | 全仓 `kernel/arch/x86/` 与 32 引导路径无 `0x6010`/`HLDY`/`g_hold_bootscreen` 读取方（grep 无命中） | **缺失（32）** | 32 位启动交接没有"保留加载屏"契约；内核日志/清屏行为不受 magic 控制 |
| 2 | `SYS_FB_FLIP` 系统调用 | 表项 `kernel/arch/x86_64/syscall64_full.c:2904`；实现 `wrap_fb_flip` `:2608-2618`（非 owner 拒绝，owner 调 `gfx64_present()`） | 32 位表项仅 `SYS_FB_DESC/ACQUIRE/RELEASE`（`kernel/arch/x86/syscall.c:218-220`），无 FLIP | **缺失（32）** | 32 位无双缓冲提交语义；32 位 fb 描述 flags 恒 0（`kernel/arch/x86/sys_fb.c:99-100`），与该缺失一致 |
| 3 | seccomp 系统调用过滤 | `kernel/arch/x86_64/seccomp64.c`：`init/allow/deny/enable_strict/set_filter/guard_current`；接入点 `syscall64_full.c:987-1017`（设过滤器）、`:3012`（`seccomp64_guard_current` 拦截）、`user_process.c:2415-2418`（槽位复用前 reset） | `kernel/arch/x86/app_container.c:49,290-293` 仅有 `seccomp_enabled` 标志位与 `ac_seccomp_set()` 开关；全仓 32 位无过滤器校验执行路径 | **缺失（32）** | 32 位进程无系统调用白/黑名单强制执行；标志位可能被误以为已隔离 |
| 4 | 私有用户地址空间（隔离页表） | `kernel/arch/x86_64/user_aspace64.c`；宏 `-DHEPU_USER_ISOLATION=1`（`kernel/arch/x86_64/Makefile:245`）；内置 hello 在隔离模式跳过（`kernel_main_64.c:873-878`） | `kernel/arch/x86/kernel.c:506` 仅 `paging_init_memory_isolation()` 解析启动参数；无私有页表分配/切换实现文件 | **缺失（32）** | 32 位用户态地址空间隔离强度低于 64 位 |
| 5 | SMP / AP 启动 | `kernel/arch/x86_64/smp.c`、`ap_trampoline.S`、`ap_trampoline_data.h`（列入 `x86_64/Makefile`）；`kernel_main_64.c:861` 注释 AP 侧 FPU 探针 | `kernel/arch/x86/` 目录无 smp/ap 相关文件（grep 无命中） | **缺失（32）** | 32 位单核；FPU 上下文保存/恢复路径不同（`kernel_init.c:302-316` 注释说明 32 位不保存 FPU 状态） |
| 6 | 账户持久化与密码提权 | `kernel/arch/x86_64/auth64.c`：config 读写、sha256 hash、`elevate/deelevate`；登录提交 `hpdesk.c:4622-4642` 调 `auth64_verify_password`（实现来自 `9e8f2a3`，经 `261a027`、`a787a01` 合入 main，61c53fa 复核仍在原位）；OOBE `oobe64.c`/`hs_oobe64.c` | `kernel/arch/x86/login.c`、`multi_user.c`：登录/解锁存在，但密码参数被忽略（见报告 05 §5.4.1，现状为审计在 61c53fa 树上复核）；无 hash 持久化；另有 syscall 级 `sys_verify_password`（`sys_user.c:103-126`）未接桌面 UI | **能力不对称** | 详见报告 04、05：64 位登录与提权路径均已接线（含绕过分支）；32 位桌面认证仍为空桩放行 |
| 7 | 双缓冲后台帧 | `gfx64.c:42` `back_buffer`，pmm 动态分配；fb_desc flags 双缓冲位（`:1428`） | `sys_fb.c:100` `d.flags = 0u`（单缓冲直写 LFB） | **设计不对称** | 32 位用户态/内核直写显存，撕裂与并发写保护依赖"owner 期间内核不 present" |
| 8 | 网卡驱动 | `kernel/arch/x86_64/e1000_64.c`、`rtl8139_64.c`（`x86_64/Makefile:45-46`）；接线 `kernel_main_64.c:1187-1188` | `kernel/drivers/e1000.c`、`kernel/drivers/rtl8139.c`；接线 `kernel_init.c:526-530` | **双份实现** | 两份驱动独立演化，寄存器/轮询逻辑可能不一致；任务基线提交 `5f93c2d` 修的正是 32 位 e1000 RCTL.BSIZE 错位 |
| 9 | AHCI/SATA 驱动 | `kernel/arch/x86_64/ahci64.c`；ext2 块设备切换 `kernel_main_64.c:1128-1133` | `kernel/drivers/ahci.c` | **双份实现** | 驱动能力（NCQ/端口枚举/错误恢复）需逐一对齐 |
| 10 | IDE 驱动 | `kernel/arch/x86_64/ide64.c`（`sys_disk_read` 引用 `ide64_read_sectors`，`syscall64_full.c:2706-2707`） | `kernel/drivers/ide.c`、`ide_dma.c` | **双份实现** | 32 位有 IDE DMA 探测回退 PIO（`init_sequence.c:319-320`），64 位侧未在同位置见到等价回退注释 |
| 11 | USB 主机控制器 | `usb64.c`、`ehci64.c`、`xhci64.c`（`x86_64/Makefile:124-126`）；文本兜底独立枚举 HID（`kernel_main_64.c:1386-1388`） | `kernel/drivers/usb.c`、`usb2.c`、`drivers/usb/` | **双份实现** | 控制器支持面（EHCI/XHCI）与 HID 合流逻辑两侧不同 |
| 12 | 音频 | `kernel/arch/x86_64/ac9764.c`；初始化 `kernel_main_64.c:1141-1143` | `kernel/drivers/audio.c`、`audio2.c`、`audio2_advanced.c`、`drivers/audio/ac97.c` | **双份实现** | 音量/混音接口未对齐；`hsservices.c:72` 音量寄存器仍是 TODO |
| 13 | 存储文件系统 | `ext2_64.c`、`hepu_fs64.c`、`blk_cache64.c`、`swap64.c`、`partition64.c`、`disk64.c`、`fs_mm/fs_open/fs_stat/fs_rw/fs_dir_sys.c` | `kernel/fs/` 下 32 位实现 + `kernel/drivers/` 块设备 | **双份实现** | 同一 syscall（mmap/open/stat/read/write）两侧各有一份校验逻辑，搬家漏一条 if 回归看不出来（探针注释 `mmprobe.c:1-9`、`rwprobe.c:1-9`） |
| 14 | JPEG 解码 | `jpeg64.c`（`x86_64/Makefile:52`） | 32 位无对应解码文件（`gfx64_compat_ext.c` 注释把 JPEG 列入退化壳依赖） | **缺失（32）** | 32 位图片/JPEG 内容走兜底或不显示 |
| 15 | 能力/域框架 | `capability64.c`、`domain64.c`（`x86_64/Makefile:65-66`），经 `capability64.h:20-29` 宏映射到统一 `cap_*` 名 | `kernel/security/capability.c`、`domain.c`（编入 32 构建） | **双份实现** | 两套数据结构与句柄表；详见报告 04 §3 |

## 3.2 初始化步骤不对称（启动序列对照）

| 步骤 | 64 位位置 | 32 位位置 | 备注 |
|------|-----------|-----------|------|
| 网卡 + 静态 IP + MAC 回写 | `kernel_main_64.c:1184-1206` | `kernel_init.c:524-530`（pci_scan_all → rtl8139 → e1000） | 64 位有 `eth64_update_mac()` 修正硬编码 MAC（`:1194-1195`）；32 位对应路径未见等价回写 |
| 网络 HAL / BSD Socket | `kernel_main_64.c:1212-1217` | `init_sequence.c:343-353`（socket_init 等） | 两侧协议栈文件不同 |
| 账户加载 / OOBE | `kernel_main_64.c:1252-1259`；`desktop64.c:242-246`；登录页三分支校验 `hpdesk.c:4622-4642` | `init_sequence.c:372` `permission_manager_init()`；`login.c` | 64 位未配置账户走 OOBE 建号，已配置账户在登录页做 sha256 比对；32 位登录界面密码不校验 |
| 桌面 extras（锁/动画/主题/插件） | `kernel_main_64.c:1156-1164` | 无对应集中初始化调用点 | 32 位桌面依赖桩降级（`hpdesk32_deps.c:1-10`） |
| 驱动注册表 / 驱动管理器 / GPS / 本地化 | `kernel_main_64.c:1167-1176`（`drv_register_all64/reg64_init/gps64_init/l10n64_init/drv_mgr64_init`） | `init_sequence.c` 对应 `unified_driver_init`（`kernel_init.c:132`）等 | 命名与注册粒度不同 |
| 大量内核子系统自测 | 桌面成功后 3 个一次性探针（`kernel_main_64.c:1291,1375,1381`） | `kernel_init.c`/`init_sequence.c` 内联 20+ 个 `*_selftest()`（见报告 01 §1.3） | 32 位自测在启动路径上密度显著更高 |

## 3.3 待验证项

1. 上表"双份实现"两侧的行为等价性需逐接口对照（寄存器时序、错误码、并发语义）；本次审计只确认了文件归属与调用点，未做逻辑等价性证明。
2. 32 位 `app_container.c` 的 `seccomp_enabled` 标志是否在任何 syscall 分发处被读取：静态 grep 未发现校验点，结论为"未接入强制执行"；需 32 位 syscall 路径通读确认。
3. 64 位 `net64_*` 与 32 位 `net/*` 的协议覆盖差（QUIC/HTTP3/DoQ 等在两侧的可用性）未逐项跑通验证。
