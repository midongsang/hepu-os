# OBS-01 默认 HPET 桌面首帧不推进：毫秒口径修复

日期：2026-11-26
状态：已修复并验证（默认 HPET 两次 64 位 UEFI 启动到锁屏；32 位带盘/无盘通过）

## 1. 现象

默认 HPET 参数启动 64 位 UEFI 时，串口已经打印：

```
[IRQ64] Interrupts enabled (PIC mode)
[OK] Early IDT installed
[EXT2_64] ext2 found on master drive
[DESKTOP64] Desktop initialized: 1920x1080
[DESKTOP64] Desktop running with mouse support
```

但没有 `[BOOT] desktop_frame1`，画面停在引导加载屏（顶部蓝色渐变、其余黑色），不是锁屏桌面。加 `-no-hpet` 时同一镜像可正常出首帧，说明入口/IRQ 链已通，问题在 PIT 不投递时的桌面帧节流时钟。

## 2. 根因

`kernel/arch/x86_64/pit64.c` 中两个入口口径不一致：

- `pit64_get_ticks()` 已经用 `g_kernel_tick64`（1kHz APIC tick）兜底，取
  `max(apic_tick, pit_ticks)`，PIT 冻住时仍单调推进；
- `pit64_get_ms()` 却直接返回 `pit_ticks`，绕开了这个兜底。

桌面主循环 `desktop64.c` 用 `pit64_get_ms()` 判断
`now - last_render_ms >= 16`。部分启动里 PIT IRQ 不投递，`pit_ticks` 冻住，
差值恒为 0，`desk_tick()` 永远不被调用，因此没有 `desktop_frame1`，桌面首帧
一直不画。这是已登记 OBS-01 的一个独立下游出口，与 OPEN-09 的入口栈/IRQ
修复无关。

## 3. 修复

`pit64_get_ms()` 改为复用 `pit64_get_ticks()`：

```c
uint64_t pit64_get_ms(void)
{
    /* PIT IRQ 不投递时 pit_ticks 冻住；pit64_get_ticks() 已把
     * g_kernel_tick64 的 1kHz APIC tick 纳入兜底，毫秒入口必须
     * 走同一口径。 */
    return pit64_get_ticks();
}
```

反汇编确认 `pit64_get_ms` 尾部直接 `jmp pit64_get_ticks`（`0x15a562`），
没有再读裸 `pit_ticks`。

## 4. 验证

### 4.1 构建

```bash
HEPU_EV=/tmp/hepu-obs01 bash tools/regress/build_all.sh
```

产物：`/tmp/hepu-obs01/build/hepu.img`，`kernel64.bin` 1,023,504 B、
`kernel32.bin` 3,863,725 B、`bootx64.efi` 106,844 B。

### 4.2 默认 HPET 64 位 UEFI（两次）

OVMF pflash、`-m 1024 -smp 2`、`snapshot=off`，**不加 `-no-hpet`**，同一镜像
连续启动两次：

- `ser64-obs01-first.log`
- `ser64-obs01-second.log`

两次都出现：

```
[BOOT] Mode: 64-bit x86_64
[IRQ64] Interrupts enabled (PIC mode)
[OK] Early IDT installed
[EXT2_64] ext2 found on master drive
[DESKTOP64] Desktop initialized: 1920x1080
[DESKTOP64] Desktop running with mouse support
[BOOT] desktop_frame1
```

第二次启动截图 `screen64-obs01-lockscreen.png` 为 1920×1080 锁屏桌面，
时间/日期正常渲染。

### 4.3 中断现场

第二次启动同时用 `-d int,cpu_reset` 记录；完整日志 1,015,201,054 字节，
仅留摘要 `qemu-int-obs01-summary.txt`：

- `Triple fault` 次数：0
- `v=0d e=0008` 次数：0

### 4.4 落盘日志

从镜像 P1（LBA 264192）用 `debugfs` 读取 `/hepu_events.log`，文件 5,210 B，
内容见 `hepu_events-obs01.log`；包含 `boot64`、`service`、`kernel64` 的
`[BOOT] ext2 root mounted`、`[BOOT] network up`、`[ACCOUNT] configured`、
`[GUI] desktop environment ready` 等事件。

### 4.5 32 位回归

- 带盘：`ktest_result=PASS`、`fail_suites=0`、`fail_cases=0`、`skip_cases=0`、
  `p3-security passed=2 failed=0`；原始输出 `smoke32-disk.txt`。
- 无盘：`ktest_result=PASS`、`skip_cases=6`、`p3_skip=2`；6 条 SKIP 均为
  `ext2 backend unavailable`，未计 PASS；原始输出 `smoke32-nodisk.txt`。
- `python3 tools/check_docs.py`：`warns=0 errors=0`，见 `check_docs.txt`。
- `python3 tools/check_stage2_bss.py`：6/6 PASS，见 `check_stage2.txt`。

## 5. 证据文件

| 文件 | 内容 |
|------|------|
| `ser64-obs01-first.log` / `ser64-obs01-second.log` | 默认 HPET 两次 64 位 UEFI 串口 |
| `screen64-obs01-lockscreen.png` | 第二次启动后的 1920×1080 锁屏截图 |
| `hepu_events-obs01.log` | 镜像内 `/hepu_events.log`（5,210 B） |
| `qemu-int-obs01-summary.txt` | 默认 HPET `-d int,cpu_reset` 摘要：0 triple fault、0 `v=0d e=0008` |
| `smoke32-disk.txt` / `smoke32-nodisk.txt` | 32 位带盘/无盘原始输出 |
| `check_docs.txt` / `check_stage2.txt` | 文档检查与 6 条入口/栈断言输出 |
