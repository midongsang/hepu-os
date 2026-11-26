# 系统层级 / 命名空间布局审计报告（2026-11-26）

- 生成时间：2026-11-26 22:19:13
- 基线提交：`e55c458`
- 审计工具：`tools/audit_layout.py`（纯 Python，无第三方依赖）
- 32 位源文件数：504；64 位源文件数：157；shared 源码：32 位 13 / 64 位 13

## 1. 只在 32 位编入的 shared 共用模块

无。

## 2. 只在 64 位编入的 shared 共用模块

无。

## 3. 两边都编、同名不同路径的模块

### 3.1 归属错位（ERROR）

无。

### 3.2 历史平行实现（INFO，仅登记）

- 32 位 `kernel/boot/acpi.c` ↔ 64 位 `kernel/arch/x86_64/acpi.c`：历史平行实现（两侧各自适配，非归属错位）
- 32 位 `kernel/sched/smp.c` ↔ 64 位 `kernel/arch/x86_64/smp.c`：历史平行实现（两侧各自适配，非归属错位）

## 4. 64 位 vpath 未覆盖的 shared 源码子目录

无：所有含 `.c` 的 shared 子目录均已在 `vpath %.c` 列表中。

## 5. shared → arch/ 反向依赖

无：`kernel/shared/` 源码未反向依赖 `kernel/arch/`。

## 6. 附加自检

- 64 位清单列出但磁盘不存在：无。
- 32 位 glob 意外收录的 arch/x86_64 文件：无。

## 结论：ERROR 0 项

本轮未发现缺陷级布局分歧；按「先设计后最小迁移」原则，本轮不搬目录，迁移路线见 `docs/design/SYSTEM_LAYOUT_AND_NAMESPACES_2026-11-26.md`。
