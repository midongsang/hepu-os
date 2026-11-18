---
title: ROADMAP 事实核对审计（2026-11-18，A 段）
status: active
date: 2026-11-18
owner: planning
source_of_truth: kernel/arch/x86_64/Makefile + syscall64_full.c dispatch 表 + git log
---

# ROADMAP 事实核对审计 · 2026-11-18（A 段）

> 配套文件：`docs/planning/ROADMAP.md`。本文件逐条记录 A 段事实修正的「行号 / 原值 / 新值 / 依据」。
> 核对基准：本地 `main`，HEAD `a60ffdb`（M1d-2 GICv2 + 虚拟定时器 PPI27）；工作区另有其他 agent 未提交改动（`README.md`、`docs/status/VERSIONING.md`、`kernel/include/version.h`、`kernel/arch/aarch64/m1/m1.c`、`M1.EFI`），本次**未触碰**这些文件。
> 铁律执行：只改事实与状态标记；未改优先级排序、未删条目、未新增产品优先级判断；核不实的条目标「待核实」，未编造进度百分比。

## 0. 版本号口径说明

- 任务基线曾记 VERSIONING 为 0.61.0；核对工作区实况：`docs/status/VERSIONING.md:4` = `sys_version: 0.62.0`、`kernel/include/version.h` = `HEPU_OS_VERSION "0.62.0"`（另一 agent 未提交的 0.61.0→0.62.0 升版）。ROADMAP front matter 按事实源对齐到 **0.62.0**；若 0.62.0 升版最终未被接受，需回退到 0.61.0。
- VERSIONING.md 当前版本段（`VERSIONING.md:110`）= 0.62.0，迭代批次含 0.62.0（多架构第一程 + 抽象层收口）、0.60.7（内核正确性批次 + 安装器闭环）、0.59.0（桌面视觉收口）、0.58.0（clone 真线程）。

## 1. front matter 与修订头

| # | 原 ROADMAP 行号 | 原值 | 新值 | 依据 |
|---|----------------|------|------|------|
| A-1 | L4 | `sys_version: 0.58.0` | `sys_version: 0.62.0` | `docs/status/VERSIONING.md:4`、`kernel/include/version.h`（工作区实况 0.62.0）；见 §0 口径说明 |
| A-2 | L5 | `updated: 2026-11-12` | `updated: 2026-11-18` | 本次修订日期（系统当前日期 2026-11-18，Asia/Shanghai） |
| A-3 | L16 | `上次全面修订：2026-11-01（对齐 0.54.6 实况，基于 syscall64_full.c dispatch 表逐行核对）` | `上次全面修订：2026-11-18（对齐 0.62.0 实况；逐条依据见 ROADMAP_AUDIT_2026-11-18.md）` | 本次复核范围覆盖 P0/P1/P2/P3 与里程碑索引 |

## 2. P0 阻塞

