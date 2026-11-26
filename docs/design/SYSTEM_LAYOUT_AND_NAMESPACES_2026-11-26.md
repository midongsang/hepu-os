---
title: SYSTEM_LAYOUT_AND_NAMESPACES_2026-11-26
status: active
sys_version: 0.62.1
updated: 2026-11-26
owner: design
source_of_truth: tools/audit_layout.py；Makefile；kernel/arch/x86_64/Makefile
supersedes: null
---

# 系统层级 / 命名空间梳理（2026-11-26）

## 1. 背景、范围与原则

- 基线：主仓合并提交 `9c942dc`（事件日志第一阶段已合入）；本路工作区分支 `feat/system-layout-2026-11-26`。
- 目标：先把「目录职责、32/64 平行实现归属、命名空间红线」设计清楚，再做**最小迁移**；本轮不搬大目录、不重排已有清单条目、不重命名既有文件。
- 输入事实：第一阶段暴露的结构问题——同一条事实被多套日志实现记录（`klog.c` / `klog64.c` / `boot_gfx.c` / `crashlog.c` / `drivers/trace.c`），服务层存在四个注册点（`sched/services.c`、`arch/x86/microg_service.c`、`arch/x86/distributed_os.c`、`arch/x86_64/hsservices.c`）。
- 审计口径：本文所有「单边编译 / 归属不一致 / vpath 缺口 / 反向依赖」结论均来自 `tools/audit_layout.py` 对两份构建清单与磁盘实际文件的解析，不做人工猜测；脚本退出码 0 表示无缺陷级发现，1 表示存在 ERROR 项，可直接进 CI。
- 与既有文档的关系：图形/桌面分层见 `docs/architecture/ARCHITECTURE_LAYERS.md`；双架构能力不对称清单见 `docs/audit/03-arch-asymmetry.md`；统一源码树历史进度见 `docs/architecture/UNIFIED_SOURCE_TREE.md`。本文只补「目录职责与命名空间规则」这一层，不重写上述文档。

## 2. 目录职责矩阵

计数口径：`.c` / `.h` 为当前 worktree 磁盘实测数量（脚本盘点，非历史文档口径）。

