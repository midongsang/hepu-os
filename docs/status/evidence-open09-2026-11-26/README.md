# OPEN-09 64 位 IRQ 后停机：根因、两轮入口加固与独立证据

日期：2026-11-26
状态：已修复并验证（第二轮入口加固后，64 位 UEFI 两次到锁屏；32 位带盘/无盘回归通过）

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

### 3.1 第一版（提交 50e6e52）

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
- 低内存 4KB 栈只继续用于 `x86_64_set_boot_info` 和 BSS 清零这两步。

### 3.2 第二版（本轮加固）

第一版之后，低 4KB 栈上仍要调用一次 C 函数 `x86_64_set_boot_info`（虽然它只写两个全局）。为彻底关闭这个早期 C 调用窗口，第二轮把调用也移到切内核栈之后：

1. `x86_64_entry_64` 首条指令保持 `movl $0x2BADB002, %edi`（引导器魔数校验依赖），随后把 magic 存 `%r12d`、`mb_info` 存 `%r13`；
2. 进入 `kernel_gdt64_flush` 后只做段寄存器设置和 `rep stosq` 清 BSS，不调用 C；
3. BSS 清零完成后切 `hepu_kstack_top`、清 `%rbp`；
4. 再恢复 `%r12d → %edi`、`%r13 → %rsi`，调用 `x86_64_set_boot_info`；
5. 最后调用 `x86_64_kernel_main`。

最终入口关键序列：

```asm
x86_64_entry_64:
    movl $0x2BADB002, %edi        /* 首 5 字节，引导器魔数校验依赖 */
    movl %edi, %r12d
    movq %rbx, %r13
    jmp kernel_gdt64_flush

kernel_gdt64_flush:
    ... 段寄存器 ...
    rep stosq                     /* 清 BSS */
    movq $hepu_kstack_top, %rsp   /* 之后才切栈 */
    xorq %rbp, %rbp
    movl %r12d, %edi
    movq %r13, %rsi
    call x86_64_set_boot_info
    call x86_64_kernel_main
```

**必须保留引导器魔数**：`tools/hepboot/uefi/hepboot_uefi.c` 的 `load_kernel_to_memory()` 会校验内核镜像前 5 字节必须是 `BF 02 B0 AD 2B`（`movl $0x2BADB002,%edi`）。本轮第一次尝试把首条指令改成 `movl $0x2BADB002,%r12d`（机器码 `41 BC ...`），校验失败后引导器**静默回退到 32 位内核**，串口先打印 `[BOOT] Mode: 64-bit x86_64`，随后运行 32 位 `[SERIAL]`；`-d int` 现场为 `CS32`、`EFER.LMA=0`。因此修复必须保留 5 字节魔数作为首指令，再用一条 `movl %edi, %r12d` 转存。

### 3.3 构建期硬断言

`tools/check_stage2_bss.py` 从 4 条规则扩展到 6 条：

- **规则 5**：`hepu_kstack_bottom/top` 不得与引导器栈 `0x8F000–0x90000`（临时 GDT `0x8F050`）重叠，且必须落在 BSS 内。
- **规则 6**：`entry_64.S` 首条指令必须是 `movl $0x2BADB002,%edi`；`hepu_kstack_top` 切换必须发生在 `rep stosq` 之后；切栈前不得出现 `call`。

本轮实测：

```
[✔] 规则 5 PASS: 内核栈 0x1fa000-0x20a000 与引导器栈 0x8f000-0x90000 不重叠
[✔] 规则 6 PASS: 入口首指令为 0x2BADB002 魔数，BSS 清零后切 hepu_kstack_top，切栈前无 call
[PASS] 全部安全检查通过 (6 条规则)
```

原始输出见 `check_stage2-final.txt`。

## 4. 验证

### 4.1 构建

```bash
HEPU_EV=/tmp/hepu-open09d bash tools/regress/build_all.sh
```

产物：`/tmp/hepu-open09d/build/hepu.img`，`kernel64.bin` 1,023,504 B、`kernel32.bin` 3,863,725 B、`bootx64.efi` 106,844 B。

新入口反汇编确认：`0x100000` 首字节为 `bf 02 b0 ad 2b`，随后 `41 89 fc`（`mov %edi,%r12d`）、`49 89 dd`（`mov %rbx,%r13`）；切栈前没有 C 调用。

### 4.2 64 位 UEFI 真启动（两次）

OVMF pflash、`-m 1024 -smp 2`、`snapshot=off`，同一镜像连续启动两次。本轮取证加 `-no-hpet`，原因见 4.5；OPEN-09 的入口/IRQ 修复与 HPET 无关。

- `ser64-entryv2-first.log`
- `ser64-entryv2-second.log`

两次都出现：

```
[BOOT] Mode: 64-bit x86_64
[IRQ64] Interrupts enabled (PIC mode)
[OK] Early IDT installed
[EXT2_64] ext2 found on master drive
[EVENT] early save to /hepu_events.log
[DESKTOP64] Desktop initialized: 1920x1080
[DESKTOP64] Desktop running with mouse support
[BOOT] desktop_frame1
```

第二次启动截图 `screen64-entryv2-lockscreen.png` 为 1920×1080 锁屏桌面，时间/日期正常渲染，不是黑屏或 UEFI 界面。

### 4.3 中断现场

第二次启动同时用 `-d int,cpu_reset` 记录；完整日志 44,931,251 字节，仅留摘要 `qemu-int-entryv2-summary.txt`：

- `Triple fault` 次数：0
- `v=0d e=0008` 次数：0

即修复后 `sti` 后首个 PIT/其他中断不再触发 `#GP(0x08)` 链。