| # | 原行号 | 原值（状态/口径） | 新值 | 依据（文件:行 / commit） |
|---|--------|------------------|------|--------------------------|
| B-1 | L24（P0-1） | `✅ 代码完成 (v0.19.2)，待真机双盘验证` | `✅ QEMU 端到端已闭环 (0.60.7)：装好的盘可单独 UEFI 启动进桌面（NONBLACK=2073597，fsck clean 74/30720 files，含 /bin 27 个 ELF）；真机双盘验证仍待做` | `VERSIONING.md:149-153`（0.60.7 批次第 2 项 OPEN-04 闭环）；验收口径保留「真机待验」 |
| B-2 | L25（P0-2，SEC-04 句） | `SEC-04 capability/uid 能力模型未建（裸盘/reboot/mkfs 仅 PID==0 临时闸；真实用户进程 PID 恒 ≥100，user_process.c:289/629）` | 拆为三条：**能力对象模型已建**——`kernel/security/capability.c` + `kernel/arch/x86_64/capability64.c` 有 mint/derive/revoke/check + 代际失效（generation 递增使旧句柄失效，`capability64.c:117-123/201-202`），`capability64.c` 编入 64 位 `Makefile:66`；拒绝审计环 `cap_audit_rec_t`（256 条，只记拒绝）+ `SYS_CAP_AUDIT`（`capability64.c:159-167`；commit `bb5565b`、`95eed32`）。**未闭环**：① 能力主体按 `domain_id` 绑定，未与 uid 主体绑定；② 凭据为全局静态 `g_cred_uid/gid/euid/egid/umask`（`syscall64_full.c:630-634`），chmod/chown/setuid/setgid 最小实现，未做 per-process 凭据继承（fork 后共享全局凭据，注释 `syscall64_full.c:628`）；③ 高危闸仍是 `PID==0` 或安装器文件名白名单（`syscall_installer_allowed`，`syscall64_full.c:31-39`；`user_process_is_installer`，`user_process.c:2302`），未改由 capability check 放行；真实用户进程 PID 恒 ≥100（`user_process.c:374/700`） | 源码逐行核对；commit `bb5565b`「能力校验拒绝即审计」、`95eed32`「审计环接上 syscall 与诊断应用」。**未把 SEC-04 整句标「已完成」**：uid/凭据主体维度与高危闸替换确实未落 |
| B-3 | L25（P0-2，SEC-05 句） | `SEC-05 sys_readdir 静态 names[64][256]/types[64] 不可重入（syscall64_full.c:1606-1607）` | `SEC-05 readdir 静态表已撤，可重入性部分残留`：0.60.7 移除 16KB 常驻 BSS，改调用期 `kmalloc/kfree`，栈帧 2360→360B（`syscall64_full.c:1562-1598`）；残留：全局 `g_readdir_lock` 串行、内部打包上限 2048B | `VERSIONING.md:154-155`（0.60.7 批次第 3 项）；`syscall64_full.c:1571-1575/1588` |
| B-4 | L25（P0-2，SEC-07 句） | `SEC-07 路径 ≥255 截断而非拒绝（syscall64_full.c:451）` | 保留待解决；行号改为「路径拷贝处，具体行号待核实」 | 未在本次复核中重定位截断判断的确切行；不沿用可能漂移的旧行号 |
| B-5 | L25（P0-2，SEC-18 句） | `SEC-18 ext2 偶发挂死根因未复现` | 保持「根因未复现」 | 无新证据，不改口径 |
| B-6 | L26（P0-3） | `⚠️ sendkey QEMU 端到端已验证；存储读写与真机仍待验证`（无驱动四域结论） | 补：`0.60.7 驱动四域（xHCI/AHCI/E1000/HPET）实测闭环`（xHCI BAR0 64 位取值、AHCI 四处 FIS 缺陷、E1000 注册误判、HPET 校准基准）；USB 写盘与高 LBA 真机读回仍待验证 | `VERSIONING.md:156-161`（0.60.7 批次第 4 项）；原 sendkey/控制器证据行保留未删 |
| B-7 | L33（P0-2 依赖说明） | `capability 模型、readdir 静态表、路径长度策略、SEC-18 根因仍待解决` | 改为：能力对象模型 + 拒绝审计环已建（SEC-04 能力侧），uid/凭据绑定、per-process 凭据继承、高危闸改走 capability check 仍待解决；readdir 16KB 静态表已撤（残留全局锁串行）；路径长度策略、SEC-18 根因仍待解决 | 同 B-2/B-3/B-4 |

### P0 已核对但未改动

- **P0-4** 消除硬编码 PROJECT_ROOT：`Makefile` 实况一致，✅ 保留。
- **P0-5** 蓝牙输入栈：`usb64_bt.c` 不存在，未开始口径保留。
- **P0-6** 驱动签名校验：`kmod64.c` 存在但无公钥验证，未开始口径保留。

## 3. P1 应用生态门槛

| # | 原行号 | 原值 | 新值 | 依据 |
|---|--------|------|------|------|
| C-1 | L41（P1-1） | `epoll_create/epoll_ctl/epoll_wait/eventfd2 仍是桩返回 0（syscall64_full.c:1268-1273）`；复核日期 2026-11-12；未列 pselect6；sys_sleep 描述为「关中断忙等」 | 复核日期改 2026-11-18；桩范围补入 `pselect6`；行号更新为 `syscall64_full.c:1255-1263`；`sys_sleep` 描述改为「忙等（`syscall64_full.c:2122`，计数空转非 hlt 让出）」；注明 epoll/eventfd2/pselect6 的真实现或显式 ENOSYS 改造见本次 B 段提交 | 桩函数实况：`sys_pselect6` L1255、`sys_epoll_create/ctl/wait` L1258-1260、`sys_eventfd2` L1263；`sys_sleep` L2122-2129（`volatile count` 计数空转）。**注**：B 段完成后本行与里程碑索引 L159 需再同步一次 |

### P1 已核对但未改动

