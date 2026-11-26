# OPEN-09 64 位 IRQ 后停机：根因与修复证据

日期：2026-11-26
状态：已修复并验证（64 位 UEFI 两次到桌面；32 位带盘/无盘回归通过）

## 1. 现象

四路合入本地 `main` 后，64 位 UEFI（OVMF，`snapshot=off`）串口停在：

```
[IRQ64] Interrupts enabled (PIC mode)
```

之后没有 `[OK] Early IDT installed`、`[EXT2_64]`、`[EVENT] saved`，也没有桌面。此前判为“停机”，QEMU monitor 看到 CPU 处于 `HLT`；实际上串口随后出现 UEFI 重新加载字符，属于**复位循环**，不是单纯 idle。

## 2. 根因

`kernel/arch/x86_64/entry_64.S` 的 UEFI 入口 `x86_64_entry_64` 仍把 RSP 设为 `0x90000`，即只使用引导器 `jump_to_kernel_64` 留下的 `0x8F000–0x90000` 4KB 低内存栈。该跳板把临时 GDT 放在 `0x8F050`，**正好位于这段栈的范围内**。

四路合入后 `irq64_init()` 的调用深度/栈帧增加，压栈越过 `0x8F050`，覆盖 GDT[1]（内核代码段选择子 `0x08` 的描述符）。`sti` 后首个 PIT IRQ（向量 0x20）投递时，CPU 按 `0x08` 查 GDT 门描述符失败，触发 `#GP(0x08)`；异常处理链再次失败，转 `#DF`，最终 triple fault，机器复位回 UEFI。

QEMU `-d int,cpu_reset` 的现场（`qemu-int-before-fix.txt`）：

```
v=20 e=0000 ... IP=0008:000000000010618f SP=0010:000000000008f320
check_exception old: 0xffffffff new 0xd
v=0d e=0008 ... IP=0008:000000000010618f SP=0010:000000000008f320
check_exception old: 0xd new 0xd
v=08 e=0000 ... IP=0008:000000000010618f SP=0010:000000000008f320
check_exception old: 0x8 new 0xd
Triple fault
```

`SP=0x8F320` 说明中断发生在 `0x8F000–0x90000` 这段 4KB 低内存栈上，`e=0008` 的选择子进一步指向 GDT[1] 被覆盖。根因**不是**事件日志/服务监督/系统管理命令的业务代码，而是这些代码增加了早期栈占用，暴露了已有的入口栈布局缺陷。

## 3. 修复

在 `kernel/arch/x86_64/entry_64.S` 的 `x86_64_entry_64` 中，BSS 清零之后、`call x86_64_kernel_main` 之前切换栈：

```asm
    rep stosq

    /* BSS 清零后再切到内核自有 64KB 栈（0x1FA000-0x20A000）。 */
    movq $hepu_kstack_top, %rsp
    xorq %rbp, %rbp

    call x86_64_kernel_main
```

关键点：

- 必须放在 `rep stosq` **之后**：BSS 清零范围包含 `hepu_kstack`，提前切换会把正在使用的栈一起清零。
- `hepu_kstack_top` 是 `entry_64.S` 定义的 64KB 栈顶（`0x20A000`），同时被 PMM 保留为 `0x1FA000–0x20A000`。
- 低内存 4KB 栈只继续用于 `x86_64_set_boot_info` 和 BSS 清零这两步，之后不再承载中断。

## 4. 验证

### 4.1 构建

```bash
HEPU_EV=/tmp/hepu-open09b bash tools/regress/build_all.sh
```

产物：`/tmp/hepu-open09b/build/hepu.img`，`kernel64.bin` 1,023,504 B、`kernel32.bin` 3,863,725 B、`bootx64.efi` 106,844 B。

### 4.2 64 位 UEFI 真启动（两次）

OVMF pflash、`-m 1024 -smp 2`、`snapshot=off`，同一镜像连续启动两次：

- `ser64-fixed-first.log`
- `ser64-fixed-second.log`

两次都出现：

```
[IRQ64] Interrupts enabled (PIC mode)
[OK] Early IDT installed
[EXT2_64] ext2 found on master drive
[EVENT] early save to /hepu_events.log
[DESKTOP64] Desktop initialized: 1920x1080
[EVENT] saved to /hepu_events.log
[BOOT] desktop_frame1
```

第二次启动截图 `screen64-lockscreen.png` 为 1920×1080 锁屏桌面，时间/日期正常渲染，不是黑屏或 UEFI 界面。

### 4.3 落盘日志

从镜像 P1（LBA 264192）用 `debugfs` 读取 `/hepu_events.log`，文件 2,605 B，内容见 `hepu_events.log`。包含 `boot64`、`service`、`kernel64` 的 `[BOOT] ext2 root mounted`、`[GUI] desktop environment ready` 等事件。

注：文件同时含 early save 与最终 save 的两段快照（会重复 early 段），属既有“每次保存写整个环”的行为，不在 OPEN-09 范围内。

### 4.4 32 位回归

- 带盘 `bash tools/regress/smoke32.sh`：`ktest_result=PASS`、`fail_suites=0`、`fail_cases=0`、`skip_cases=0`、`p3-security passed=2 failed=0`；`[KTEST] RESULT: ALL PASS`（`event-log 6/6`、`system admin commands 7/7`、`system-manager 6/6`）。
- 无盘 `bash tools/regress/smoke32.sh --no-disk`：`ALL PASS`、`skip_cases=6`、`p3_skip=2`；6 条 SKIP 全部为 `ext2 backend unavailable`（2 条 p3、2 条 event-log、2 条 admin），未计为 PASS。
- `python3 tools/check_docs.py`：`warns=0 errors=0`。

摘要见 `ser32-disk-summary.txt`、`ser32-nodisk-summary.txt`。

## 5. 证据文件

| 文件 | 内容 |
|------|------|
| `qemu-int-before-fix.txt` | 修复前 QEMU `-d int` 的 IRQ0→#GP(8)→#DF→Triple fault 现场 |
| `ser64-fixed-first.log` | 修复后第一次 64 位 UEFI 串口 |
| `ser64-fixed-second.log` | 修复后第二次 64 位 UEFI 串口 |
| `screen64-lockscreen.png` | 第二次启动后的 1920×1080 锁屏截图 |
| `hepu_events.log` | 镜像内 `/hepu_events.log` 落盘内容 |
| `ser32-disk-summary.txt` | 32 位带盘冒烟摘要 |
| `ser32-nodisk-summary.txt` | 32 位无盘冒烟摘要 |
| `smoke32-disk.txt` / `smoke32-nodisk.txt` | 冒烟脚本原始输出 |
