# 2. 32/64 桌面 `hpdesk.c` 共用核查

## 2.1 结论（已确认）

**32 位与 64 位编译的是同一份 `kernel/arch/x86_64/hpdesk.c`（a787a01 上 8879 行；基线 `5f93c2d` 为 8767 行），不存在 32 位私有桌面副本。** 32 位通过同名兼容层（gfx64 图元桩 + 缺符号填充）链接这份源码；源码内部用 `__i386__` / `__x86_64__` 条件分支区分架构行为。

> 注：`f3b3c78` 把 ELF/PE/Mach-O/Linux ABI 兼容层从 `kernel/` 迁到 `services/compat/`；与本节的 gfx64 图元兼容层（`arch/x86/gfx64_compat*.c`）不是一回事，后者仍在 32 位内核树内。

## 2.2 编译归属证据

### 64 位构建

- 显式文件列表：`kernel/arch/x86_64/Makefile:158` → `$(ARCH_DIR)/hpdesk.c`（`ARCH_DIR = kernel/arch/x86_64`，`:18`）。
- 调用方声明：`kernel/arch/x86_64/desktop64.c:15-23` extern `hpdesk_init / hpdesk_render / hpdesk_handle_key / hpdesk_update_mouse / hpdesk_mark_dirty / hpdesk_claim_owner / hpdesk_deskstate_drain / hpdesk_mouse_down / hpdesk_mouse_up`。
- 注册表包装：`kernel/arch/x86_64/Makefile:104` → `$(KERNEL_DIR)/shared/desktop/desk_hpdesk.c`。

### 32 位构建

- 顶层 `Makefile:71`：`C_SRCS = $(filter-out $(KERNEL_DIR)/sched/scheduler.c $(KERNEL_DIR)/arch/x86_64/%, $(shell find $(KERNEL_DIR) -name "*.c")) $(HP_DESK_DEPS)`。即默认排除整个 `arch/x86_64/`，再把 `HP_DESK_DEPS` 追加回来。
- `Makefile:64`：`HP_DESK_SHARED = $(KERNEL_DIR)/arch/x86_64/hpdesk.c`。
- `Makefile:67-70`：`HP_DESK_DEPS = $(HP_DESK_SHARED) + arch/x86_64/hs_ui.c + arch/x86_64/hsqml.c + arch/x86_64/png64.c`。
  注释 `:60-63` 明确："hpdesk.c(64 位桌面) 也编进 32 位——这是 32 位共用 64 位桌面……64 位其余文件仍排除，只放行这一个。"
- 32 位链接所需的兼容实现（**不是桌面副本**）：
  - `kernel/arch/x86/gfx64_compat.c`（628 行）：30 个 `gfx64_*` 图元同名实现（头注释 `:4-7`）。
  - `kernel/arch/x86/gfx64_compat_ext.c`（141 行）：hpdesk 依赖的非绘制外部符号，含光标/主题退化壳（`:1-8`、`:85-110`）。
  - `kernel/arch/x86/hpdesk32_deps.c`（229 行）：hpdesk.c 编进 32 位后还缺的外部符号；分"真实现"与"退化壳"两类（`:1-10`）。
- 32 位启动走注册表：`kernel/arch/x86/init_sequence.c:582-591` 调 `desk_start(100)`；`shared/desktop/desk_hpdesk.c:1-18` 把 hpdesk 包装成 `desk_backend_t` 注册进可切换桌面。

### 32 位无私有副本的核查结果

- 仓内名为 hpdesk 的源码仅三个：`arch/x86_64/hpdesk.c`、`arch/x86_64/hpdesk.h`、`arch/x86/hpdesk32_deps.c`（`find kernel -name "hpdesk*"`）。
- `hpdesk32_deps.c` 不含任何桌面绘制/布局逻辑，仅补外部符号；`gfx64_compat*.c` 是图元后端适配层。

## 2.3 同一份源码内的架构条件分支

`hpdesk.c` 内按宏条件编译的位置（行号为 a787a01，已逐处打开核对）：

| 行号 | 宏 | 32 位分支内容 |
|------|----|--------------|
| `:1246` | `#ifndef __x86_64__` | 32 位无文件系统时用内置预解码 RGBA 壁纸，绕开会卡死的 png 解码 |
| `:2981` | `#if defined(__i386__)` | 32 位阶段打点限 6 条，避免串口洪水 |
| `:3971` | `#if defined(__i386__)` | 32 位 kheap 结构性缺陷下，桌面离屏层走专用静态池 `hpdesk32_layer_pool_alloc` |
| `:5400` | `#if defined(__i386__)` | 阴影半径减半（32MB 堆约束） |
| `:5410` | `#if defined(__i386__)` | 32 位跳过阴影模糊（TCG 下首帧超 70 秒），函数直接 return |
| `:6905` | `#if defined(__i386__)` | 32 位启动时显式清 LFB 顶部残留行 |
| `:7261` | `#if defined(__i386__)` | 帧率统计降为 2000 帧一行、最多 20 行 |
| `:6774`、`:6979` | `#if 0` | 两处被禁用的旧逻辑（非架构分支） |

## 2.4 可复核的复核路径

1. 32 位目标列表：`make -n` 后检查 `build_v250/arch/x86_64/hpdesk.o` 是否出现在编译命令中（源码路径来自 `arch/x86_64/hpdesk.c`）。
2. 64 位目标列表：`kernel/arch/x86_64/Makefile:158`。
3. 符号来源：32 位 `gfx64_*` 未解析符号应全部落在 `gfx64_compat.c` / `gfx64_compat_ext.c` / `hpdesk32_deps.c`。

## 2.5 待验证项

- 32 位构建产物中 `hpdesk.o` 是否真的被链接进最终内核（`--gc-sections` 可能丢弃无引用段；`Makefile:27`）。验证方法：32 位 `kernel.elf` 符号表中查 `hpdesk_init` 等符号的归属目标文件。
- `hs_ui.c`、`hsqml.c`、`png64.c` 随 `HP_DESK_DEPS` 编进 32 位后，32 位桩是否覆盖了它们引用的全部外部符号；需一次 32 位链接确认无未解析符号。