- **P1-2** ELF64 装载 + Linux 兼容层：PIE/ET_DYN、static-PIE 端到端、PT_INTERP 交 P1-7，口径与 `elf64.c`/`user_process.c` 实况一致。
- **P1-3** TLS/HTTPS：crypto 11 文件 + TLS 5 文件已编入（`Makefile:85-100`），HTTP 客户端无 443 路由、顶层模拟握手，口径保留。
- **P1-4** swap 框架已有未闭环：`memory_pressure.c` 无进程页换出调用者，口径保留。
- **P1-5** /cfg /data 挂载：`ext2_64.c` 挂载点前缀解析存在（多处），「验证中」保留。
- **P1-6** 桌面 P1 增强：无图标字体/Crossfade 接线/令牌体系，未开始口径保留（源码落点文件名 `hepushell.c` 与现状文件映射关系见 §6 待核实）。
- **P1-7** 动态链接器（用户态 ld.so 方案）：✅ 保留，验收 `gui_test.py --scenario dyn`。

## 4. P2 成熟度

| # | 原行号 | 原值 | 新值 | 依据 |
|---|--------|------|------|------|
| D-1 | L67（P2-7） | `❌ 未开始：5 个占位应用（Browser/Editor/Calculator/Notes/About）渲染 App content rebuilding；Calculator 最简单可先做` | `⚠️ 部分（2026-11-18 复核）`：Notes 内核实现已落地（`notes64.c`，启动期 `notes64_init()` 见 `kernel_main_64.c:1237`，终端 `notes` 命令 `term64.c:244/248/252`）；Editor 有窗口实现（`hs_apps.c:4363/4380`）。**待核实**：Dock 图标点击到 GUI 应用的完整接线，以及 Browser/Calculator/About 是否仍渲染占位；旧「5 个全占位」口径不再沿用 | `notes64.c` 导出 `notes64_init/open/show/save/draw`；`term64.c` 命令分支；`hs_apps.c` Editor 窗口。Dock 点击链路未在本次完整走通，标待核实 |

### P2 已核对但未改动

- **P2-1** 三区制重装保留 /cfg：未开始，口径保留。
- **P2-2** 统一块缓存基础：✅ 保留。
- **P2-3** 任务管理器：`taskmgr64.c` 内无 kill/结束进程路径（grep 无 `kill/terminate/结束`），「无结束进程」口径保留。
- **P2-4 / P2-5 / P2-6**：部分落地口径保留，本次未逐项重核 UI 面板细节。

## 5. P3 远期

| # | 原行号 | 原值 | 新值 | 依据 |
|---|--------|------|------|------|
| E-1 | L76（P3-2） | `gps64.c 存在但无任何网络调用；天气显示依赖 HTTP（已做）+ 无 TLS` | 保留 gps64 无网络调用（源码 grep 无 tcp/http/recv/send）；补：**天气取数已打通**（BUG-59，commit `308e72b`，换国内源 + 解析适配 + 失败重试），仍走明文 HTTP、无 TLS（P1-3） | commit `308e72b`；`gps64.c` grep 无网络调用；天气数据结构在 `hpdesk.c:47-49` |

### P3 已核对但未改动

- **P3-1** 兼容层：Linux 按 env_type 判定已做；Windows/Mach-O 仅生成器无加载器，口径保留。
- **P3-3** 驱动在线下载：`kmod64.c` 编入但无 HTTP 下载路径，规划中保留。
- **P3-4** Rust：`user/rust/` 多 crate 存在；`user/Makefile` 无 cargo 目标（grep 无 rust/cargo），未接入构建口径保留。
- **P3-5** 安全审计闭环：crypto/TLS 代码编入未接 HTTP 链，部分口径保留。
- **P3-6** VFS 抽象层：64 位未建。**本行属另一 agent 接手范围，本次未改。**
- **P3-7** NVMe/virtio-gpu：不在 64 位 Makefile，口径保留。
- **P3-8** Secure Boot：源码不存在，保留。
- **P3-9** IPC 原语：64 位 syscall 表无 shmget/semget/msgget（grep 为空），未实现口径保留。
- **P3-10** IPv6：源码不存在，保留。

## 6. 里程碑索引与新增小节

| # | 原行号 | 原值 | 新值 | 依据 |
|---|--------|------|------|------|
| F-1 | （新增，插在「构建与工具」小节之后） | 无 ARM64/arch_port 条目 | 新增 `### 多架构移植（arch_port + ARM64 M0–M1d）` 小节，9 行：arch_port 抽象层；M0；M1a；M1b；M1c；M1d-1；M1d-2；M1d-3（HEAD 未做，工作区草稿待核实）；复用层接入未开始 | 见下表逐条 |

### F-1 小节逐条依据