### 4.4 落盘日志

从镜像 P1（LBA 264192）用 `debugfs` 读取 `/hepu_events.log`，第二轮文件 7,815 B，内容见 `hepu_events-entryv2.log`。包含 `boot64`、`service`、`kernel64` 的 `[BOOT] ext2 root mounted`、`[BOOT] network up`、`[ACCOUNT] configured`、`[GUI] desktop environment ready` 等事件。

注：文件同时含 early save 与最终 save 的两段快照（会重复 early 段），属既有“每次保存写整个环”的行为，不在 OPEN-09 范围内。

### 4.5 关于 `-no-hpet` 与 OBS-01

默认 HPET 启动时，本轮复跑多次都停在引导器加载屏：串口已打印 `[DESKTOP64] Desktop initialized`、`Desktop running`，但没有 `desktop_frame1`。根因是已登记的 **OBS-01/BUG-49 边界**：部分启动里 PIT IRQ 不投递，`pit64_get_ms()` 仍直接读 `pit_ticks`，帧节流差值恒为 0；`pit64_get_ticks()` 有 APIC tick 兜底，但 `pit64_get_ms()` 没有。该问题与 OPEN-09 的入口栈/IRQ 修复无关，本轮不混入修复；64 位取证固定加 `-no-hpet` 让 PIT 路径可用，以验证入口本身。`-d int` 中无 triple fault。

第一版 `50e6e52` 的首轮证据（默认 HPET、两次到桌面）仍完整保留：`ser64-fixed-first.log`、`ser64-fixed-second.log`、`screen64-lockscreen.png`、`hepu_events.log`。

### 4.6 32 位回归

- 带盘 `bash tools/regress/smoke32.sh --disk`：`ktest_result=PASS`、`fail_suites=0`、`fail_cases=0`、`skip_cases=0`、`p3-security passed=2 failed=0`；原始输出 `smoke32-disk-final.txt`。
- 无盘 `bash tools/regress/smoke32.sh --no-disk`：`ALL PASS`、`skip_cases=6`、`p3_skip=2`；6 条 SKIP 全部为 `ext2 backend unavailable`（2 条 p3、2 条 event-log、2 条 admin），未计为 PASS；原始输出 `smoke32-nodisk-final.txt`。
- `python3 tools/check_docs.py`：`warns=0 errors=0`，见 `check_docs-final.txt`。

## 5. 独立二分证据（Organizer 路）

为排除“服务逻辑本身有问题”的假设，另一条工作树（`wt-o09`，分支 `test/open09-bisect`，基线 `33102f1`）做了独立二分，日志复制在 `independent/`：

| 文件 | 实验 | 结果 |
|------|------|------|
| `independent/probe-revert-svc.log` | 回退服务路三提交 `b3274bc 9168251 9a48877` | 64 位完整启动到桌面（`desktop_frame1`） |
| `independent/probe-only-core.log` | 只保留 `9a48877`（服务核心），其余三路回退 | 仍停在 `[IRQ64] Interrupts enabled`，之后无输出 |
| `independent/probe-skip-boot.log` | 同一棵含 `9a48877` 的树，仅跳过 `services_start_boot()` | 64 位完整启动到桌面（`desktop_frame1`） |
| `independent/probe-irq-dbg.log` | `sti` 后插 `[DBG] post-sti ok`、`pit64_interrupt()` 首行插 `[DBG] first pit tick` | 两条都没打印，排除 handler 内死循环/IRQ 风暴，CPU 在首个中断交付时已经跑飞 |

`independent/regression64-irq-hang-notes.md` 记录服务路早期 A/B：把 `hs_audio_init()` 移回桌面阶段仍停机，音频假设被证伪。该记录写于根因定位前，结论已被本 README 第 2 节取代，保留仅作对照。

独立结论与主路一致：`services_start_boot()` 只是把早期栈帧压深的触发器，不是服务逻辑缺陷；修复方向是入口栈布局。

## 6. 证据文件

| 文件 | 内容 |
|------|------|
| `qemu-int-before-fix.txt` | 修复前 QEMU `-d int` 的 IRQ0→#GP(8)→#DF→Triple fault 现场 |
| `ser64-fixed-first.log` / `ser64-fixed-second.log` | 第一版（50e6e52）两次 64 位 UEFI 串口 |
| `screen64-lockscreen.png` | 第一版第二次启动后的 1920×1080 锁屏截图 |
| `hepu_events.log` | 第一版镜像内 `/hepu_events.log`（2,605 B） |
| `ser64-entryv2-first.log` / `ser64-entryv2-second.log` | 第二版两次 64 位 UEFI 串口（`-no-hpet`） |
| `screen64-entryv2-lockscreen.png` | 第二版第二次启动后的 1920×1080 锁屏截图 |
| `hepu_events-entryv2.log` | 第二版镜像内 `/hepu_events.log`（7,815 B） |
| `qemu-int-entryv2-summary.txt` | 第二版 `-d int,cpu_reset` 摘要：0 次 triple fault、0 次 `v=0d e=0008` |
| `check_stage2-final.txt` | 6 条构建期硬断言输出 |
| `smoke32-disk-final.txt` / `smoke32-nodisk-final.txt` | 32 位带盘/无盘回归原始输出 |
| `check_docs-final.txt` | `check_docs.py` 输出（warns=0 errors=0） |
| `independent/` | Organizer 路独立二分日志与音频 A/B 记录 |
| `ser32-disk-summary.txt` / `ser32-nodisk-summary.txt` | 第一版 32 位冒烟摘要（保留） |