| 目录 | 该放什么（职责定义） | 现在实际放了什么 | 判定 |
|---|---|---|---|
| `kernel/arch/x86` | 32 位 i386 平台适配：32 位启动后期初始化、GDT/IDT/PIT/中断适配、32 位 syscall 分发、32 位驱动与图形适配；**不放**架构无关算法 | 131 个 `.c`。除平台适配外，混入 32 位应用/服务平行实现（`microg_service.c`、`distributed_os.c`、`login.c`、`multi_user.c` 及一批 `*_app.c`）；`klog.c` 为 32 位文本环适配器 | 职责基本成立；历史平行实现与占位服务混杂在内，属归集候选，本轮不搬 |
| `kernel/arch/x86_64` | 64 位 x86_64 平台适配：UEFI 启动后期、64 位内存/中断/syscall、64 位驱动与桌面栈 | 125 个 `.c`、51 个 `.h`；含 `hsservices.c`（64 桌面服务集成）、`shell64.c`、`ext2_64.c` 等平行集合；其中 `hpdesk.c`/`hs_ui.c`/`hsqml.c`/`l10n64.c`/`png64.c` 是纯软件文件，被 32 位根 Makefile 的 `HP_DESK_DEPS` 白名单显式回追编译 | 职责基本成立；5 个白名单文件是跨架构复用的历史例外，需在 Makefile 注释中持续标注 |
| `kernel/sched` | 架构无关的调度、进程、Shell 命令注册共用层 | 69 个 `.c`：调度器、`process.c`、Shell 命令族、`services.c`（第一阶段起 32/64 共用）；另含 `smp.c`（32 位 SMP 平行件，历史上错置于此） | 共用层主体成立；`smp.c` 归属错位，登记归集候选 |
| `kernel/shared/fs` | 架构无关的文件系统算法与路径工具（回调上下文 + 纯算法） | 3 个 `.c`（`ext2_algo.c`、`ext2_dir_algo.c`、`fs_path.c`）+ 6 个头 | 符合职责 |
| `kernel/shared/gfx` | 架构无关图形算法：颜色、矩形、后端操作表；不碰帧缓冲直写 | 1 个 `.c`（`gfx_algo.c`）+ 2 个头 | 符合职责 |
| `kernel/shared/drivers` | 统一驱动框架接口与 ops 约定 | 1 个 `.c`（`driver_framework.c`）+ 1 个头 | 符合职责 |
| `kernel/shared/lib` | 架构无关的字符串/内存/标准库替代实现 | 1 个 `.c`（`lib_shared.c`）+ 1 个头 | 符合职责 |
| `kernel/shared/log` | 统一事件日志事实层（结构化记录、单一环形缓冲；平台只提供时钟/pid/sink 适配） | 1 个 `.c`（`event_log.c`）；契约头在 `kernel/include/event_log.h` | 符合职责 |
| `kernel/shared/debug` | 架构无关调试辅助 | 1 个 `.c`（`debug_shared.c`）+ 1 个头 | 符合职责 |
| `kernel/shared/desktop` | 桌面运行时注册表/表面/注册器等纯软件层，不直接写像素 | 5 个 `.c`（`desk_demo/desk_hpdesk/desk_registry/desk_runtime/desk_surface.c`）+ `desk_api.h` | 符合职责 |
| `kernel/shared/{ai,capability,domain,mm,net,process,qos,security}` | 仅放跨架构契约头，不放实现 | 均为 0 个 `.c`，1~3 个头 | 符合职责；后续若下沉实现，必须两侧清单同步收录（见红线 R5） |
| `kernel/include` | 跨架构契约头：两侧共同消费的数据模型、ABI、共享结构 | 311 个头。实测分三类：① 两侧共同消费（如 `event_log.h`、`services.h`、`types.h` 部分）；② 仅 32 侧消费的历史契约头（如 `vfs.h`、`ext2.h`、`process.h`、`shell.h`、`kstdio.h`）；③ 64 位私有头例外 `seccomp64.h`（仅 `arch/x86_64/seccomp64.c`、`syscall64_full.c` 包含） | 混杂：历史 32 位私有头与 64 位私有头未与真跨架构契约分离，登记 OPEN-05 |
| `kernel/kunit` | 架构无关的 KUnit 测试套件 | 58 个 `.c` | 符合职责 |
| `kernel/drivers` | 历史顶层 32 位设备驱动树；64 位对应驱动在 `arch/x86_64` | 60 个 `.c`、6 个头，含 `audio/`、`usb/` 子目录 | 32 位驱动集合，属双份实现一侧；后续驱动归集应先经 `shared/drivers` 出 ops 契约 |
| `kernel/net` | 32 位协议栈 | 44 个 `.c`；64 位对应 `net64_*` 在 `arch/x86_64` | 同上，双份实现一侧 |
| `kernel/crypto` | 架构无关密码算法 | 11 个 `.c`；32 位 glob 与 64 位 `C_SOURCES` 均编入全部 11 个 | 符合职责，两侧一致 |

## 3. 32/64 重复 / 平行实现映射表

分类口径：
- **平台适配**：依赖特定架构的中断、内存模型、寄存器或本架构驱动/图形后端，归 `arch/`。
- **共用逻辑**：架构无关，可下沉 `shared/` 或 `sched/` 被两侧同时编译。
- **历史平行集合**：两侧各写一份、接口尚未对齐的独立实现；或仅占位、未接真实能力的服务集。