| 里程碑 | 状态 | 源码落点 / 依据 |
|--------|------|----------------|
| arch_port 抽象层（`arch_ticks`/`arch_console_puts`/`arch_poweroff`） | ✅ 已落地 | 声明 `kernel/include/arch_port.h:24-32`；实现 `arch/x86_64/arch_port_x86_64.c`（64 位 `Makefile:159`）、`arch/x86/arch_port_x86_32.c`、`arch/aarch64/arch_port_aarch64.c`（41 行：CNTVCT_EL0 / PL011 @0x09000000 / PSCI SYSTEM_OFF）；commit `3afe24d`（引入移植点）、`889b8cb`（第二架构实现）。注：`arch_port.h:19` 注释仍写 arm64「待建」，与实况不符，待同步（本审计不改动该头文件，超出本次边界） |
| ARM64 M0：UEFI + GOP 800×600 填色 + PSCI 关机 | ✅ 已通 | `arch/aarch64/m0/hepu_m0_a64.c`；PE 包装 `tools/pe_flatten_aarch64.py`；串口证据 `arch/aarch64/README.md`「UEFI 引导：已通」段；commit `b0e1809` |
| M1a：自建页表切 TTBR0_EL1 | ✅ | `arch/aarch64/m1/m1.c`；commit `2b64fd0` |
| M1b：排除 WXN 假设，锁定 tlbi 后崩溃为真崩溃 | 实验结论 | `m1.c`；`arch/aarch64/README.md` M1b 段；commit `f79826a` |
| M1c：4KB L3 白名单映射 + VBAR，tlbi 后存活 exit=0 | ✅ | `m1.c`；commit `54c7bb6` |
| M1d-1：GetMemoryMap 全 RAM 混合映射 + 远端 RAM 写读回 | ✅ | `m1.c`；commit `391e1db` |
| M1d-2：GICv2 判定 + 虚拟定时器 PPI27 中断计数（EOIR 后重 arm） | ✅ | `m1.c` GICD/GICC @0x08000000/0x08010000；commit `a60ffdb` |
| M1d-3：ExitBootServices + 描述符静态副本 + 自有 bss 栈切换 | ❌ HEAD 未做；工作区另有未提交草稿，结论待核实 | 计划项 `m1.c:7`；HEAD 代码内未见 `ExitBootServices` 调用。工作区 `m1.c` 未提交 diff 含 `new_stack`/`stack_switch_asm`/`map_fb_region` 草稿，属另一 agent，未代其下结论 |
| 复用层接入（shared/desktop、gfx64 基元接 GOP 出界面） | ❌ 未开始 | 无对应源码 |

### 索引中本次未改、留待 B 段或后续

- **L159**（系统调用里程碑）：`futex / clock_gettime ✅；epoll/eventfd2 为桩返回 0`。B 段完成 epoll/eventfd2/pselect64 改造后，本行口径与行号需同步；A 段先保留桩口径，避免提前写未验结论。
- **L146/147/149** 源码落点写 `hepushell.c`，但 64 位树内未见同名文件（桌面/窗口实现分布在 `hs_apps.c`/`desktop64.c`/`window64.c` 等）。本次未改文件名映射，**待核实**：需确认 hepushell 功能实际归属文件后再更正落点。

## 7. 变更日志

| # | 原行号 | 动作 | 依据 |
|---|--------|------|------|
| G-1 | L176 表头之后 | 新增 `2026-11-18` 变更行，概括 A 段全部修正并指向本审计文件 | 本审计 §1–§6 |

## 8. 待核实清单（未编造结论）

1. **SEC-07 路径 ≥255 截断判断的确切行号**：旧引 `syscall64_full.c:451` 已漂移，本次未重定位。
2. **M1d-3 完成度**：HEAD 未做；工作区草稿属另一 agent，合并后需按其串口证据复核。
3. **P2-7 Dock 点击链路**：Notes/Editor 内核实现已见，Dock 图标 → GUI 窗口的完整接线未走通；Browser/Calculator/About 占位现状待核实。
4. **`hepushell.c` 落点映射**：里程碑索引与 P1-6 引用的文件名与现状文件对应关系待核实。
5. **版本号 0.62.0**：来自另一 agent 未提交升版；若回退，ROADMAP front matter 需一并回退（见 §0）。

## 9. 边界与未做事项

- 未触碰：`kernel/arch/aarch64/m1/`（M1d 线）、能力审计/crash_ctx/linuxhello/diag 相关文件、`docs/architecture`、`docs/status`、VFS（P3-6，另一 agent 接手）。
- 本次只 `git add` 本审计文件与改过的 `ROADMAP.md`；未 `git add -A`，未 push。
