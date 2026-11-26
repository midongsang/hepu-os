# 系统层级 / 命名空间梳理 证据（2026-11-26）

分支：`feat/system-layout-2026-11-26`
基线：`9c942dc`（合并后 main）
审计工具：`tools/audit_layout.py`（纯 Python，无第三方依赖）
设计文档：`docs/design/SYSTEM_LAYOUT_AND_NAMESPACES_2026-11-26.md`

## 1. 本轮改动范围

本路只做「设计 + 审计固化」，**未搬目录、未重命名文件、未改 `C_SOURCES` 既有条目顺序、未动别路锚点实现代码**。

- 新增 `tools/audit_layout.py`：32/64 构建清单对比审计脚本（单边收录、归属错位、vpath 缺口、shared→arch 反向依赖、磁盘缺失、32 位泄漏 x86_64 检查；退出码 0/1）。
- 新增 `docs/design/SYSTEM_LAYOUT_AND_NAMESPACES_2026-11-26.md`：目录职责矩阵、32/64 平行实现映射表、命名空间红线、分阶段迁移路线。
- `tools/gen_docs_index.py`：分组顺序补入 `design/`（生成器原本漏列该目录，导致设计文档不进 `docs/README.md` 索引）；重新生成 `docs/README.md`。
- `docs/status/OPEN_BUGS.md`：登记 OPEN-07（私有头与平行件归属待归集）。
- 未改动：两个 Makefile 的源文件条目、`event_log*`、`services.c`、`shell*.c` 实现、`arch/` 目录结构。

## 2. 审计执行与结论

命令：

```bash
python3 tools/audit_layout.py
```

实测输出（基线 `9c942dc`）：

```text
=== 布局审计（audit_layout）===
32 位源文件: 502  64 位源文件: 156  shared: 13/13
shared 仅 32 位: 0  仅 64 位: 0
归属错位: 0  平行实现 INFO: 2
vpath 缺口: 0  反向依赖: 0
磁盘缺失: 0  32 位泄漏 x86_64: 0
  INFO  平行: kernel/boot/acpi.c <> kernel/arch/x86_64/acpi.c
  INFO  平行: kernel/sched/smp.c <> kernel/arch/x86_64/smp.c
errors=0
```

- 13 个 shared `.c` 在 32/64 两侧收录一致；无共用模块单边编译。
- 无 `shared/` → `arch/` 反向依赖；含 `.c` 的 shared 子目录均在 64 位 `vpath %.c` 列表中。
- 2 条 INFO 为架构平行件（非归属错位，不阻断）：`boot/acpi.c` ↔ `arch/x86_64/acpi.c`；`sched/smp.c` ↔ `arch/x86_64/smp.c`。
- 机器可读报告：`layout-audit-report.md`。

## 3. 构建验证

| 项 | 命令 | 结果 |
|---|---|---|
| 32 位构建 | `flock /tmp/hepu-build.lock -c 'make -j2'` | 退出 0，无 `error:`；产物 `build_v250/hepu-kernel.bin`（3.7M） |
| 64 位构建 | `flock /tmp/hepu-build.lock -c 'make -C kernel/arch/x86_64 -j2'` | 退出 0，无 `error:`；产物 `build_x86_64/hepu-kernel64.bin`（985K） |

本轮未改 64 位源码与 64 位清单；64 位仅做构建验证，未另跑 UEFI 启动取证（无 64 位行为变更）。

## 4. 32 位冒烟验证

### 4.1 无盘模式（p3-security 两项显式 SKIP 属预期）

命令：

```bash
HEPU_EV=$PWD/docs/status/evidence-layout-2026-11-26/ev-nodisk \
  bash tools/regress/smoke32.sh --no-disk 70
```

关键片段（`ev-nodisk/tests/smoke32.txt`）：

```text
mode=nodisk disk=/tmp/hepu-smoke32-test.img
serial=1 initrd=1 net=1 desk=1 fault=0 lines=110944
ext2_mounted=0 ext2_nodisk=1
ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=2 p3_skip=2
p3-security: passed=0 failed=0 skipped=2
[KUNIT-SKIP] p3-security/s3_normal_user_denied (ext2 backend unavailable)
[KUNIT-SKIP] p3-security/posix_error_table (ext2 backend unavailable)
[regress] ✓ 冒烟通过（mode=nodisk）
```

### 4.2 带盘模式（p3-security 必须真实执行）

命令：

```bash
HEPU_EV=$PWD/docs/status/evidence-layout-2026-11-26/ev-disk \
  bash tools/regress/smoke32.sh --disk 70
```

关键片段（`ev-disk/tests/smoke32.txt`）：

```text
mode=disk disk=/tmp/hepu-smoke32-test.img
serial=1 initrd=1 net=1 desk=1 fault=0 lines=107554
ext2_mounted=1 ext2_nodisk=0
ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=0 p3_skip=0
p3-security: passed=2 failed=0 skipped=0
--- failed suites ---
(none)
--- failed cases ---
(none)
--- skipped cases ---
(none)
[regress] ✓ 冒烟通过（mode=disk）
```

串口关键行摘录：`ev-nodisk/tests/serial-key.txt`、`ev-disk/tests/serial-key.txt`（完整 8MB 原始串口日志不落库；摘要见各自 `smoke32.txt`）。

观察项（非本轮回归）：带盘串口在 ext2 自测段出现一行 `[EXT2] read /hello.txt FAIL`，紧随其后的 `write+readback /test_hepu.txt PASS`、`mkdir /docs PASS`、`create /docs/note.txt PASS` 均通过，最终 `[KTEST] RESULT: ALL PASS`、`p3-security: passed=2 failed=0 skipped=0`。本轮未改任何文件系统源码，该行为与 32 位 ext2 自测基线一致；如需排查应另立 FS 路跟进。

## 5. 文档检查

命令：`python3 tools/check_docs.py`

结果：`当前版本: 0.62.1`，`warns=0 errors=0`，退出 0。新设计文档已由 `tools/gen_docs_index.py` 登记进 `docs/README.md` 的 `design` 分组。

## 6. 结论

- 本轮审计未发现缺陷级布局分歧；按「先设计后最小迁移」原则，**本轮未迁移**任何目录或文件。
- 已落地可执行红线检查（`audit_layout.py`），后续新增 shared 模块可由脚本自动发现单边收录 / vpath 缺口 / 反向依赖。
- 分阶段迁移路线（私有头归位 → 日志旁路收敛 → Shell/服务表下沉 → 驱动/FS 平行件归集）见设计文档第 6 节，每阶段独立可回滚。

## 7. 未覆盖项

1. 未证明两侧平行实现的行为等价性，只核对文件归属与构建收录；等价性核对属长周期阶段 4 前置工作。
2. 32 位 Shell CLI 交互未覆盖（无串口输入），命令行为验证走 KUnit 带盘路径，与规范口径一致。
3. `kernel/include` 中 32 位历史私有头的完整消费方清单未逐个复核，已登记 OPEN-07。
4. 64 位本轮无源码/清单改动，未跑 UEFI 启动取证；若后续阶段 1 移动 64 私有头，需补 64 位启动取证。