| 能力 / 事实 | 32 位侧 | 64 位侧 | 共用层现状 | 分类 | 建议动作 |
|---|---|---|---|---|---|
| 内核诊断日志 | `arch/x86/klog.c`：32 位文本环形缓冲，`klog_save_to_disk()` 写 `/hepu_boot.log`，经 PIT tick/pid 适配接 `event_log` | `arch/x86_64/klog64.c`：64 位 8KB 文本环 + 自旋锁，接 `event_log` | `shared/log/event_log.c`：结构化事实层（第一阶段落地） | 两侧 `klog*` 均为**平台适配**；`event_log` 为**共用逻辑**；无历史平行集合 | 维持薄适配器形态；后续把落盘 sink 接口化，适配器只提供时钟/pid/sink。本轮不改实现 |
| Shell 命令行 | `sched/shell.c` + `shell_cmd_{adv,fs,system,test,ui}.c` + `shell_execute.c` + `shell_help.c`（32 命令族） | `arch/x86_64/shell64.c`（64 命令族） | 无共用命令注册表；第一阶段仅 `journal`/`log`/`service` 三条命令两侧接通 | **历史平行集合**为主，夹带平台适配（键盘/输入后端） | 命令实现与后端先分离；候选下沉位置 `shared/shell/`；本轮只登记，不搬 |
| 服务注册 | `sched/services.c`：systemd 式注册/启动/停止/依赖表，第一阶段起 32/64 共用；64 侧 `crash_ctx` 为不伪造恢复现场的桩 | `arch/x86_64/hsservices.c`：64 桌面服务集成，直接调 `gfx64_*`/`process64_*` | `services.c` 表本身是**共用逻辑**；桌面服务绑定是 64 适配 | `services.c`=共用逻辑；`hsservices.c`=64 **平台/应用适配** | 共用表保持单一事实源；64 桌面集成继续留在 `arch/x86_64` |
| 占位服务集 A：microG 集成 | `arch/x86/microg_service.c`：静态服务名表 + 占位状态 | 无对应实现 | 无 | **历史平行集合**（占位，未接真实 Android 兼容能力） | 标注占位属性，不进共用层；待真实能力落地后再评估归属 |
| 占位服务集 B：分布式互联 | `arch/x86/distributed_os.c`：`distributed_os_t` 初始化占位 | 无对应实现 | 无 | **历史平行集合**（占位） | 同上，登记备查，不归集 |
| ext2 文件系统 | `kernel/fs/ext2.c`、`ext2_dir.c`、`ext2_alloc.c`、`ext2_path.c`、`ext2_fsck.c`、`fsr_ext2.c`、`ide_ext2*.c` | `arch/x86_64/ext2_64.c`、`vfs64_ext2.c`、`ext2_fsck64.c`、`hepu_fs64.c` | `shared/fs/ext2_algo.c`、`ext2_dir_algo.c`、`fs_path.c`：纯算法 + 回调上下文 | 两侧编排/缓存/块设备适配为**平台适配**；算法已部分下沉；32 侧 `kernel/fs` 多文件为**历史平行集合** | 新算法先进 `shared/fs`；编排层平行维持；等价性对照见 `docs/audit/03-arch-asymmetry.md` 第 13 行 |
| 启动主入口 | `arch/x86/kernel.c`，配合 `kernel_core.c`、`kernel_init.c`、`init_sequence.c` 组成 32 启动链 | `kernel_main_64.c`（位于 `kernel/` 根，未进 `arch/x86_64`） | 无共用启动序列（启动步骤强依赖架构中断/内存模型） | 两侧均为**平台适配**；`kernel_main_64.c` 位置偏离 `arch/` 属历史遗留 | 后续阶段评估 64 入口归位 `arch/x86_64/`；本轮不动 |
| 同名平行件：ACPI | `kernel/boot/acpi.c`（32） | `arch/x86_64/acpi.c`（64） | 无 | 架构**平台适配**双份；32 侧文件放在 `kernel/boot/` 是历史位置 | 登记归集候选；行为等价性未证明，不做合并 |
| 同名平行件：SMP | `kernel/sched/smp.c`（32） | `arch/x86_64/smp.c`（64） | 无 | 架构**平台适配**双份；32 侧错置于 `sched/` | 同上；32 位当前单核，见不对称清单第 5 行 |

补充说明：`arch/x86/boot_gfx.c` 自带 `boot_log`、`arch/x86/crashlog.c` 追加进 klog、`kernel/drivers/trace.c` 独立序号——这三处是第一阶段梳理出的额外日志旁路，本轮未取得下沉事实层的运行证据，保留现状并在交接中列出。

## 4. 命名空间与 include 规则（可检查红线）

### 4.1 依赖方向

```
arch/x86 ──┐
           ├─→ kernel/include（跨架构契约）←─ kernel/shared/*
arch/x86_64 ┘        ↑                          │
                     └────── shared 不得反向 ────┘
```

- **R1（shared 不反向依赖 arch）**：`kernel/shared/**.c` 的 `#include` 解析结果不得落在 `kernel/arch/**`。由 `audit_layout.py` 第 5 类检查执行；当前基线 0 命中。
- **R2（契约头归属）**：跨架构契约头只能放 `kernel/include/` 或 `kernel/shared/<子域>/*.h`；`shared/` 源码不得包含 `*64.h`、`klog64.h`、`process64.h`、`gfx64.h` 等架构私有头。
- **R3（架构侧互不引用）**：`arch/x86` 与 `arch/x86_64` 互不直接 include 对方目录；唯一例外是根 Makefile `HP_DESK_DEPS` 白名单内的 5 个纯软件文件，且必须在 Makefile 注释中说明「不碰硬件、只调同签名图元」。
- **R4（私有头归位）**：单架构私有头应放 `arch/<arch>/include/`；现状例外 `kernel/include/seccomp64.h` 仅 64 侧消费，登记 OPEN-05，本轮不移动。
- **R5（共用模块双侧收录）**：新增 `shared/` 实现必须同时进两份构建清单。32 位由根 Makefile 的 `find kernel -name "*.c"` 自动收录；64 位必须显式加入 `arch/x86_64/Makefile` 的 `C_SOURCES`，且含 `.c` 的 shared 子目录必须出现在 `vpath %.c` 列表。`audit_layout.py` 对单边收录、vpath 缺口报 ERROR，退出码 1。
- **R6（shared 不碰硬件）**：`shared/` 内不得出现端口/寄存器读写、页表操作、帧缓冲直写；这类操作只能在 `arch/` 或 `drivers/`。

### 4.2 include 搜索口径（与两份 Makefile 对齐）

- 32 位（根 `Makefile` CFLAGS）：`kernel/include`、`kernel`、`kernel/shared`、`services`。
- 64 位（`arch/x86_64/Makefile` CFLAGS）：`kernel/include`、`kernel`、`arch/x86_64`、`arch/x86_64/include`、`kernel/shared`、`shared/fs`、`shared/lib`、`shared/debug`。
- 头文件解析顺序：先源文件同目录，再按上述 `-I` 顺序；审计脚本按同一顺序解析，避免把同目录私有头误判为跨架构契约。

### 4.3 `kernel/include` 头文件分类（实测口径）

| 类别 | 判定方法 | 当前示例 | 处置 |
|---|---|---|---|
| 跨架构契约 | 被 `arch/x86`（含 sched/fs/drivers/net）与 `arch/x86_64` 两侧源码共同包含 | `event_log.h`、`services.h`、`types.h`（部分） | 留在 `kernel/include` |
| 32 位历史私有头 | 仅 32 侧源码包含，64 侧使用自己的 `*64.h` | `vfs.h`、`ext2.h`、`process.h`、`shell.h`、`kstdio.h` | 暂留原位；归集候选，后续随 32 位驱动/文件系统平行件一起评估 |
| 64 位私有头 | 仅 `arch/x86_64` 源码包含；部分已在 `arch/x86_64/include/`（`kernel_contracts.h`、`pmm64.h`、`vmm64.h`），例外 `seccomp64.h` 仍在 `kernel/include` | `seccomp64.h` | `seccomp64.h` 归位事项登记 OPEN-05 |

## 5. 本轮审计结论（来自工具实测）

执行命令：`python3 tools/audit_layout.py`（基线提交 `9c942dc`）。

| 检查项 | 结果 |
|---|---|
| 32 位源文件数 / 64 位源文件数 | 502 / 156 |
| shared 源码两侧收录 | 13 / 13，一致 |
| 只在 32 位编的 shared 模块 | 无（ERROR 0） |
| 只在 64 位编的 shared 模块 | 无（ERROR 0） |
| shared/arch 归属错位 | 无（ERROR 0） |
| vpath 未覆盖的含 `.c` shared 子目录 | 无（ERROR 0） |
| `shared/` → `arch/` 反向依赖 | 无（ERROR 0） |
| 64 位清单磁盘缺失文件 | 无 |
| 32 位 glob 意外泄漏 `arch/x86_64` 文件 | 无（仅 `HP_DESK_DEPS` 白名单 5 个） |
| INFO：历史平行实现（不阻断） | 2 条：`kernel/boot/acpi.c` ↔ `arch/x86_64/acpi.c`；`kernel/sched/smp.c` ↔ `arch/x86_64/smp.c` |

**结论：本轮审计未发现缺陷级布局分歧，按「先设计后最小迁移」原则，本轮不做目录迁移、不补漏编源文件、不改 vpath。** 完整机器报告见 `docs/status/evidence-layout-2026-11-26/layout-audit-report.md`。

## 6. 分阶段迁移路线（本轮未迁移，以下为后续阶段计划）

每阶段独立可回滚；阶段之间以构建 + 冒烟证据作为进入下一阶段的前提。影响文件数为当前基线估算，随清单漂移以 `audit_layout.py` 实测为准。

### 阶段 0：红线固化（本轮已落地）

- 目标：把上述 R1–R6 规则与清单对比固化为可执行检查。
- 动作：`tools/audit_layout.py` 落地；接入 CI 门禁（退出码 1 阻断）；报告入证据目录。
- 影响文件：新增 `tools/audit_layout.py` 1 个；无源码/构建清单改动。
- 验证：32/64 构建 0 error；32 位无盘 + 带盘冒烟按规范口径通过；`check_docs.py` warns=0 errors=0。
- 回滚：删除脚本即可，不影响既有构建。

### 阶段 1：私有头归位（预计 1~2 个头文件）

- 目标：把仅 64 侧消费的私有头从 `kernel/include` 移到 `arch/x86_64/include/`，同步 `-I` 与包含路径。
- 候选：`seccomp64.h`（OPEN-05）。
- 影响文件：约 1 个头 + 2 个包含方；不动 32 位源码。
- 验证：64 位构建 0 error；64 位 UEFI 镜像启动取证；32 位构建与冒烟不受影响。
- 回滚：恢复头文件原位与包含路径，单提交可逆。

### 阶段 2：日志旁路收敛（前置事实层稳定后）

- 目标：评估 `boot_gfx.c` 的 `boot_log`、`crashlog.c`、`drivers/trace.c` 三条旁路是否改为经 `event_log` 记录；未取得运行证据前不宣称统一。
- 动作：先补各旁路与事件日志的映射对照表，再逐旁路切换 sink。
- 影响文件：待定，预计 3~5 个 `.c`；每旁路独立提交。
- 验证：32 位带盘冒烟 KUnit 全 PASS；事件日志序号/级别/来源可查；64 位启动取证。
- 回滚：各旁路保留旧路径开关，出问题按提交回退。

### 阶段 3：Shell 命令与服务表下沉

- 目标：把两侧重复的命令实现从后端适配中剥离，候选落 `shared/shell/`；服务注册只保留 `services.c` 单一事实源，占位服务集标注清楚。
- 影响文件：`sched/` 命令族与 `shell64.c` 中可剥离部分；预计先动 1~2 个无硬件依赖命令做样板。
- 验证：两侧命令行为对照（KUnit 带盘挂载 ext2）；命令帮助与输出一致；不新增 32 位 CLI 交互手测口径（32 位无串口输入，按规范走 KUnit）。
- 回滚：下沉模块可在清单中摘除，回退到两侧原实现。

### 阶段 4：驱动 / 文件系统平行件归集（长周期）

- 目标：先经 `shared/drivers`、`shared/fs` 出 ops 契约，再逐驱动/逐文件系统对照两侧等价性；不做整目录搬运。
- 前置：`docs/audit/03-arch-asymmetry.md` 所列双份实现逐项完成行为等价性核对。
- 验证：每归集一项，两侧构建 + 冒烟 + 对应子系统自测全绿；回归证据入新证据目录。
- 回滚：按子系统粒度回退，不影响已归集项。

## 7. 未覆盖项与交接

1. 本轮未证明任何两侧平行实现的**行为等价性**，只核对了文件归属与构建收录；等价性属阶段 4 前置工作。
2. 32 位 Shell 的 CLI 交互未覆盖（无串口输入），命令行为验证走 KUnit 带盘路径，与规范第 3 节口径一致。
3. `kernel/include` 中 32 位历史私有头的完整清单未逐个复核消费方；本文表格示例以实测包含关系为准，后续随阶段 1 一起盘点。
4. 别路改动锚点（`event_log*`、`services.c`、`shell*.c` 实现代码、第一阶段两份 Makefile 条目）本路只读不写；发现别路应改的问题记录在此，不替它修改。
