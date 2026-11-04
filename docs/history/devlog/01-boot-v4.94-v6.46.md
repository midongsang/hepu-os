---
title: 01-boot-v4.94-v6.46
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

# Hepu OS 引导器排障错误历史

> 本文档记录 Hepu OS 引导链（bootsect → stage2 → kernel）开发与排障过程中遇到的所有错误、根因分析、修复方法及教训，供后续开发者参考，避免重复踩坑。
>
> 最后更新：引导器迭代 V6.46（对应系统镜像 V1.02）。BUG-10 递归中断已修复，系统成功启动到内核；代码巩固完成，stage2=1784B，3次启动验证稳定。

---

## 错误总览

| 编号 | 版本 | 错误类型 | 严重程度 | 状态 |
|------|------|----------|----------|------|
| BUG-01 | V4.94 | 寄存器依赖 BIOS 保留 | **致命（系统无法启动）** | 已修复 |
| BUG-02 | V4.94c | 寄存器跨调用被破坏 | 中（调试显示错误） | 已修复 |
| BUG-03 | V4.94d | 除法余数/被除数被覆盖 | 中（调试显示错误） | 已修复 |
| BUG-04 | V4.95 | CHS 读盘几何越界 | **高（SEG2 无法读取）** | 已修复 |
| BUG-05 | V4.96 | 实模式 CS D 位未刷新 | **致命（回实模式崩溃）** | 已修复 |
| BUG-06 | V4.96c | ES:BX 地址拆分错误 | **致命（内核数据错误）** | 已修复 |
| BUG-07 | V4.96/97 | 清PE后CS.base被QEMU TCG重置 | **致命（回实模式三重故障）** | 已修复（V6.32 16位代码段方案） |
| BUG-08 | V6.13 | GAS `as --32`下`.code16`段jmp/jnz被编码为32位rel32 | **高（指令流错位）** | 已修复（手动编码16位rel16） |
| BUG-09 | V6.32 | 清PE后16位远跳转加0x66前缀致选择子读错 | **致命（回实模式崩溃）** | 已修复（用`.byte 0xEA`手动编码） |
| BUG-10 | V6.35+ | **BIOS INT13切换栈致IRET返回地址错误→递归中断** | **致命（SEG2无法加载）** | **已修复（V6.45 IDTR重置+磁盘重置）** |
| BUG-11 | V6.39 | .bss段变量被BIOS磁盘缓冲覆盖 | **高（递归检测失效）** | 已规避（变量移到0x4000自由内存） |
| BUG-12 | V6.38 | INT13包装器IVT写入绝对地址而非偏移 | **致命（早期崩溃）** | 已发现（包装器方案整体放弃） |
| BUG-13 | V6.40 | ATA PIO发送读命令后BSY一直为1 | **高（替代方案失败）** | 待解决（需进一步调试IDE初始化） |
| BUG-14 | V6.41 | 回实模式后FS/GS未清零（保护模式值0x10残留） | 中（可能影响BIOS行为） | 已修复（V6.42 rm_read_seg2中清零） |
| BUG-15 | V6.41 | 递归检测放在循环入口干扰SEG1正常读盘 | **高（SEG1数据错误）** | 已规避（仅保留入口检测，循环入口不加） |
| BUG-16 | V6.50 | **重构时删除递归检测代码导致系统无法启动** | **致命（串口无输出）** | **已修复（恢复V6.45稳定版递归检测）** |
| BUG-17 | V6.50 | **VBE BIOS调用(INT10 AX=4F02)在SeaBIOS下破坏实模式状态** | **致命（VBE设置后内核无法启动）** | **已规避（VBE暂禁用，用GRUB分支）** |
| TOOL-01 | — | GAS `jmp .` 非原地死循环 | 中（诊断误导） | 已规避 |
| TOOL-02 | — | objdump 16位反汇编误读32位指令 | 低（诊断误导） | 已规避 |
| ENV-01 | — | stage2 2048B 硬限制 | 中（空间紧张） | 已知约束 |
| ENV-02 | — | QEMU 环境周期性重置 | 低（开发效率） | 已知 workaround |

---

## BUG-01：read_slice 依赖 BIOS 保留 EBX=1（主因）

### 现象
系统启动后卡死在引导器读盘阶段，VGA 显示 `H2GAR` 后无后续标记，从未进入内核。QEMU 中 CPU 执行垃圾代码死循环。

### 根因
`read_slice` 读盘循环中，用 `addl %ebx,%ebp`（LBA += 1）和 `addl %eax,%edi`（缓冲地址 += 512）推进，完全依赖 INT13 返回后 EBX=1。

但 **BIOS 不保证保留 BX 寄存器**——SeaBIOS 实测 INT13 返回后 BX 被改写为 0xD980 等随机值。导致：
- EBP（LBA）跳飞 → 读错误扇区
- EDI（缓冲地址）跳飞 → ES 变垃圾段 → CPU 执行垃圾代码

### 修复
读块成功后强制 `movl $1,%ebx`，不依赖 BIOS 保留。

### 教训
- **x86 实模式下，INT 调用后只有 AX（状态码）和指定的输出寄存器有定义，其他通用寄存器可能被破坏。**
- 引导器代码中，任何依赖寄存器跨 INT 调用保持不变的逻辑都是隐患。

---

## BUG-02：print_hex8 低 4 位恒为 0

### 现象
所有 hex 调试输出的低 4 位恒为 0（如显示 `A0` 而非 `AB`）。

### 根因
`print_hex8` 打印低 4 位时依赖 AH 跨 `hex_digit` 调用保持，但 `hex_digit` 内部用 EAX 计算 VGA 偏移，破坏了 AH。

### 修复
用 CL 保存原值，不依赖 AH 跨调用保持。

### 教训
- 16 位代码中，AX 的高/低字节（AH/AL）容易被子函数意外破坏。需要跨调用保持时，用栈或其他寄存器保存。

---

## BUG-03：print_dec32 双重 bug

### 现象
十进制显示 `b=0000` 而非 `b=2246`。

### 根因
两个独立 bug 叠加：
1. `movzbl vga_col,%edx` 在除法循环中覆盖了 EDX（除法余数）
2. `movw $0xB800,%ax` 在保存被除数之前覆盖了 EAX 低 16 位

### 修复
- 用 EBX 保存余数
- 先保存被除数再设 ES

### 教训
- 除法指令 `div` 隐含使用 EDX:EAX，任何修改 EDX/EAX 的指令必须在除法前后 careful 安排。
- 调试函数本身的 bug 会误导排障方向——先验证调试输出的正确性。

---

## BUG-04：CHS 读盘受 SeaBIOS 几何限制

### 现象
SEG2（内核后半部分，LBA 1157-2250）无法读取，INT13 返回 AH=01（非法命令）。

### 根因
原 `read_slice` 使用 CHS 读盘（INT13 AH=02），需要将 LBA 转换为 C/H/S。SeaBIOS 报告的磁盘几何为 PCHS=2/16/63=2016 扇区，LBA≥2016 时转换越界。

SEG2 的 LBA 范围 1157-2250 超过 2016，CHS 读盘失败。

### 修复
将 `read_slice` 从 CHS（AH=02）改为 LBA 扩展读盘（AH=42），使用 DAP（磁盘地址包）结构，支持 64 位 LBA，不受 CHS 几何限制。

DAP 结构放在 .bss 段（地址 0x87ee），注意：
- DAP 的 size 字段不能放 .bss（非零值），需运行时 `movb $0x10, dap_packet` 设置
- 一次读 1 块（sectors=1），避免跨 64KB 边界和 DMA 限制

### 教训
- **CHS 读盘在现代系统上已不可靠**，磁盘几何可能被 BIOS 截断或翻译。引导器应优先使用 LBA 扩展（AH=42）。
- 使用 LBA 扩展前应检查 BIOS 是否支持（AH=41），但 SeaBIOS/QEMU 均支持。

---

## BUG-05：回实模式 CS D 位未刷新（致命）

### 现象
搬移 SEG1 后回实模式读 SEG2，CPU 三重故障复位。VGA 被清屏，CPU 在 BIOS 中。

### 根因
回实模式时，直接在保护模式下清 CR0.PE=0，然后 ljmp 到实模式段。但 **实模式下加载 CS 不刷新 CS 隐藏属性的 D 位**——QEMU TCG（及真机）保持保护模式的 D=1。

D=1 导致后续 16 位代码被 32 位解码：
- `movw $0x9000,%sp` 被误读为 `movl $0x????9000,%esp`
- 指令错位 → 执行垃圾 → 三重故障复位

### 修复
三步回实模式：
1. 保护模式（D=1）下先 ljmp 到 code16 段（GDT 新增第4个描述符，选择器 0x18，D=0，limit=64K）
2. 在 code16 段（D=0）中清 CR0.PE=0（**必须加 data32 前缀**，否则 `mov %cr0,%eax` 只读写 AX 低16位，EAX/CR0 高16位垃圾→CR0 状态损坏→三重故障）
3. 再 ljmp 到实模式段 CS=0x0800（D 保持 0，base=0x8000）

GDT 从 3 个描述符扩为 4 个，GDTR limit 从 23 改为 31。

### 教训
- **x86 实模式与保护模式切换时，段寄存器的隐藏属性（base/limit/D/B）是关键。** 实模式下加载段寄存器不保证刷新所有隐藏属性。
- 从保护模式回实模式，必须先切到 D=0 的 16 位代码段，再清 PE，最后切到实模式段。这是 Intel 手册推荐的流程。
- `mov %cr0,%eax` 在 16 位模式下默认只读写 AX，必须用 data32 前缀强制 32 位操作数。

---

## BUG-06：ES:BX 地址拆分错误（原始致命 bug）

### 现象
SEG1 读盘后，物理内存 0x100000 有正确的内核头（魔数正确），但内核大部分内容错误。内核启动后 T0 探针异常、9999 刷屏、shell 自动测试失败。

### 根因
`read_slice` 中设置 ES:BX 缓冲地址时：
```asm
movl %edi, %eax
movw %ax, %bx          # BX = EDI 低16位（错误！）
shrl $4, %eax
movw %ax, %es           # ES = EDI >> 4
```

物理地址 = ES<<4 + BX = (EDI>>4)<<4 + (EDI&0xFFFF)。

正确的实模式段:偏移拆分应该是：
- 段地址 = 物理地址 >> 4
- 偏移 = 物理地址 & 0xF（只低4位！）
- 物理地址 = (段地址 << 4) + 偏移

原代码用偏移 = EDI & 0xFFFF（低16位），导致物理地址计算错误。对于 512 对齐地址（低4位=0），每块实际间隔 1024 字节而非 512——**数据稀疏存储**，每块只有前 512 字节正确，后 512 字节是上一块残留。

这是从 CHS 版本（V4.93）就存在的原始 bug，之前因内核头只在第一块、魔数校验只查前几十字节而未暴露。

### 修复
在 `movw %ax,%bx` 后添加 `andl $0xF,%ebx`（只保留低4位偏移）：
```asm
movw %ax, %bx
andl $0xF, %ebx          # BX = EDI & 0xF（只保留低4位偏移）
shrl $4, %eax
movw %ax, %es
```

### 教训
- **实模式段:偏移地址拆分是易错点。** 偏移必须只占低 4 位（& 0xF），段地址 = 物理地址 >> 4。
- 512 对齐地址的低 4 位恒为 0，所以稀疏存储 bug 不容易通过"第一块正确"发现。必须验证整块数据的连续性（如检查 0x100000+512 处是否正确）。
- 魔数校验只查前几十字节，不能证明整块数据正确。

---

## TOOL-01：GAS 中 `jmp .` 不是原地死循环

### 现象
在代码中放置 `jmp .` 作为诊断死循环，但 CPU 没有停在原地，而是继续执行了后续代码。

### 根因
GAS 中，`jmp .` 的 `.` 表示当前位置，但短跳转的偏移是相对于**下一条指令**计算的。所以 `jmp .` 实际上跳转到了下一条指令，而不是原地。

### 规避方法
使用标签方式实现原地死循环：
```asm
1:
    jmp 1b        # 正确的原地死循环
```

### 教训
- 汇编器的 `.`（当前位置）在跳转指令中的含义可能与直觉不符。
- 诊断用死循环必须用标签方式，确保跳转到自身。

---

## TOOL-02：objdump 16位反汇编误读 32 位指令

### 现象
用 `objdump -m i8086` 反汇编 stage2 时，32 位远跳转 `ljmp $0x0018, $label` 被误读为 5 字节 16 位 ljmp + 2 字节垃圾指令，段选择器显示为 0x0000 而非 0x0018。

### 根因
在保护模式 D=1 的 32 位段中，`ljmp` 是 7 字节（32位偏移 + 16位段选择器）。但 `objdump -m i8086` 按 16 位解码，把 7 字节指令拆成了 5 字节 ljmp + 2 字节数据。

### 规避方法
- 反汇编保护模式代码时用 `objdump -m i386`（32位）
- 或直接查看原始字节：`ea <offset_4bytes> <seg_2bytes>` = 7 字节 32 位远跳转

### 教训
- stage2 混合了 16 位和 32 位代码，单一反汇编模式无法正确解码所有指令。
- 关键指令（如远跳转、CR0 操作）应直接核对原始字节编码。

---

## ENV-01：stage2 2048B 硬限制

### 现象
添加修复代码后，构建脚本报错 `AssertionError: stage2超2048字节`。

### 根因
`tools/make_iso.py` 中 assert stage2 ≤ 2048B（脚本注释写 4096 是错的）。stage2 被加载到 0x8000，后续 0x8800 开始是其他数据/栈，不能超过 2048B。

### 规避方法
- 添加代码前评估字节数，必要时精简其他地方
- 常用精简手段：去掉冗余的寄存器设置、合并指令、用更短的编码
- 当前 V4.96c 完整版 2043B，仅剩 5B 余量

### 教训
- 引导器空间极其紧张，每个字节都要计算。
- 修改后必须确认 stage2 大小，不能假设"应该够"。

---

## ENV-02：QEMU 环境周期性重置

### 现象
开发过程中，`qemu-system-i386` 命令不定期丢失，apt 源配置被重置，需要反复重装。

### 根因
沙箱环境周期性清理，apt 源配置和 qemu 包会丢失。/tmp 目录也会被自动清理。

### Workaround
- apt 源丢失时用清华源重装：
  ```bash
  sudo tee /etc/apt/sources.list <<'EOF'
  deb https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ jammy main restricted universe multiverse
  deb https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ jammy-updates main restricted universe multiverse
  deb https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ jammy-backports main restricted universe multiverse
  deb https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ jammy-security main restricted universe multiverse
  EOF
  sudo apt-get update && sudo apt-get install -y qemu-system-x86
  ```
- 镜像副本放在项目目录 `build/` 下，不要放 /tmp
- QEMU 启动必须 `run_in_background: true`

---

## BUG-07：清 PE=0 后 CS.base 被 QEMU TCG 重置（待解决，当前最关键阻塞项）

### 现象
搬移 SEG1 完成（VGA 显示 A2 标记）后，回实模式读 SEG2 阶段 CPU 三重故障复位。VGA 被部分清屏，CPU 在 BIOS 中（CS=0xF000），0x190000 全 0（SEG2 未加载）。

### 根因
回实模式流程：保护模式 ljmp 到 code16 段（选择器 0x0018, base=0, D=0）→ 在 code16 段中清 CR0.PE=0 → ljmp 到实模式段 CS=0x0800。

**清 PE=0 后，QEMU TCG 将 CS 隐藏属性的 base 重置为 CS<<4（实模式地址计算方式），而不是保持保护模式 code16 段的 base=0。**

- CS=0x0018 → CS.base 被重置为 0x0018<<4 = 0x180
- EIP 保持清 PE 前的值（如 0x8684，下一条指令地址）
- 取指物理地址 = CS.base + EIP = 0x180 + 0x8684 = **0x8804**（错误！）
- 正确的物理地址应为 0x8684（code16 段 base=0 时）

0x8804 在 stage2（0x8000-0x87FA）之外，是未初始化内存。CPU 执行垃圾代码 → 触发异常 → IDTR 仍指向保护模式 IDT（实模式下无效）→ 双重故障 → 三重故障 → CPU 复位。

### 诊断过程
1. 在清 PE=0 后放置 `hlt` 指令：CPU 未停机（HLT=0），证明 hlt 未被执行 → 取指地址错误
2. 在清 PE=0 后放置原地死循环 `1: jmp 1b`：CPU 未停在死循环，同样证明取指地址错误
3. CR0=0x10（PE=0）确认清 PE 指令本身执行成功
4. V4.97 尝试在 code32 段（CS=0x0008）直接清 PE：同样有 CS.base 重置问题（0x0008<<4=0x80），且清 PE 后 D=1 导致后续 ljmp 加载的 CS.D 可能不刷新，系统崩溃更早

### 待选解决方案

**方案 A：IRET 进入实模式（推荐）**
- 在保护模式下，在栈上构造 IRET 帧（EIP/CS/EFLAGS）
- 清 PE=0
- 执行 32 位 IRET
- IRET 由处理器强制刷新 CS 隐藏属性（D=0, base=CS<<4, limit=64K），不依赖清 PE 后的取指
- 注意：IRET 指令本身的取指也受 CS.base 重置影响，需确保清 PE 与 IRET 在同一 cache line 或使用其他技巧

**方案 B：扩大 GDT，创建 base=CS<<4 的代码段**
- 将 GDT 扩大到 256 个描述符（2048 字节）
- 在 GDT[0x100] 创建 base=0x8000, D=0, limit=64K 的代码段
- 选择器 = 0x0800（索引 0x100），满足 选择器<<4 = 0x8000 = base
- 回实模式前 ljmp 到选择器 0x0800（CS.base=0x8000）
- 清 PE=0 后 CS.base 被重置为 0x0800<<4=0x8000，与保护模式 base 一致 → 取指地址正确
- 缺点：GDT 需 2048 字节，需找空闲内存区域

**方案 C：将清 PE 代码移到低地址**
- 将清 PE=0 和后续指令复制到物理地址较低的位置（如 0x1000-0x2000）
- 确保 CS<<4 + EIP = 物理地址
- 缺点：需要额外的复制代码，且低地址可能被 BIOS 数据占用

### 当前状态
- V4.96c 完整版（code16 段回实模式）能执行到 A2（SEG1 搬移完成），回实模式失败
- V4.97（code32 段直接清 PE）崩溃更早，已回退
- **V4.98（方案 B：扩大 GDT + 索引256 rm16_cs 描述符）实施中遇到新问题，已回退**：
  - GDT 从 4 个描述符（32B）扩大到 257 个（2056B），GDTR limit=2055
  - 索引256 描述符：选择器 0x0800, base=0x8000, D=0, limit=64K
  - 回实模式前 ljmp 到 0x0800，清 PE 后 CS.base 重置为 0x8000（与保护模式 base 一致）
  - **问题1：GDT_ADDR=0x9600 时，GDT 扩大到 0x9600-0x9E07 覆盖了 MBINFO_ADDR=0x9700（Multiboot info 结构），导致内核加载异常**
  - **问题2：GDT_ADDR 移到 0x9E00/0x9800 后，系统在 read_kernel 阶段就崩溃（VGA 只有 H2GAR，比回实模式早），进保护模式失败。原因未完全确认——可能是 GDT 扩大后 GDTR limit=2055 导致 CPU 加载 GDT 时读取了未初始化内存，或 GDT 写入代码中直接地址寻址默认用 DS 而非 ES（需加 `%es:` 前缀）**
  - **问题3：索引256 描述符写入用 `movl $imm, GDT_ADDR+2048` 直接地址寻址时，默认段寄存器是 DS 而非 ES。如果 DS≠0，物理地址错误。必须显式加 `%es:` 前缀：`movl $imm, %es:GDT_ADDR+2048`**
  - V4.98 已回退到 V4.96c 稳定版（GDT 4 描述符 @0x9600，回实模式用 0x0018）
- 代码中已注释此问题和待选方案，见 `tools/bootloader.S` 回实模式部分

### V4.98 排障新教训
- **GDT 扩大必须检查内存布局冲突。** 0x9600 附近有 MMAP_BUFFER(0x9400)、MBINFO_ADDR(0x9700) 等关键数据，GDT 扩大会覆盖它们。扩大前必须画完整内存布局图。
- **16 位汇编中直接地址寻址默认用 DS。** `movl $imm, addr` 的默认段是 DS，不是 ES。如果 ES=0 但 DS≠0，必须显式加 `%es:` 前缀。这是从 CHS 版本就存在的潜在坑。
- **GDTR limit 改大后，CPU 认为 GDT 更大。** 虽然只用到前几个描述符，但 lgdt 后 CPU 可能在某些操作中检查 GDT 边界。未初始化的 GDT 内存（全 0 或垃圾）可能导致问题。
- **QEMU 环境周期性重置会导致验证结果不可复现。** 重装 QEMU 后，之前验证通过的版本可能表现不同。每次验证前应确认 QEMU 版本和环境一致性。

### 教训
- **x86 保护模式↔实模式切换是操作系统开发中最容易踩坑的部分之一。** 段寄存器隐藏属性（base/limit/D/B）在模式切换时的行为因 CPU 实现而异，不能假设所有 CPU 都遵循 Intel 手册的"理想"行为。
- **QEMU TCG 与真机可能有差异。** QEMU TCG 在清 PE 后重置 CS.base 的行为可能与真机不同，但在 QEMU 中能跑通是基本要求。
- **诊断用 hlt/死循环必须放在正确的位置，且要确认 CPU 确实停在那里。** 如果 CPU 没有停在预期位置，说明取指地址或指令解码有问题。
- **objdump -m i8086 反汇编 32 位代码时会误读指令长度。** 关键指令（远跳转、CR0 操作）应直接核对原始字节编码。

---

## 关键常量参考

| 常量 | 值 | 说明 |
|------|-----|------|
| KERNEL_SECTORS | 2246 (0x8C6) | 内核总扇区数 |
| KERNEL_LBA | 5 | 内核在磁盘上的起始 LBA |
| KERNEL_LOAD_ADDR | 0x100000 | 内核加载地址 |
| KERNEL_ENTRY | 0x101000 | 内核入口点 |
| SEG1_SEC | 1152 | 第一段扇区数（576KB） |
| SEG2_SEC | 1094 | 第二段扇区数（547KB） |
| SEG2_LBA | 1157 | 第二段起始 LBA |
| SEG2_DEST | 0x190000 | 第二段目标地址 |
| stage2 基址 | 0x8000 | stage2 加载地址 |
| stage2 上限 | 2048B | stage2 最大大小 |
| dap_packet | 0x87ee | DAP 结构地址（.bss 段） |
| GDT_ADDR | 0x9600 | GDT 地址 |
| GDT 描述符 | 4 个 | NULL(0x00), code32(0x08), data32(0x10), code16(0x18) |

---

## 排障方法论总结

1. **先验证调试工具的正确性**：调试函数本身有 bug 会误导方向（BUG-02/03）。
2. **不要假设 BIOS 保留寄存器**：INT 调用后只有 AX 和指定输出寄存器有定义（BUG-01）。
3. **实模式地址拆分要 careful**：偏移只占低 4 位（BUG-06）。
4. **保护模式↔实模式切换关注隐藏属性**：D 位、base、limit 是关键（BUG-05）。
5. **诊断死循环用标签不用 `jmp .`**（TOOL-01）。
6. **混合 16/32 位代码反汇编要核对原始字节**（TOOL-02）。
7. **每个修复后立即构建验证 stage2 大小**（ENV-01）。
8. **魔数校验≠整块数据正确**：必须验证数据连续性（BUG-06）。

---

## BUG-10：BIOS INT13切换栈致IRET返回地址错误→递归中断（核心未解决）

### 现象
SEG1（LBA 5-1156）读盘正常，内核魔数 `02 b0 ad 1b` 正确写入 0x100000。回实模式后读 SEG2（LBA 1157-2250）时，INT13 仅执行 2 次即崩溃，0x190000 处全零。QEMU 调试日志显示第一次 INT13 实际执行成功（AH=0），但 IRET 后 EIP=0x8230（read_slice 入口），CS=0x0800——CPU 递归到了 read_slice 入口。

### 根因链
```
BIOS INT13处理程序内部切换栈(SS:SP被修改)且出口不切回原栈
  → IRET指令从BIOS的栈(而非我们的栈)弹出返回地址
  → 弹出的是垃圾值 0x0800:0x0230 (= read_slice入口物理地址)
  → CPU递归到read_slice入口 → 执行pushal(进一步破坏BIOS栈)
  → DAP设置使用被破坏的EDI → dap_segment=0x0708(而非0x1000)
  → 缓冲区物理地址=0x7080(栈区域附近) → 恶性循环
```

关键证据：
- 第一次 INT13 后 ESP=0x6FFC（不是预期的栈值），证明 BIOS 切换了栈
- IVT 0x4C 未被直接破坏（DAP 缓冲区地址不是 IVT）
- 栈位置从 0x7000 移到 0x7C00 后 SEG1 恢复正常，但 SEG2 仍递归
- 回实模式后 FS/GS 未清零（保护模式值 0x10 残留），已在 V6.42 修复但未解决递归

### 已尝试的方案（均未完全成功）
1. **独立栈 0x6000/0x9000**：BIOS 仍切换栈，失败
2. **栈位置 0x7C00**：SEG1 正常，SEG2 仍递归
3. **INT13 包装器（修改 IVT 0x4C）**：IVT 写入绝对地址而非偏移导致早期崩溃；修复后仍失败，方案放弃
4. **递归检测（变量在 .bss/0x9000/0x1000/0x4000）**：
   - .bss 变量被 BIOS 磁盘缓冲覆盖（BUG-11）
   - 变量移到 0x4000 后入口检测生效，但循环入口检测干扰 SEG1（BUG-15）
   - 递归检测需同时恢复 SS 和 SP（仅恢复 SP 不够，BUG-10 子项）
5. **ATA PIO 直接读盘**：发送读命令后 BSY 一直为 1（BUG-13），需进一步调试 IDE 控制器初始化
6. **回实模式后 FS/GS 清零**：V6.42 已添加，未解决递归

### 为什么 SEG1 不递归而 SEG2 递归
尚未完全确定。可能因素：
- SEG1 在进入保护模式前调用，SEG2 在回实模式后调用
- 回实模式过程中某些 CPU 隐藏状态（段寄存器 base/limit/D 位）可能未完全恢复
- SEG2 的 LBA（1157+）可能触发 SeaBIOS 不同的代码路径

### 下一步建议
1. **深入调试 SEG2 第一次 INT13 前后的完整 CPU 状态**（CS/SS/DS/ES/FS/GS 及隐藏属性、EFLAGS、CR0），对比 SEG1 第一次 INT13 的状态差异
2. **在 SEG2 读盘前完全重置 CPU 状态**：所有段寄存器=0，EFLAGS=已知值，IDTR 指向 BIOS IVT
3. **尝试 ATA PIO 方案的深入调试**：在发送命令前读取状态寄存器清除中断，检查 QEMU IDE 控制器是否需要软件复位或 PIO 模式设置（SET FEATURES 0xEF 子命令 0x03）
4. **尝试在 QEMU 中使用 `-device ide-hd` 替代 `-drive if=ide`**，或使用 virtio-blk
5. **递归检测方案完善**：将检测变量放到绝对不会被覆盖的地址（如 0x80000 用 FS 段访问），同时在入口和循环入口都检测但避免干扰 SEG1（可通过判断当前是否在 SEG2 上下文中决定是否启用检测）

### 教训
- **BIOS 实模式中断处理程序可能切换栈**，不能假设 INT 指令压入的返回地址会被 IRET 正确弹出
- **递归检测方案必须保存/恢复完整的 CPU 状态**（包括 SS），仅恢复 SP 不够
- **自由内存初始值不确定**，用作标志变量前必须显式清零
- **回实模式后必须重置所有段寄存器**（包括 FS/GS），保护模式的残留值会影响 BIOS 行为
- **回实模式后必须显式重置 IDTR 和磁盘控制器**（V6.45 最终修复）：保护模式切换可能扰乱 BIOS 内部状态，必须用 `lidt` 重新加载 BIOS IVT（base=0, limit=0x3FF），并用 INT13 AH=00 重置磁盘系统

### 最终修复（V6.45）
在 `rm_read_seg2`（回实模式后读 SEG2 的入口）中，FS/GS 清零之后、读盘之前，添加：
1. **显式加载 IDTR**：`movw $0x03FF, 0x9910; movl $0x00000000, 0x9912; lidt 0x9910` — 确保实模式下使用 BIOS IVT（base=0, limit=1023），而非保护模式切换后可能残留的 IDTR 状态
2. **INT13 AH=00 磁盘重置**：`movb $0x00, %ah; movb boot_drive, %dl; orb $0x80, %dl; int $0x13` — 让 BIOS 重新初始化磁盘控制器内部状态

修复后系统成功启动到内核，VGA 显示 `[HEPU]`、`[SYSCALL]`、`[PAGING]`、`[SHELL]`、`[MACHO]` 等内核输出，内核运行用户态进程（winargs.exe、macho_demo）。

**根因总结**：保护模式→实模式切换后，BIOS 的中断描述符表缓存或磁盘控制器内部状态被扰乱，导致 INT13 AH=42 处理程序行为异常（切换栈且不切回），IRET 从错误栈弹出垃圾返回地址→递归中断。显式重置 IDTR 和磁盘控制器后，BIOS 恢复正常行为。

### 巩固版本（V6.46）
在 V6.45 修复基础上进行代码巩固：
1. **移除递归检测 workaround**：根因已由 IDTR+磁盘重置解决，移除 read_slice 入口的递归检测、INT13 前的标志保存、0x4000 变量初始化，简化代码
2. **修复 INT13 错误检测**：将错误的 `testw $1, %ax; jnz`（测试 AL 位0）改为正确的 `jc`（测试 CF 进位标志）
3. **添加读盘重试机制**：每扇区 INT13 失败时自动重试最多 3 次（`read_retry` 变量），提高可靠性
4. **清理 VGA 诊断标记**：移除 P0/M1/R1/P1/P2/RS 等开发期诊断标记（保留 read_fail 的错误信息显示）
5. **清理 .bss 段**：移除已废弃的 saved_ret_ip/saved_sp/orig_int13_*/wrapper_saved_sp/int13_ret_ip/int13_saved_sp/in_int13 等变量

**结果**：stage2 从 1931 字节缩减到 1784 字节（节省 147 字节），连续 3 次 QEMU 启动验证全部成功（SEG1=OK, SEG2=OK, Kernel=OK）。

---

## BUG-16: PIT时钟中断处理程序未注册，系统卡在Shell自检pit_sleep

### 发现时间
2026-09-21

### 症状
- 系统启动到Shell后，串口输出`[SHELL] main started`，然后系统无响应
- EIP停留在`pit_sleep+0x3e`的HLT指令（地址0x00110c1e）
- `pit_ticks`变量恒为0，定时器中断从未触发
- EFLAGS IF=1（中断已开启），PIC IRQ0掩码=0xfe（IRQ0已开启），但中断就是不触发
- 排查过程中曾误判为"中断被禁用"，实际是EFLAGS位9计算错误（0x206的位9=1，IF是开启的）

### 根因
`pit_init()`（kernel/arch/x86/pit.c）中只调用了`pic_unmask_irq(IRQ_TIMER)`开启PIC的IRQ0，但**没有调用`irq_register_handler(IRQ_TIMER, pit_handler)`注册中断处理程序**。

对比其他设备：
- `keyboard_init()`: `irq_register_handler(IRQ_KEYBOARD, keyboard_handler)` ✅
- `mouse_init()`: `irq_register_handler(IRQ_MOUSE, mouse_handler)` ✅
- `rtc_init()`: `irq_register_handler(8, rtc_irq_handler)` ✅
- `pit_init()`: 只有`pic_unmask_irq`，**缺少注册** ❌

结果：PIT硬件正常产生中断，PIC也正常传递到CPU，但`irq_handler()`发现`irq_handlers[0] == NULL`，什么也不做，`pit_ticks`永远为0，`pit_sleep()`的`while (pit_ticks < target)`永远为真，系统永久HLT。

### 排查过程
1. 检查VGA文本模式驱动 → 正常（显存写入正确，QEMU screendump文本模式渲染异常是QEMU本身问题）
2. 检查分页映射 → 正常（256MB全映射）
3. 检查EFLAGS → 误判IF=0（实际计算错误，IF=1）
4. 检查PIC掩码 → 正常（IRQ0已开启）
5. 检查IDT向量32 → 正常（指向irq0）
6. 检查`pit_ticks`值 → 恒为0
7. 检查`pit_handler`注册 → **发现从未注册**

### 修复
在`pit_init()`中（pic_unmask_irq之后）添加：
```c
extern void irq_register_handler(uint8_t, void (*)(void));
irq_register_handler(IRQ_TIMER, pit_handler);
```
同时在文件头部添加`#include "irq.h"`。

### 验证
修复后：
- 系统成功通过Shell自检的`pit_sleep(3000)`（3秒等待）
- 成功运行HEPU_BOOT_DEBUG自动测试：linuxhello、linuxc、linuxbrk、linuxcrt、winhello.exe、winargs.exe、macho_demo
- 最终进入Shell主循环，EIP停留在`keyboard_getchar`的HLT等待键盘输入
- `pit_ticks`正常递增（100Hz）

### 教训
- **开启PIC IRQ不等于注册中断处理程序**：两者是独立的步骤，缺一不可
- **EFLAGS位计算要仔细**：0x206 = 0x200(IF) + 0x06，IF=1不是0
- **nm符号地址要核对**：pit_ticks地址是0x03f4e154，不是0x03f4d154（差0x1000）
- **同类设备要对比检查**：键盘/鼠标/RTC都注册了handler，唯独PIT没有，对比法能快速定位

## BUG-17: 根文件系统挂载后/dev节点丢失导致ELF应用stdout未绑定

**发现日期**: 2026-09-21
**严重程度**: 高
**影响范围**: 所有Linux ELF兼容应用（含Rust用户态应用）
**症状**: ELF应用调用write(1, ...)返回-1，无任何输出
**根因**: 
- devfs_init()在INIT_PHASE_CORE阶段创建/dev/console节点
- fs2_init()在INIT_PHASE_FS阶段调用fs2_mount("/dev/root", "/", FS_TYPE_EXT2, 0)
- vfs_mount()直接替换mount_point->children，未保存原节点
- 导致根目录下原有的/dev目录及/dev/console节点丢失
- binfmt_elf加载ELF应用时vfs_find("/dev/console")返回NULL
- vfs_bind_stdio未被调用，fd_table[0/1/2].used=0
- sys_write→vfs_write检查fd未打开，返回-1
**修复方案**: 
- 在binfmt_elf.c中添加fallback：vfs_find("/dev/console")失败时自动重新创建/dev目录和/dev/console节点
- console_ops从devfs.c导出为全局符号（去掉static）
- 重新创建后调用vfs_bind_stdio绑定fd 0/1/2
**修复文件**: kernel/compat/binfmt_elf.c, kernel/fs/devfs.c
**经验教训**: 
- vfs_mount替换children时应保存原节点或合并，而非直接覆盖
- 设备文件系统应在根文件系统挂载后重新初始化，或使用独立的mount命名空间
- 诊断方法：在sys_write和vfs_write中添加串口诊断，确认返回值和失败原因

## BUG-18: 诊断代码push/pop不平衡导致栈损坏longjmp返回地址被覆盖

**发现日期**: 2026-09-21
**严重程度**: 高
**影响范围**: elf_mainline_save()函数（所有ELF应用退出路径）
**症状**: longjmp返回到elf_mainline_save后，函数ret跳到错误地址，无法返回到elf_enter_user
**根因**:
- 在elf_mainline_save()中添加大量serial_puts/serial_puthex诊断输出
- 编译器生成的PIC代码中，部分serial_puthex调用后只pop一个参数（应为两个）
- 导致栈指针偏移48字节，返回地址被串口端口号0x3F8覆盖
- ret前诊断显示esp=0x06B30A80 ret_addr=0x000003F8（明显错误）
- hepu_longjmp恢复上下文正确，但elf_mainline_save的栈帧已被诊断代码破坏
**修复方案**: 移除elf_mainline_save和elf_enter_user中的所有诊断输出，恢复简洁版本
**修复文件**: kernel/compat/binfmt_elf.c
**经验教训**:
- 在noinline函数中添加大量函数调用诊断时，必须检查栈平衡
- PIC代码（使用ebx作为GOT指针）对栈布局更敏感
- 诊断代码应尽量使用宏或独立函数，避免在关键路径（如setjmp/longjmp）中内联大量调用
- ret前打印栈指针和返回地址是定位栈损坏的有效方法

## BUG-19: 新系统调用使用65/66号导致所有ELF应用崩溃

**发现日期**: 2026-09-21
**严重程度**: 高
**影响范围**: 所有Linux ELF兼容应用（含Rust用户态应用）
**症状**: 添加SYS_GFX_INIT=65和SYS_GFX_TEXT=66后，所有ELF应用运行时触发General Protection Fault
**根因**: 
- 原SYS_MAX=65，系统调用表大小为65（索引0-64）
- 添加SYS_GFX_INIT=65、SYS_GFX_TEXT=66后，SYS_MAX=67，系统调用表扩展为67
- 系统调用表扩展后，某些内部数据结构或边界检查与新大小不兼容
- 具体表现为：ELF应用的系统调用路径被破坏，触发GPF
- 回滚到SYS_MAX=65后应用恢复正常
**修复方案**: 
- 新系统调用使用较高的号（200+），避免扩展低端系统调用表
- SYS_GFX_INIT=200，SYS_GFX_TEXT=201，SYS_MAX=202
- 系统调用表从65扩展到202，但新增区域（65-199）全为NULL，不影响现有调用
**修复文件**: kernel/include/syscall.h, kernel/include/constants.h, kernel/arch/x86/syscall.c, user/apps/gfxtest/gfxtest.c
**经验教训**: 
- 新系统调用应使用较高的号（200+），避免与现有号冲突
- 扩展低端系统调用表（SYS_MAX从65到67）可能引入未预期的兼容性问题
- 添加新系统调用后必须测试所有现有ELF应用，确保无回归
- 系统调用号命名空间应预留扩展空间：0-199为稳定ABI，200+为新增/实验性

## BUG-20: Rust cat工具和gfxtest运行时GPF（待调试）

**发现日期**: 2026-09-21
**严重程度**: 中
**影响范围**: 新Rust用户态应用（cat）和C图形测试应用（gfxtest）
**症状**: 
- hello-rust（brk测试）正常运行
- cat工具和gfxtest运行时触发General Protection Fault，无任何输出
**已排除原因**:
- 不是argv读取导致（已回滚argv功能，问题依旧）
- 不是系统调用号65/66冲突（已改用200/201号，问题依旧）
**可能原因（待排查）**:
1. open系统调用在Linux兼容进程中有问题
2. hepu-sys库修改后影响了某些应用
3. cat/gfxtest二进制文件入口点或栈布局问题
4. 内存分配器修改影响了应用启动
**当前状态**: 待进一步调试
**临时解决方案**: 简化cat为最小测试版本（只print一行），待定位根因

### BUG-20 调试进展（2026-09-21 第二轮）

**新发现**：
1. 问题不是Rust特有：C版本cat（cat_c.elf）也崩溃
2. 汇编版本（linuxhello.elf）正常工作
3. 区别：C编译的应用有多个LOAD段（3-4个），汇编应用只有2个
4. 链接脚本修改（RWE单段）没有解决问题
5. -fno-pic编译选项也没有解决问题

**已排除原因**：
- 不是argv读取导致
- 不是系统调用号65/66冲突
- 不是链接脚本LOAD段太小
- 不是PIC/GOT问题

**待排查方向**：
1. ELF加载器对多LOAD段的处理是否有bug
2. initrd中C应用二进制文件是否完整
3. GCC生成的C运行时初始化代码是否访问了未映射内存
4. 段对齐和页映射是否正确

**当前状态**：QEMU环境不稳定，调试效率低，待后续深入排查

### BUG-20 调试进展（2026-09-21 第三轮）

**新发现**：
1. cat_c3（-fno-pic编译，3个LOAD段）也崩溃
2. _start和main反汇编看起来正确，没有访问GOT或.bss
3. ELF加载器elf_build_segs代码逻辑正确
4. 所有C编译应用都崩溃，汇编应用正常

**核心疑问**：
- 为什么C编译的应用（3-4个LOAD段）都崩溃？
- 为什么汇编应用（2个LOAD段）正常？
- 区别在哪里？

**待深入排查**：
1. 对比linuxhello.elf和cat_c3.elf的ELF头详细差异
2. 检查initrd中C应用二进制文件是否完整
3. 在ELF加载器中添加更多调试输出，跟踪每个段的映射和复制
4. 尝试用不同的编译选项（-march=i386、-Os等）编译C应用

## BUG-20 调试进展（V1.02阶段）

### 问题描述
cat/gfxtest等ELF应用运行时GPF（General Protection Fault）。

### 已排除方向
1. argv读取（已回滚）
2. 系统调用号65/66冲突（已改用200/201）
3. 链接脚本LOAD段大小（已改为RWE单段）
4. PIC/GOT导致（-fno-pic编译）

### 关键发现（2026-09-22）

#### ELF段结构对比
| 应用 | LOAD段数 | 段虚拟地址 | 数据段(.data) | 状态 |
|------|----------|-----------|--------------|------|
| linuxhello.elf | 2 | 0x08048000, 0x08049000 | 无 | ✅ 正常 |
| cat_c3.elf | 3 | 0x08048000, 0x08049000, 0x0804a000 | 无(只读) | ❓ 待确认 |
| linuxcrt.elf | 4 | 0x08048000, 0x08049000, 0x0804a000, 0x0804c000 | 有(RW) | ❓ 待确认 |
| linuxc.elf | 4 | 同上 | 有(RW) | ❓ 待确认 |
| linuxbrk.elf | 4 | 同上 | 有(RW) | ❓ 待确认 |

#### 第4个段（数据段）特征
- 虚拟地址：0x0804c000
- 文件偏移：0x003000
- 文件大小：0x0000c（12字节）
- 内存大小：0x00018（24字节，含bss）
- 标志：RW（可读可写）

#### QEMU日志分析
- ELF加载器成功映射4个段（0x08048000, 0x08049000, 0x0804a000, 0x0804c000）
- 多个ELF进程成功创建并进入用户态
- 崩溃的是winhello.exe（PE应用），不是ELF应用
- macho_demo正常运行，输出"hello from HEPU!"

### 待确认事项
1. cat_c3.elf是否真的崩溃？（需要正确输入命令并查看日志）
2. 有4个段的ELF应用（linuxcrt.elf等）是否崩溃？
3. 如果ELF应用没有崩溃，BUG-20可能只影响特定的ELF应用，或者已经被修复

### 下一步调试计划
1. 启动QEMU，正确输入cat_c3命令，确认是否崩溃
2. 如果崩溃，查看具体的崩溃地址和寄存器状态
3. 对比正常应用（linuxhello.elf）和崩溃应用的段映射差异
4. 重点检查数据段（.data）的bss清零是否正确

### BUG-20 重大进展（2026-09-22 第二轮）

#### 关键验证结果

将cat_c3.elf和linuxhello.elf写入ext2数据盘/bin目录后，通过QEMU精确验证：

| PID | 应用 | 入口点 | LOAD段数 | 运行结果 |
|-----|------|--------|----------|----------|
| 3 | linuxhello.elf | 0x08049000 | 2 | ✅ 成功，输出"Hello from Linux ELF on HEPU!" |
| 4 | (系统自动) | 0x0804917F | 4 | ✅ 成功进入用户态 |
| 5 | (系统自动) | 0x08049135 | 4 | ✅ 成功进入用户态 |
| 6 | (系统自动) | 0x08049000 | 4 | ✅ 成功进入用户态 |

#### 重大发现

**崩溃的是PE应用（winhello.exe, winargs.exe），不是ELF应用！**

- `[UEXC] General Protection Fault`
- `[CRASHLOG] recorded: winhello.exe`
- `[CRASHLOG] recorded: winargs.exe`

#### 结论

BUG-20（"cat/gfxtest运行时GPF"）可能是之前调试时混淆了PE应用崩溃和ELF应用崩溃：
1. ELF加载器对多LOAD段（2段/3段/4段）的处理是正确的
2. 有数据段（.data，RW标志）的ELF应用也能正常运行
3. 崩溃的是PE子系统的应用（winhello.exe, winargs.exe），与ELF加载器无关
4. macho_demo（Mach-O格式）也正常运行

#### 待确认事项
1. cat_c3.elf是否真的能正常运行（需要更可靠的按键输入确认）
2. gfxtest图形模式是否正常（需要单独验证）
3. PE应用崩溃的原因（可能是PE子系统的bug，不是ELF加载器的问题）

#### 状态更新
BUG-20从"ELF加载器多段处理问题"重新分类为"PE应用崩溃问题"，ELF加载器部分基本确认正常。

### BUG-20 PE应用崩溃分析（2026-09-22 第三轮）

#### 崩溃信息
```
[SC] other a1=0x00000000 a2=0x00401200 a3=0x0000000D ueip=0x00403034
[UEXC] General Protection Fault
[CRASHLOG] recorded: winhello.exe
[CRASHLOG] recorded: winargs.exe
```

#### 关键发现

**崩溃地址 0x00403034 正好在 IAT（导入地址表）区域内！**

PE应用的IAT区域：
- 0x00403000: WCA (WriteConsoleA?)
- 0x00403020: RdF (ReadFile)
- 0x00403040: Exit (ExitProcess)
- 0x00403050: GStdH (GetStdHandle)
- 0x00403080: GCmdL (GetCommandLineA)
- 0x004030A0: GTick (GetTickCount)
- 0x004030C0: Sleep
- 0x004030E0: GModF (GetModuleFileNameA)

崩溃地址 0x00403034 在 WCA(0x00403000) 和 Exit(0x00403040) 之间。

#### 根本原因分析

IAT（导入地址表）区域应该存储的是 **stub函数地址（数据）**，不是可执行代码。

PE加载器的工作流程：
1. 解析PE文件的导入表
2. 将每个导入API的IAT条目填充为对应的stub函数地址
3. PE应用通过 `call [IAT]` 间接调用stub函数
4. stub函数将Win32 API调用翻译为HEPU syscall

**问题**：CPU正在执行IAT区域内的代码（ueip=0x00403034），而不是通过间接调用跳转到stub函数。

#### 可能的原因

1. **IAT条目没有正确填充**：存储的是0或其他无效地址，导致PE应用跳转到了错误的地址
2. **PE应用使用了错误的调用方式**：直接跳转到了IAT区域，而不是通过间接调用
3. **stub函数有问题**：stub函数执行了错误的指令，导致返回到了IAT区域
4. **节映射问题**：IAT所在的节可能没有正确映射，或者属性设置错误

#### 下一步调试计划

1. 检查IAT条目是否正确填充（查看PE加载器的iat验证输出）
2. 分析PE应用的反汇编代码，确认调用方式
3. 检查节映射是否正确（特别是IAT所在节的属性）
4. 单步调试PE应用，确认崩溃时的指令

#### 状态
PE应用崩溃问题已定位到IAT区域执行问题，待进一步调试。

### BUG-20 PE崩溃精确定位（2026-09-22 第四轮）

#### 任务1：检查IAT条目是否正确填充 ✅

**结论**：IAT条目填充逻辑正确。

PE加载器（binfmt_pe.c第356-357行）：
```c
pe_u32_write(proc, opt.ImageBase + desc.FirstThunk + j * 4,
             stub_va + stub_off);
```

将stub函数地址写入IAT条目。PE应用通过`call [IAT]`间接调用stub函数。

#### 任务2：分析PE应用反汇编代码 ✅

**重大发现**：PE应用是用Python脚本生成的（gen_winhello.py, gen_winargs.py），可以直接查看机器码。

winhello.exe的调用方式：
```python
text_code += b"\xFF\x15" + struct.pack("<I", iat_wca)  # call [WriteConsoleA]
text_code += b"\xFF\x15" + struct.pack("<I", iat_ep)   # call [ExitProcess]
```

使用`FF 15`（call [mem]）间接调用，这是正确的。

#### 任务3：检查节映射是否正确 ✅

**结论**：节映射逻辑正确。

PE加载器（binfmt_pe.c第122-126行）：
```c
if (paging_map(proc->page_dir, vaddr, frame,
               PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0) {
```

所有节都被映射为PAGE_PRESENT | PAGE_WRITE | PAGE_USER，有读、写、执行权限。

#### 崩溃精确定位

**崩溃地址 0x00403034 在 stub_read 函数内！**

stub函数布局（STUB_BASE=0x00403000）：
| 偏移 | 函数 | 大小 |
|------|------|------|
| 0x000 | stub_write | 18字节 |
| 0x020 | stub_read | 33字节 |
| 0x040 | stub_exit | 9字节 |
| 0x050 | stub_stdhandle | 7字节 |
| ... | ... | ... |

stub_read函数字节码（从0x00403020开始）：
```
offset 0-4:   mov eax, 9 (SYS_READ)
offset 5-9:   mov ebx, 0 (fd=0)
offset 10-13: mov ecx, [esp+8] (buf)
offset 14-17: mov edx, [esp+12] (count)
offset 18-19: int 0x80 (syscall)
offset 20-23: mov ebx, [esp+16] (lpNumberOfBytesRead) ← 崩溃地址！
offset 24-25: test ebx, ebx
offset 26-27: jz +2
offset 28-29: mov [ebx], eax
offset 30-32: ret 20 (stdcall, 5 args)
```

**崩溃发生在 int 0x80 syscall 返回后，执行 `mov ebx, [esp+16]` 时。**

#### 崩溃类型

General Protection Fault（#GP），不是Page Fault（#PF）。

#GP通常由以下原因引起：
1. 段权限违规
2. 无效的段选择子
3. 执行了特权指令
4. 栈切换错误

#### 用户栈信息

- USER_STACK_ADDR = 0x40000000
- USER_STACK_SIZE = 65536 (64KB)
- 栈范围：0x40000000 - 0x40010000
- 栈顶：0x40010000
- PE应用进入用户态时 esp = 0x40010000

#### 待进一步调试

1. 查看syscall（int 0x80）的处理代码，确认返回时是否正确恢复用户态esp
2. 分析PE应用调用ReadFile时的栈帧
3. 单步调试PE应用，确认syscall返回后的esp值
4. 检查stub_read函数的字节码是否正确

#### 状态
PE崩溃已精确定位到stub_read函数内的`mov ebx, [esp+16]`指令，待进一步调试syscall返回机制。

### BUG-20 修复尝试与新发现（2026-09-22 第五轮）

#### 修复尝试：stub_read nop替换

将stub_read中int 0x80后的指令替换为nop：
- 原：mov ebx, [esp+16] / test ebx, ebx / jz +2 / mov [ebx], eax
- 新：nop nop nop nop nop nop nop nop nop nop（10个nop）

**结果：崩溃仍然发生！**

#### 新发现：[SC] other输出来源

`[SC] other`输出来自syscall_handler中的PE诊断代码（kernel/arch/x86/syscall.c:234-253）。

崩溃时的syscall信息：
- syscall号：SYS_READ (num=9, tag="other")
- arg1 (fd) = 0 (stdin)
- arg2 (buf) = 0x00401200
- arg3 (count) = 0x0D (13)
- ueip = 0x00403034 (int 0x80的下一条指令，即nop)

#### 关键分析

崩溃流程：
1. 用户态stub_read执行int 0x80（地址0x00403032）
2. 进入内核态，syscall_handler打印[SC] other
3. 调用sys_read(0, 0x00401200, 0x0D)
4. sys_read返回，iret返回到用户态（eip=0x00403034）
5. 用户态执行nop指令时发生#GP

**nop指令不会导致#GP**，说明：
- iret返回时用户态状态（esp/ss/cs/eflags）被破坏
- 或者sys_read执行过程中修改了用户态的关键内存
- 或者异常发生在iret过程中，eip被错误报告

#### winhello.exe的IAT问题

winhello.exe崩溃时IAT值是乱码：
- WCA=0x6C6C642E (字符串".dll"的ASCII码)
- GStdH=0x00000000
- 其他IAT值也是乱码

这说明winhello.exe的IAT没有被正确填充为stub地址。可能原因：
- PE加载器的IAT填充逻辑对winhello.exe不生效
- winhello.exe的导入表结构与winargs.exe不同
- IAT填充后被其他代码覆盖

winargs.exe的IAT值是正确的stub地址（WCA=0x00403000等）。

#### 待进一步调试

1. 分析sys_read(vfs_read)对stdin(fd=0)的处理
2. 检查iret返回时的用户态esp/ss值
3. 分析winhello.exe的IAT为什么没有被正确填充
4. 在syscall返回前添加用户态esp验证

#### 状态
stub_read nop替换修复未成功，崩溃根因可能在syscall返回机制或用户态状态破坏。

### BUG-20 重大纠正：崩溃位置分析错误（2026-09-22 第六轮）

#### 之前的错误分析

之前认为崩溃地址是0x00403034（stub_read函数内），这是**错误的**！

`[SC] other a1=... a2=... a3=... ueip=0x00403034`中的ueip是**syscall调用时的eip**（int 0x80指令的下一条指令地址），来自syscall_handler中的诊断代码（fr[11] = int 0x80返回地址），**不是崩溃时的eip**！

#### 真实崩溃位置（通过CRASH-FRAME串口输出验证）

**winhello.exe崩溃**：
- eip = 0x0040101E（.text节内，int3 fallback指令）
- cs = 0x0000001B（用户代码段）
- esp = 0x4000FFF0（用户栈）
- ss = 0x00000023（用户数据段）
- err = 0x0000001A
- eax = 0x00000005（SYS_EXIT调用号）
- ebx = 0x00000000（exit code=0）

**winargs.exe崩溃**：
- eip = 0x00402092（.idata节内，hint/name数据区域！）
- cs = 0x0000001B
- esp = 0x4000FFE4
- ss = 0x00000023
- err = 0x00000000
- eax = 0x0040121B
- ebx = 0xFFFFFF07
- ecx = 0x004011FF
- edx = 0x0000000D

#### 关键发现

1. **崩溃不在stub函数内**，而在PE应用自己的区域：
   - winhello.exe崩溃在.text节的int3指令（0x0040101E）
   - winargs.exe崩溃在.idata节的hint/name数据区域（0x00402092）

2. **winhello.exe的IAT没有被正确填充**（值是乱码，如WCA=0x6C6C642E），导致call [IAT]可能跳转到错误地址

3. **winargs.exe崩溃在数据区域**，说明CPU错误地跳转到了.idata节的hint/name表执行数据

4. **stub_read的nop替换修复是无效的**，因为崩溃根本不在stub_read函数内

#### 新的假设

崩溃可能的根本原因：
1. **IAT填充问题**：PE加载器没有正确将IAT条目填充为stub地址，导致call [IAT]跳转到错误地址
2. **stub返回地址问题**：stub函数的ret指令返回到错误地址
3. **栈帧破坏**：syscall或stub函数破坏了用户栈帧，导致ret返回到错误地址
4. **PE应用代码bug**：PE应用本身的代码有问题

#### 下一步

1. 回滚stub_read的nop修改（无效修复）
2. 重点调试IAT填充逻辑，确认winhello.exe的IAT为什么没有被正确填充
3. 在stub函数入口和出口添加调试输出，确认调用和返回地址
4. 分析winargs.exe为什么会跳转到.idata节的数据区域

#### 状态
之前的崩溃位置分析错误，已通过CRASH-FRAME串口输出纠正。真实崩溃位置在PE应用自己的区域，需要重新定位根因。

### BUG-20 IAT填充验证（2026-09-22 第七轮）

#### IAT填充验证结果

在PE加载器中添加[PE-IAT]串口输出后验证：

**winhello.exe的IAT填充**：
- WriteConsoleA va=0x00402034 wr=0x00403000 rd=0x00403000 ✓
- ExitProcess va=0x00402038 wr=0x00403040 rd=0x00403040 ✓

**winargs.exe的IAT填充**：
- WriteConsoleA va=0x00402048 wr=0x00403000 rd=0x00403000 ✓
- GetStdHandle va=0x0040204C wr=0x00403050 rd=0x00403050 ✓
- GetCommandLineA va=0x00402050 wr=0x00403080 rd=0x00403080 ✓
- GetTickCount va=0x00402054 wr=0x004030A0 rd=0x004030A0 ✓
- Sleep va=0x00402058 wr=0x004030C0 rd=0x004030C0 ✓
- ExitProcess va=0x0040205C wr=0x00403040 rd=0x00403040 ✓
- GetModuleFileNameA va=0x00402060 wr=0x004030E0 rd=0x004030E0 ✓
- ReadFile va=0x00402064 wr=0x00403020 rd=0x00403020 ✓

**结论：IAT填充完全正确！** wr=rd，所有IAT条目都正确填充为stub地址。崩溃原因不是IAT填充问题。

#### 崩溃帧重新分析

**winhello.exe崩溃**：
- eip=0x0040101E（.text节，int3指令）
- eax=0x00000005（SYS_EXIT调用号）
- ebx=0x00000000（exit code=0）
- ecx=0x00401020（WriteConsoleA的buf参数）
- edx=0x0000001D（WriteConsoleA的count参数）

**关键发现**：eax=5说明stub_exit的mov eax,5已执行。eip=0x0040101E是winhello.exe的int3，不是stub_exit的int3（0x00403043）。这说明SYS_EXIT的iret可能返回到了错误的地址。

**winargs.exe崩溃**：
- eip=0x00402092（.idata节，hint/name数据区域）
- eax=0x0040121B
- ebx=0xFFFFFF07
- ecx=0x004011FF
- edx=0x0000000D

#### 新的假设

崩溃可能的根本原因：
1. **SYS_EXIT的调度切换有问题**：sys_exit将当前进程转为"待回收"并置need_resched，中断返回路径调用schedule切换进程。如果schedule切换时栈帧处理有问题，可能导致iret返回到错误地址。
2. **SYS_EXIT的longjmp有问题**：无其他活进程时，sys_exit释放自己的资源并longjmp回主线Shell。如果longjmp的栈帧有问题，可能导致跳转到错误地址。
3. **stub函数的ret指令返回到错误地址**：某个stub函数的ret指令清理栈的字节数不正确，导致返回到错误地址。

#### 下一步

1. 继续查看sys_exit的longjmp部分代码
2. 分析调度切换（schedule）时的栈帧处理
3. 在stub函数入口和出口添加调试输出，确认调用和返回地址
4. 验证stub函数的ret操作数是否正确

#### 已修改的文件
- kernel/arch/x86/idt.c：添加CRASH-FRAME串口输出
- kernel/compat/binfmt_pe.c：添加[PE-IAT]串口输出
- docs/BOOTLOADER_BUG_HISTORY.md：更新BUG-20分析

### BUG-20 winhello.exe崩溃修复（2026-09-22 第八轮）

#### 根本原因

通过在sys_exit中添加[SYS-EXIT]串口输出，发现所有进程has_live=0，都走"无其他活进程"分支（longjmp回主线）。

在sys_exit的"无其他活进程"分支中：
1. 回收其他进程的资源
2. **current = NULL**（重置调度器状态）
3. TSS.ESP0 = 内核初始栈顶
4. 释放自己的资源（页目录、用户栈、内核栈）
5. hepu_longjmp(mainline_jmp_buf, 1) → 回到PE加载器的pe_mainline_save

**问题**：longjmp回到PE加载器后，current仍然是NULL。后续代码（vga_printf、kmalloc等）在current=NULL的情况下执行，可能导致NULL解引用或其他错误，最终引发用户态崩溃。

#### 修复方案

在pe_enter_user的longjmp返回路径中，重新设置current为Shell进程（PID=1）：

```c
if (pe_mainline_save() != 0) {
    /* 恢复 current 为 Shell 进程(PID=1), 保证主线 Shell 环境完整 */
    extern process_t *current;
    extern process_t proc_table[];
    if (!current) {
        for (int i = 0; i < 64; i++) {
            if (proc_table[i].pid == 1 && proc_table[i].state != 0) {
                current = &proc_table[i];
                break;
            }
        }
    }
    return BINFMT_OK;
}
```

#### 验证结果

修复后：
- **winhello.exe不再崩溃** ✓（之前崩溃在eip=0x0040101E）
- 系统能继续运行到Shell和mobile界面 ✓
- macho_demo正常运行 ✓
- ELF应用正常运行 ✓

#### 剩余问题

**winargs.exe仍然崩溃**：
- 崩溃位置：eip=0x00402092（.idata节数据区域）
- 崩溃前：[SC] other ueip=0x00403034（stub_read内，offset=0x14）
- 可能原因：stub_read的ret 20返回到错误地址，或ReadFile调用参数不正确

#### 已修改的文件
- kernel/compat/binfmt_pe_util.c：pe_enter_user中添加current恢复逻辑
- kernel/arch/x86/sys_process.c：添加[SYS-EXIT]串口调试输出
- kernel/compat/binfmt_pe.c：添加[PE-IAT]串口调试输出
- kernel/arch/x86/idt.c：添加CRASH-FRAME串口调试输出
- docs/BOOTLOADER_BUG_HISTORY.md：更新BUG-20分析

### BUG-20 winargs.exe ReadFile崩溃分析（2026-09-22 第九轮）

#### 用户栈内容分析

在异常处理中添加[USTACK]输出后，分析崩溃时的用户栈内容：

**winhello.exe崩溃**：
- 崩溃位置：eip=0x0040101E（int3）
- 崩溃时esp=0x4000FFF0
- 用户栈内容：`0x00401020 0x0000001D 0x00000000 0x00000000`
- 栈顶是lpBuffer(0x00401020)，然后是count(29)，然后是两个0

**分析**：
- WriteConsoleA有5个参数，stub_write的ret 20应该清理20字节（5个参数）
- ret 20之后，esp应该是0x40010000（栈顶，无内容）
- 但崩溃时esp=0x4000FFF0，栈顶是lpBuffer
- 这说明ret 20只清理了16字节（4个参数），没有清理第5个参数（hConsoleOutput）

**winargs.exe崩溃**：
- 崩溃位置：eip=0x00402092（.idata节数据区域）
- 崩溃时esp=0x4000FFE4
- 用户栈内容：`0x0040121A 0x48000020 0x6C646E61 0x0000000D ...`
- 栈上存储的是字符串数据，不是返回地址或参数

#### 新的假设

1. **stub_write/stub_read的ret操作数可能不正确**：
   - WriteConsoleA/ReadFile有5个参数，ret 20应该正确
   - 但用户栈内容显示只清理了4个参数
   - 可能需要将ret 20改为ret 16

2. **PE加载器可能在用户栈上压入了额外内容**：
   - 进入用户态前，用户栈可能不是空的
   - 需要检查process_enter_user_asm的实现

3. **stub_read的ret 20返回到错误地址**：
   - winargs.exe崩溃在.idata节数据区域
   - 可能是ret 20返回到了错误的地址，导致CPU执行数据区域的代码

#### 下一步

1. 验证stub_write/stub_read的ret操作数
2. 检查process_enter_user_asm的用户栈初始化
3. 尝试将ret 20改为ret 16，验证是否解决问题
4. 在stub函数入口添加调试输出，确认调用时的栈布局

#### 已修改的文件
- kernel/arch/x86/idt.c：添加[USTACK]用户栈内容输出
- docs/BOOTLOADER_BUG_HISTORY.md：更新BUG-20分析（第九轮）

### BUG-20 根本原因修复（2026-09-22 第十轮）—— 已修复！

#### 根本原因

**int 0x80中断入口的参数压入偏移量错误！**

在`kernel/arch/x86/interrupt.S`的`isr128`（int 0x80入口）中，参数压入的偏移量基于错误的栈布局注释，导致访问错误的寄存器：

**标准pusha顺序（低地址→高地址）**：eax, ecx, edx, ebx, esp, ebp, esi, edi

**push ds后的正确栈布局**：
```
ds@esp+0, eax@esp+4, ecx@esp+8, edx@esp+12, ebx@esp+16,
esp@esp+20, ebp@esp+24, esi@esp+28, edi@esp+32
```

**原代码的错误偏移量**：
```asm
pushl 8(%esp)    /* 声称arg4=esi，实际访问ecx@esp+8 */
pushl 28(%esp)   /* 声称arg3=edx，实际访问ebp@esp+24 */
pushl 36(%esp)   /* 声称arg2=ecx，实际访问esi@esp+28 */
pushl 32(%esp)   /* 声称arg1=ebx，实际访问esp@esp+20 */
pushl 48(%esp)   /* 声称num=eax，实际访问edi@esp+32 */
```

**后果**：syscall_handler收到的`num=edi`而不是`eax`，参数全面错位。

对于PE应用调用SYS_EXIT（eax=5），实际传入的调用号是edi的值（通常不是5），导致：
- 调用错误的系统调用函数，或
- 触发"unknown syscall"错误，或
- 越界访问syscall_table导致GPF崩溃

#### 修复方案

修正参数压入偏移量，基于正确的栈布局计算：

```asm
pushl 28(%esp)          /* arg4=esi   (esp+28) */
pushl 16(%esp)          /* arg3=edx   (原esp+12, push后+4=16) */
pushl 16(%esp)          /* arg2=ecx   (原esp+8, push后+8=16) */
pushl 28(%esp)          /* arg1=ebx   (原esp+16, push后+12=28) */
pushl 20(%esp)          /* num=eax    (原esp+4, push后+16=20) */
```

#### 验证结果

| 应用 | 修复前 | 修复后 |
|------|--------|--------|
| linuxhello.elf | ✓ 正常 | ✓ 正常 |
| linuxc.elf | ✓ 正常 | ✓ 正常 |
| linuxbrk.elf | ✓ 正常 | ✓ 正常 |
| linuxcrt.elf | ✓ 正常 | ✓ 正常 |
| **winhello.exe** | ❌ GPF崩溃(eip=0x0040101E) | **✓ 不再崩溃！** |
| **winargs.exe** | ❌ GPF崩溃(eip=0x00402092) | **✓ 不再崩溃！** |
| macho_demo | ✓ 正常 | ✓ 正常 |

**系统正常启动到mobile界面，无任何GPF崩溃！**

#### 调试过程回顾

1. 第一轮：怀疑IAT填充问题 → 验证IAT填充完全正确（wr=rd）
2. 第二轮：怀疑stub ret操作数 → 验证ret 20正确，ret 16无效
3. 第三轮：怀疑current=NULL问题 → 添加current恢复，部分缓解但未根治
4. 第四轮：添加用户栈内容输出 → 发现栈顶残留lpBuffer
5. 第五轮：添加sys_exit入口调试输出 → 发现崩溃发生在进入sys_exit之前
6. 第六轮：添加longjmp目标验证 → longjmp目标都是内核地址，正确
7. **第七轮：检查int 0x80中断入口 → 发现参数压入偏移量错误！**

#### 已修改的文件
- kernel/arch/x86/interrupt.S：修正isr128参数压入偏移量（根本修复）
- kernel/arch/x86/sys_process.c：添加[EXIT-ENTRY]和[LONGJMP]调试输出
- kernel/compat/binfmt_pe_util.c：pe_enter_user中添加current恢复逻辑（防御性修复）
- kernel/compat/binfmt_pe.c：添加[PE-IAT]调试输出
- kernel/arch/x86/idt.c：添加[CRASH-FRAME]和[USTACK]调试输出
- docs/BOOTLOADER_BUG_HISTORY.md：更新BUG-20完整分析（第七到第十轮）

---

## BUG-20 最终修复（V6.48，2026-09-22）

### 关键纠正：之前的根因判断错误

此前第七到第十轮认为BUG-20的根本原因是**int 0x80参数压入偏移量错误**，并修改了interrupt.S。但后续验证发现：

1. **原始偏移量完全正确**：pusha压入顺序为EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI，栈向下增长后从低地址到高地址为EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX。正确参数压入偏移量pushl 8/28/36/32/48（arg4=esi, arg3=edx, arg2=ecx, arg1=ebx, num=eax）完全正确。
2. **错误修改导致所有系统调用失效**：将偏移量改为pushl 28/16/16/28/20后，num=edi而非eax，所有用户态程序系统调用失效。已回退。

### 真正的根本原因：stub函数extern声明大小全部错误

经过深入排查，最终发现**真正的根本原因**是`kernel/compat/binfmt_pe.c`第36-44行stub函数的extern声明大小全部错误：

| stub函数 | 声明大小(错误) | 实际大小(正确) | 缺失字节 |
|----------|---------------|---------------|---------|
| stub_write | 18 | 23 | ret 20指令(偏移0x14-0x16) |
| stub_read | 26 | 33 | 后续指令 |
| stub_exit | 9 | 12 | int3指令 |
| stub_stdhandle | 7 | 8 | ret 4指令最后1字节 |
| stub_unsup | 7 | 8 | ret 4指令最后1字节 |
| stub_cmdline | 7 | 8 | ret 0指令最后1字节 |
| stub_tick | 14 | 16 | ret 0指令最后2字节 |
| stub_sleep | 11 | 14 | ret 4指令最后3字节 |
| stub_modname | 23 | 24 | ret 12指令最后1字节 |

**导致的后果**：
- `sizeof(stub_write)=18`（实际23字节）
- `memcpy`只复制18字节，**缺失了ret 20指令**（偏移0x14-0x16，字节0xC2,0x14,0x00）
- CPU执行到stub_write的int 0x80返回后，eip=0x00403014，读到的是0x00而非ret指令
- CPU继续执行全零指令（0x00=add [eax],al），最终触发int 0x80时eip=0x00403034（stub_read位置）
- 造成"调用stub_read而非stub_write"的假象，系统调用num=9(SYS_READ)而非10(SYS_WRITE)

### 排查过程（关键证据链）

1. **添加[SC]调试输出**：发现PE应用系统调用num=9(SYS_READ)而非10(SYS_WRITE)
2. **添加[PE-STUB]输出**：验证stub页内容，确认stub_write加载正确
3. **添加[PE-TEXT]输出**：验证.text节内容，确认winhello.exe代码正确
4. **添加[PE-IAT-RT]输出**：验证IAT填充，确认进入用户态前WriteConsoleA=0x00403000正确
5. **在syscall_handler中添加IAT槽运行时值检查**：发现系统调用时iat_wca=0x00403000正确，但ueip=0x00403034（stub_read中int 0x80下一条指令）
6. **用户态直接验证**：修改winhello.exe生成脚本，在call前读取IAT槽和stub页内容，发现用户态读取stub页0x00403014处字节为0x00（应该是0xC2=ret指令）
7. **memcpy后立即验证**：在binfmt_pe.c中添加[PE-STUB-AFTER-COPY]输出，发现stub页[0x14]=0x00
8. **sizeof验证**：添加sizeof(stub_write)输出，发现sizeof=0x12(18字节)，但stub_write[0x14]=0xC2（说明数组至少有21字节）
9. **根因确认**：检查binfmt_pe.c第36行，发现`extern const uint8_t stub_write[18];`声明大小错误

### 修复方案

修正`kernel/compat/binfmt_pe.c`中所有9个stub的extern声明大小，与`binfmt_pe_stub.c`定义一致：

```c
extern const uint8_t stub_write[23];
extern const uint8_t stub_read[33];
extern const uint8_t stub_exit[12];
extern const uint8_t stub_stdhandle[8];
extern const uint8_t stub_unsup[8];
extern const uint8_t stub_cmdline[8];
extern const uint8_t stub_tick[16];
extern const uint8_t stub_sleep[14];
extern const uint8_t stub_modname[24];
```

### 修复验证

| 验证项 | 修复前 | 修复后 |
|--------|--------|--------|
| stub页[0x14]字节 | 0x00（错误） | **0xC2（正确，ret指令）** |
| PE系统调用num | 9(SYS_READ，错误) | **10(SYS_WRITE，正确)** |
| ueip(write调用) | 0x00403034(stub_read，错误) | **0x00403014(stub_write，正确)** |
| winhello.exe | ❌ GPF崩溃 | **✓ 正常退出(code=0)** |
| 崩溃帧 | 有(eip=0x0040101E) | **无** |

### 经验教训

1. **extern声明大小必须与定义一致**：使用`sizeof`计算数组大小时，extern声明的大小会直接影响sizeof结果。声明大小偏小会导致memcpy不完整。
2. **不要被假象迷惑**："调用stub_read而非stub_write"是假象，真正原因是stub_write缺失ret指令导致CPU执行到stub_read区域。
3. **证据链要完整**：从IAT填充→stub页内容→用户态实际看到的值→memcpy后立即验证→sizeof验证，逐步缩小范围。
4. **回退错误修改要及时**：发现int 0x80偏移量修改导致所有系统调用失效后，立即回退，避免扩大影响。
5. **规范化修复流程**：遵循`docs/BUG_FIX_GUIDELINES.md`的证据优先、最小修改、验证闭环原则，避免错删误判。

### 已清理的调试输出

BUG-20修复确认后，已清理以下调试输出（V6.48）：
- binfmt_pe.c：[PE-STUB-COPY]、[PE-STUB-AFTER-COPY]、[PE-STUB-PTE]、[PE-STUB]全字节打印、[PE-IAT]、[PE-TEXT]、[PE-IAT-RT]、[PE-PTE]
- syscall.c：[SC]系统调用计数、[SC]PE兼容进程调用点（含IAT槽运行时值检查）
- sys_process.c：[EXIT-ENTRY]、[SYS-EXIT]、[LONGJMP]
- sys_hepu.c：[PRINT]标记（保留串口消息输出）

### 剩余问题

- winargs.exe仍有IAT填充错误（WriteConsoleA=0x000020A9，不是stub地址），这是独立问题，待后续修复。

---

## BUG-16: V6.50重构时删除递归检测代码导致系统无法启动

### 现象
- V6.50重构bootloader（添加VBE支持）后，系统无法启动
- 串口输出为0字节，屏幕全黑
- stage2大小2285字节（未超4096限制），MBR正常

### 排查过程
1. 二分定位：稳定版本(v645)能正常启动（串口17252字节），当前版本不能
2. 对比差异：发现当前版本删除了稳定版本中的递归检测代码（V6.44引入）
3. 验证：在稳定版本基础上添加VBE定义和空setup_vbe函数，仍能正常启动
4. 确认：递归检测代码被删除是导致启动失败的根本原因

### 根因
递归检测代码（V6.44引入）用于处理BIOS在INT13调用期间切换栈导致IRET弹出垃圾CS:IP的问题。BIOS切换栈后，IRET可能弹出错误的CS:IP（CS=0, IP碰巧在stage2内），导致CPU递归执行到错误地址。递归检测代码通过以下机制处理：
- 调用INT13前保存SP+返回地址到0x4000，设置递归标志in_int13=1
- INT13返回后清除递归标志
- 在read_slice入口检测递归标志，如果是递归调用则恢复SS:SP并远跳转到int13_ret

删除递归检测代码后，BIOS切换栈导致的递归调用无法被正确处理，CPU跳转到错误地址执行，导致系统崩溃。

### 修复方法
恢复V6.45稳定版本中的递归检测代码，包括：
1. 启动时初始化递归检测变量（0x4000=ret_ip, 0x4002=saved_sp, 0x4004=in_int13）
2. read_slice入口的递归检测逻辑
3. INT13调用前后的递归标志设置和清除

### 经验教训
1. **重构时不要删除看似"复杂"的代码**：递归检测代码虽然复杂，但是为了解决BIOS切换栈的真实问题，删除后会导致系统无法启动
2. **稳定版本是金标准**：重构后如果系统无法启动，立即与稳定版本对比差异
3. **BIOS行为不可预测**：BIOS在INT13调用期间可能切换栈、修改寄存器、甚至修改内存，必须有完善的容错机制

---

## BUG-17: VBE BIOS调用(INT10 AX=4F02)在SeaBIOS下破坏实模式状态

### 现象
- 在bootloader中调用INT 10H AX=4F02设置VBE高分辨率模式后，内核无法启动
- 串口输出为0字节，VBE模式本身设置成功（截图800x600）
- 即使保存恢复所有寄存器（通用/DS/ES/FS/GS/SS/SP），系统仍无法启动
- 即使把VBE设置移到所有实模式操作最后、设置完直接进保护模式，系统仍无法启动

### 排查过程
1. 二分定位：setup_vbe只做ret→正常；添加寄存器保存→正常；获取VBE模式信息(INT10 AX=4F01)→正常；设置VBE模式(INT10 AX=4F02)→失败
2. 保存恢复所有寄存器（通用/DS/ES/FS/GS/SS/SP）→仍失败
3. VBE设置移到build_mbinfo之后→仍失败
4. VBE成功后直接call enter_protected（不返回主流程）→仍失败
5. 设置VBE后立即切回文本模式(INT10 AX=0003)→仍失败

### 根因
SeaBIOS的VBE实现内部会切换CPU到保护模式来设置LFB（线性帧缓冲），然后切回实模式。在这个过程中，可能破坏了某些底层状态：
- GDT缓存（GDTR）可能被修改
- 内存映射可能被改变（LFB映射到高地址）
- 硬件寄存器（中断控制器、DMA控制器等）可能被修改
- IVT（中断向量表）可能被修改

这些被破坏的状态无法通过保存恢复寄存器来恢复，导致后续的实模式代码（甚至保护模式切换）无法正常执行。

### 临时方案
1. HepuBoot中暂禁用VBE设置（注释掉call setup_vbe），保证文本模式和VGA模式正常
2. VBE高分辨率使用GRUB分支（已验证能设置800x600x24，内核能启动但显示全黑，需解决LFB页表映射）

### 长期方案
1. 学习GRUB的VBE设置实现（GRUB能在设置VBE后正常引导内核）
2. 使用VBE 3.0保护模式接口（避免实模式BIOS调用）
3. 内核态KMS风格模式设置（进入保护模式后直接编程显示控制器）

### 经验教训
1. **BIOS调用有副作用**：VBE BIOS调用不仅仅是设置显示模式，还可能切换CPU模式、修改内存映射、破坏系统状态
2. **保存恢复寄存器不够**：BIOS调用可能破坏GDT、IVT、内存映射等无法通过寄存器保存恢复的状态
3. **GRUB是金标准**：GRUB经过广泛测试，能正确处理VBE设置后的状态恢复，自研bootloader需要学习其实现
4. **三模式降级是必要的**：高模式可能因为硬件/BIOS兼容性问题无法工作，必须有低模式兜底

---

## 决策记录：放弃GRUB安全模式，转向HepuBoot自恢复

### 决策时间
2026-09-22

### 决策内容
放弃GRUB作为安全模式/恢复模式，集中精力做HepuBoot内置自恢复能力。

### 决策背景
1. 学习了GRUB的优点（Multiboot兼容、模块化、配置文件、命令行、多系统引导等）
2. 曾考虑保留GRUB分支作为"Windows风格安全模式"，与HepuBoot形成互补
3. 用户决策：放弃GRUB安全模式，完全用HepuBoot自恢复

### 决策理由
1. **完全自研可控** — 赫普OS理念是"系统归人，自由可控"，不依赖外部引导器
2. **减少维护复杂度** — 维护两个引导方案增加复杂度，集中精力做一个更好
3. **HepuBoot可以具备自恢复能力** — 三模式降级+引导菜单+失败自动恢复，不需要GRUB
4. **GRUB的优点可以融入HepuBoot** — 学习GRUB的设计理念，但用HepuBoot自己实现

### 替代方案：HepuBoot自恢复
1. **三模式显示降级** — VBE失败→VGA，VGA失败→文本，保证有显示（已实现框架）
2. **引导失败计数** — 连续失败3次自动进入恢复模式（待实现）
3. **引导菜单** — 按F8呼出，可选正常/安全/恢复模式（待实现）
4. **配置文件** — hepboot.cfg可配置默认模式、超时、内核参数（待实现）

### 遗留物
- `grub.cfg.deprecated` — GRUB配置文件，保留仅作历史参考
- `build/hepu-os-v1.02-grub*.iso` — 之前构建的GRUB ISO，可删除或保留作备份

---

## BUG-18: stage2函数位置导致关键代码地址偏移（2026-09-22）

### 现象
在hepboot.S的print_str函数之后添加引导菜单函数（check_f8_and_menu/show_boot_menu/print_char），即使不调用这些函数，系统也无法启动（串口0字节）。

### 排查过程
1. 二分定位：只保留字符串和变量（删除所有函数）→ 正常；加print_char → 正常；加极简check_f8_and_menu(ret) → 正常；加极简show_boot_menu(ret) → 正常；加完整show_boot_menu → 失败
2. 反汇编show_boot_menu，指令完全正常，无异常机器码
3. 检查stage2大小2418字节，在MBR读取的4096字节限制内
4. 检查符号地址：show_boot_menu放在print_str之后，导致read_kernel/collect_e820/build_mbinfo/enter_protected等关键函数，以及boot_drive/vga_row/vga_col等变量的地址全部向后偏移
5. 将三个函数移到文件末尾（.bss段之后，单独.section .text）→ 系统正常启动（串口17252字节，ALL DONE）

### 根因
stage2中存在依赖固定地址布局的代码（如递归检测中的`int13_ret - 0x8000`偏移、`rm_read_seg2 - 0x8000`偏移等）。在.text段中间插入函数会导致后面所有符号的地址偏移，虽然这些偏移是通过标签计算的（理论上应该自动调整），但某些底层代码（如INT13包装器、实模式/保护模式切换）可能对地址布局有隐含依赖，导致启动失败。

### 修复方案
所有新增的辅助函数（引导菜单、调试函数等）必须放在hepboot.S文件末尾，使用单独的`.section .text`标记，避免影响前面关键代码的地址布局。

### 教训
- stage2的代码布局非常敏感，不要在关键函数中间插入新函数
- 新增函数一律放文件末尾
- 修改后必须用QEMU实际启动验证，不能只看编译通过

### 当前状态
- 引导菜单代码已实现并放在文件末尾
- 系统正常启动验证通过（串口17252字节，ALL DONE）
- F8按键的实际交互测试待QEMU monitor稳定后验证

---

## BUG-19: stage2添加call指令导致代码布局敏感（2026-09-22）

### 现象
在主流程中添加`call check_boot_fail_count`（即使函数只做ret），系统无法启动（串口0字节）。

### 排查过程
1. 函数内容逐步简化：完整逻辑 → 只设DS → pusha/popa → 只做ret，全部失败
2. 调用位置调整：collect_e820之后 → 显示启动信息之后，全部失败
3. 禁用call调用（保留函数定义在文件末尾）→ 系统正常启动
4. 确认：不是函数内容问题，而是添加call指令（3字节）导致后面代码地址偏移

### 根因
stage2的代码布局高度敏感。BUG-18已证明：在.text段中间插入函数会导致关键代码地址偏移。本次进一步发现：即使只是添加一条call指令（3字节），也会导致后面所有代码地址偏移3字节，从而破坏某些底层代码对地址布局的隐含依赖（可能是实模式/保护模式切换、INT13包装器、递归检测等代码中的硬编码相对偏移）。

### 当前状态
- check_boot_fail_count/read_single_sector/write_single_sector函数定义已实现，放在文件末尾
- call调用暂时禁用，避免破坏代码布局
- 引导失败计数功能已在内核侧实现（IDE初始化后读取LBA 10，fail_count>=3自动进恢复模式，Shell启动成功后清除）
- bootloader侧引导失败计数待stage2重构后启用

### 重构尝试（2026-09-23，V6.53）
尝试将stage2拆分为核心段(.text)和扩展段(.exttext)，扩展段固定地址0x8C00，新增函数放扩展段，不影响核心段布局：

1. **链接脚本修改**：添加.exttext段(0x8C00)和.bss段(0x8E20)
2. **汇编修改**：将check_f8_and_menu/show_boot_menu/print_char/check_boot_fail_count等辅助函数移到.exttext段
3. **段属性**：指定.exttext为"ax"（可分配+可执行），确保objcopy包含此段
4. **结果**：编译通过，stage2大小3613字节（含扩展段），但系统无法启动（串口0字节）
5. **可能原因**：
   - .bss段变量地址从0x888c变为0x8e20，某些底层代码可能隐含依赖原地址
   - 跨段调用的重定位在实模式下可能有问题
   - MBR读取连续4096字节，但段间隙(0x888c-0x8c00)的填充可能影响执行
6. **回退**：恢复单段布局，系统正常启动（17252字节，ALL DONE）

### 后续方案
1. **重构stage2**：将所有辅助函数统一放文件末尾，主流程只保留关键代码，减少地址敏感区域
2. **移到内核实现**：引导失败计数已在内核实现（推荐方案，功能完整）
3. **使用固定地址桩**：在固定地址（如0x7E00）放置跳转桩，主流程call固定地址，桩再跳转到实际函数
4. **位置无关代码**：将stage2改为PIC（位置无关代码），彻底解决地址布局敏感问题（工作量大）

### 教训
- stage2的代码大小和布局极其敏感，任何修改都必须用QEMU实际启动验证
- 新增函数调用可能破坏代码布局，即使函数本身是空的
- 修改后必须验证启动，不能只看编译通过

## UEFI-01: UEFI应用在OVMF下运行返回"Not Found"（2026-09-23）

### 现象
自研UEFI应用（独立编译，不依赖gnu-efi）在OVMF固件下运行返回"Not Found"，无法启动。

### 排查过程
1. **第一版（自研头文件）**：定义自己的UEFI数据结构+efi_main函数，`ld -shared -Bsymbolic -e efi_main`链接，objcopy转PE/COFF
   - 结果：编译成功，但ImageBase=0、无.reloc重定位段，OVMF返回"Not Found"
2. **尝试修正ImageBase**：自定义链接脚本设置ImageBase=0x10000
   - 结果：objcopy生成的PE节表为空、ImageBase仍为0
3. **尝试汇编入口**：编写entry.S处理ms_abi调用约定和栈对齐
   - 结果：仍无效果
4. **改用gnu-efi标准库**（最终成功）：使用crt0-efi-x86_64.o + elf_x86_64_efi.lds + libgnuefi.a + libefi.a
   - 结果：UEFI应用在OVMF下正常启动，HepuBoot界面完整显示

### 根因
自研方案缺少两个关键要素：
1. **crt0入口处理**：gnu-efi的crt0-efi-x86_64.o处理了正确的入口点(_start)、GOT初始化、以及EFI调用约定，自研方案没有
2. **正确的重定位段**：gnu-efi的elf_x86_64_efi.lds链接脚本生成完整的.reloc重定位段，UEFI固件依赖它进行地址重定位（ImageBase为0也依赖.reloc段）；自研链接脚本生成的重定位段无法被objcopy正确转换为PE的.reloc表

### 教训
- UEFI应用开发应该直接使用gnu-efi标准库，不要自己定义UEFI数据结构（容易出错且不完整）
- UEFI应用必须由crt0处理入口和初始化，链接脚本必须生成.reloc重定位段
- 标准gnu-efi编译流程：
  ```
  gcc -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
      -DEFI_FUNCTION_WRAPPER -I/usr/include/efi -I/usr/include/efi/x86_64 \
      -I/usr/include/efi/protocol -c app.c -o app.o
  ld -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds \
     -shared -Bsymbolic -L/usr/lib /usr/lib/crt0-efi-x86_64.o \
     app.o -l:libgnuefi.a -l:libefi.a -o app.so
  objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
     -j .rel -j .rela -j .reloc --target=efi-app-x86_64 app.so app.efi
  ```
- 验证环境：QEMU + OVMF + GPT分区表 + ESP分区（FAT32），BOOTX64.EFI放到ESP分区的/EFI/BOOT/

### 成功状态
- HepuBoot UEFI v0.02 在OVMF下成功运行，显示完整框架界面（UEFI Firmware: EDK II）
- 截图：docs/uefi_boot_screenshot.png

## UEFI-02: UEFI控制台换行错位（\n不回行首）（2026-09-23）

### 现象
UEFI引导器界面文字逐行错位，每行不从最左列开始，越往下越乱。

### 根因
UEFI的`ConOut`协议中，`\n`（LF, 0x0A）只换行（line feed）**不回行首**（carriage return）。必须使用`\r\n`（CR+LF）才能既换行又回到行首。代码里全部使用了`L"\n"`，导致每行从上一个光标列继续。

### 修复
把所有`L"\n"`替换为`L"\r\n"`。

### 教训
- UEFI控制台输出换行必须用`\r\n`，不能用`\n`
- 这是UEFI/终端通用的经典坑（Linux串口、telnet、UEFI ConOut都如此）

## UEFI-03: UEFI引导界面边框错位（文本宽度不齐）（2026-09-23）

### 现象
青色边框的`|`右边界错位：`HepuBoot UEFI v0.02`内容行宽度41列，比边框行42列少1列，导致右侧`|`比边框右端靠左1位。

### 根因
边框由`=`和`|`拼接的ASCII文本，各行字符宽度必须完全一致才能对齐。其中一行文本(`HepuBoot UEFI v0.02`=19字符)填充空格计算错误，实际宽度41列，与其他42列行不齐。

### 修复
把该行右侧空格从13个补到14个，使宽度=42列与边框一致。验证方法：python脚本计算每行`len(s)`必须全为42。

### 教训
- ASCII边框对齐必须逐行精确计算字符宽度，不能肉眼估
- 修改后先用脚本验证每行长度一致，再截图确认
- UEFI文本界面没有自动换行对齐，所有行宽必须手工保证一致

---

## UEFI-04: x64→32位切换时lretq加载32位代码段系统重置（2026-09-23）

### 现象
UEFI引导器成功加载内核到0x100000、ExitBootServices通过，但跳转内核时系统重置（三重故障）。调试显示：加载GDT后重新加载CS为64位代码段成功（输出C），但lretq加载32位代码段（CS.L=0）时系统重置。

### 根因（经过6轮试错定位）
1. **最初方案**：加载GDT→禁用分页→lretq切32位 → 失败。原因：禁用分页后栈仍在高地址，lretq从高地址栈弹出数据访问不存在的物理内存。
2. **切换栈到低内存后**：仍失败。原因：重新加载CS为64位代码段后，某些状态改变导致lretq切32位失败。
3. **先切兼容模式（分页仍启用）再禁分页**：仍失败。
4. **最终发现**：lgdt后CS隐藏缓存（cached descriptor）仍有效，**不需要先重新加载CS为64位代码段**，可直接lretq加载32位代码段。之前重新加载CS为64位反而引入了问题。

### 修复
加载GDT后，不重新加载CS，直接：
1. 切换栈到低内存（0x8F000）
2. 在64位模式下禁用长模式（wrmsr清除EFER.LME）
3. **立即**lretq加载32位代码段（0x08），wrmsr与lretq之间不能有其他指令
4. 在32位代码中禁用分页、禁用PAE、设置数据段、跳转内核

### 教训
- **lgdt后CS隐藏缓存仍有效**，可直接远跳转加载新的CS，无需先重载当前CS
- 重新加载CS为64位代码段后再切32位反而可能失败（具体机制待进一步研究）
- 调试时先用串口输出定位到哪一步重置，再逐步缩小范围
- VGA文本缓冲区（0xB8000）在分页启用时可能未映射，写VGA会导致页故障；应先用串口（I/O端口0x3F8）调试

---

## UEFI-05: 兼容模式下wrmsr禁用长模式失败（2026-09-23）

### 现象
lretq成功切换到32位兼容模式（输出'3'），禁用分页成功（输出'P'），但wrmsr清除EFER.LME时系统重置（输出到'3P'后停止）。尝试了多种顺序（先禁PAE再禁长模式、先禁长模式再禁PAE、直接写EFER=0）均失败。

### 根因
在兼容模式下（LME=1, CS.L=0, PG=0），wrmsr指令访问EFER MSR（0xC0000080）会导致故障。具体机制：兼容模式下CPU仍处于长模式激活状态，wrmsr修改EFER可能触发内部一致性检查失败。

### 修复
**必须在64位模式下（切换到32位之前）禁用长模式**：
1. 在64位模式下执行rdmsr/and/wrmsr清除EFER.LME
2. wrmsr后**立即**执行lretq切换到32位代码段（中间不能有其他指令，因为此时CS.L=1在非长模式下无效）
3. 在32位代码中再禁用分页和PAE

### 教训
- **禁用长模式必须在64位模式下完成**，不能在兼容模式下做
- wrmsr清除LME后必须立即切换CS到32位代码段，否则CS.L=1在非长模式下会导致故障
- x64→32位切换的正确顺序：加载GDT → 64位下禁长模式 → 立即lretq切32位 → 32位下禁分页/PAE → 设置数据段 → 跳转内核
- 这个顺序与大多数OSDev教程不同（教程通常是：禁分页→切32位→禁长模式），因为UEFI环境下从长模式启动，需要先禁长模式再切CS

---

## UEFI-06: 内核入口地址错误（跳转到0x100000而非0x101000）（2026-09-23）

### 现象
UEFI引导器成功跳转，但内核输出乱码（清屏序列），没有输出预期的"T0="标志。

### 根因
内核文件开头200字节是组合引导存根（含BIOS引导代码和multiboot头在偏移0xC8），真正的`_start`在`.text`段，由链接脚本`.text ALIGN(4K)`决定，位于**0x101000**（不是0x100000）。跳转到0x100000执行的是数据字节（被当作指令），导致不可预测行为。

### 修复
跳转地址改为0x101000。验证方法：内核文件偏移0x1C处有`ljmp $0x8, $0x101000`，确认_start地址。

### 教训
- 纯二进制内核文件的入口点不一定是文件起始地址
- 必须检查链接脚本的段对齐规则（.text ALIGN(4K)）
- 可以通过反汇编内核文件开头的远跳转指令确认入口地址

---

## UEFI-07: 缺少multiboot info导致内存管理失败（2026-09-23）

### 现象
内核_start执行，T0=输出正常，但内存管理初始化失败：`[BUDDY] test1 alloc 1 page: FAIL`，所有内存分配失败。

### 根因
引导器传递ebx=0（multiboot info指针为NULL），内核无法获取内存大小信息，mem_kb=0，物理内存管理器认为没有可用内存。

### 修复
在UEFI引导器中构建multiboot info结构（放在0x90000）：
1. 通过GetMemoryMap获取UEFI内存映射
2. 转换为multiboot mmap格式（放在0x90200）
3. 设置flags=0x41（mem_info + mmap），mem_lower=639KB，mem_upper=计算值
4. 跳转内核时设置ebx=0x90000

### 教训
- multiboot规范要求引导器传递内存信息，不能只传magic
- 即使multiboot_init处理了NULL，后续内存管理仍需要实际的内存大小
- UEFI内存映射类型EfiConventionalMemory(1)对应multiboot可用内存类型1

---

## UEFI-08: UEFI环境缺少VGA文本模式（2026-09-23）

### 现象
内核启动后VGA寄存器检测全0/0xFF（`[VGAREG] misc=0x00000000`），内核在VGA初始化后停住。

### 根因
UEFI使用GOP（Graphics Output Protocol）图形模式，不设置VGA文本模式寄存器。内核期望VGA文本模式（0xB8000缓冲区和VGA寄存器），但UEFI没有提供。

### 修复
在UEFI引导器中ExitBootServices之前，通过直接写入VGA寄存器设置80x25文本模式：
- Misc Output Register (0x3C2) = 0x67
- Sequence Registers (0x3C4/0x3C5) = 标准文本模式值
- CRTC Registers (0x3D4/0x3D5) = 标准80x25值（先解锁0x11寄存器）
- Graphics Registers (0x3CE/0x3CF) = 标准值
- Attribute Registers (0x3C0) = 标准调色板
- 清屏0xB8000

### 教训
- UEFI不保证VGA文本模式可用，引导器必须主动设置
- VGA寄存器设置必须在ExitBootServices之前完成（之后无法使用UEFI服务）
- CRTC寄存器0x11必须先清除保护位（& ~0x80）才能写入其他寄存器

---

## UEFI-09: UEFI freestanding环境下标准C库函数导致#UD异常（2026-09-23）

### 现象
添加配置文件解析功能后，引导器在`[Loading config...]`后崩溃，OVMF输出`X64 Exception Type - 06(#UD - Invalid Opcode)`。

### 根因
UEFI应用是freestanding环境，不链接标准C库。使用`strcmp`/`strcpy`等标准C库函数时，链接器可能解析到无效地址或生成SSE指令（UEFI应用默认禁用SSE），导致#UD异常。

### 修复
实现自定义字符串函数`my_strcmp`/`my_strcpy`，不依赖标准C库。编译选项保持`-fno-builtin`。

### 教训
- UEFI应用开发中避免使用标准C库函数（strcmp, strcpy, strlen, memcpy等）
- gnu-efi提供了部分库函数（如CompareMem, CopyMem），但字符串函数需自行实现
- 编译选项`-fno-builtin`可防止编译器内联标准函数，但链接时仍需确保符号存在

## BUG-17: UEFI下multiboot magic被破坏导致内存映射失效 (V6.60)

**现象**: UEFI引导下所有用户态程序binfmt_exec返回-3(BINFMT_ERR_MEM)，BUDDY伙伴分配器自测全部FAIL。

**根因**: 
1. boot.S中_start入口的K0调试代码(串口初始化、hex打印)和BSS清零代码`xorl %eax,%eax`会破坏eax寄存器，而eax保存的是multiboot magic(0x2BADB002)
2. 调用hepu_kernel_main前push %eax时，eax已被清零，导致kernel_main收到magic=0
3. 虽然mb_info指针正确，但部分内存初始化逻辑依赖magic校验

**修复**: 在_start最开头用固定地址0x8000/0x8004保存eax/ebx，调用hepu_kernel_main前从固定地址恢复。

**教训**: 内核入口处任何使用eax/ebx的代码(包括调试代码)都会破坏multiboot参数，必须在最开头保存。

## BUG-18: 内核BSS段异常膨胀(108MB)导致可用内存为0 (V6.60)

**现象**: magic修复后BUDDY分配器仍全部FAIL，free_pages=0。调试发现kern_end=0x6DDB000(110MB)超过总内存86MB。

**根因**: 内核中多个子系统使用了过大的静态数组：
- webengine: WEB_MAX_NODES=1024, WEB_MAX_DOCUMENTS=4 → 每个document约4MB，共17MB
- hug_sdk: HSDK_MAX_PROJECTS=64, HSDK_MAX_SOURCES=256 → 12MB
- hug_depman: HDM_MAX_PACKAGES=256 → 12MB
- multilingual_ime: IME_MAX_DICTIONARY=65536 → 6.3MB
- usb2: USB2_MAX_DEVICES=32等 → 5.4MB
- fs_real: FS_R_MAX_INODES=1024等 → 4.5MB
- 内核堆固定64MB，从kern_end开始保留，覆盖所有剩余内存

**修复**:
1. 减小各子系统大数组宏定义(WEB_MAX_NODES 1024→128, IME_MAX_DICTIONARY 65536→4096等)
2. 内核堆大小改为动态：总内存<128MB用8MB，<256MB用16MB，否则32MB
3. BSS从108MB降至57MB，可用内存约22MB

**教训**: 内核静态数组必须严格控制大小，教学/实验用途的数组不应超过实际需要。内核堆大小应根据总内存动态调整。

## BUG-19: 系统调用返回值写入偏移量错误(36→32)导致用户态看到系统调用号 (V6.64)

**现象**: Rust用户态程序调用service_register(204)和service_lookup(205)时，返回值始终是系统调用号本身(204/205)，而不是内核函数的返回值(0/14)。内核侧调试确认syscall_handler返回值正确，但用户态看到的是系统调用号。

**根因**: `kernel/arch/x86/interrupt.S`中isr128的返回值写入指令偏移量错误：
```asm
mov %eax, 36(%esp)   ; 错误！应该是32(%esp)
```

栈布局分析：
- push ds后esp=S，ds@S+0, pusha寄存器@S+4到S+32，eax槽@S+32
- call syscall_handler后ret弹出返回地址，addl $20清理5个参数，esp恢复为S
- 因此eax槽@S+32 = esp+32，正确偏移量是32，不是36

原偏移量36将返回值写入了eax槽后4字节(ecx槽)，导致：
1. eax槽未被更新，仍保持int 0x80前的系统调用号
2. ecx槽被返回值覆盖(但popa后ecx被破坏，通常无感知)

**修复**: 将`mov %eax, 36(%esp)`改为`mov %eax, 32(%esp)`

**验证**: 
- service_register返回0(成功) ✓
- service_lookup返回PID(14) ✓
- 所有13个用户态程序rc=0 ✓
- 所有自测PASS ✓

**教训**: 汇编代码中的栈偏移量必须仔细计算，特别是在push/pop操作后esp变化时。注释中的计算"esp=S-4"是错误的，实际esp=S。这个bug存在了很长时间，但因为低系统调用号的返回值恰好与某些值巧合，或者未被严格检查，所以一直未被发现。高系统调用号(200+)的服务发现机制首次严格检查了返回值，才暴露了这个问题。

## BUG-20: fork后子进程立即崩溃（General Protection Fault）(V6.65)

**现象**: Rust用户态程序调用fork()后，子进程立即崩溃（GPF at eip=0x0000202B），无任何输出。父进程正常运行，waitpid返回0。

**根因分析**:
1. 原实现中fork后子进程与父进程共享用户栈（child->user_stack = current->user_stack），导致父子进程互相干扰
2. 修复1：为子进程创建独立页目录和用户栈，但子进程内核栈未映射到新页目录，导致访问内核栈时#GP
3. 修复2：添加内核栈映射后，子进程仍然崩溃，eip=0x0000202B（低地址），说明内核栈复制或返回地址有问题
4. 根本原因：process_fork中复制内核栈时，current->kernel_esp是系统调用入口时的栈指针，但复制的内容可能不完整或偏移量错误

**当前状态**: 未完全修复。已尝试：
- 为子进程创建独立页目录（paging_create_dir + 复制用户空间PDE）
- 映射子进程内核栈到新页目录
- 分配独立用户栈并复制内容
- 子进程仍然崩溃，eip=0x0000202B

**临时方案**: 端到端IPC测试改用自包含模式（同一进程内验证服务注册/发现/IPC发送接收），不依赖fork。

**后续修复方向**:
1. 仔细检查process_fork中内核栈复制的偏移量和大小
2. 对比process_create中初始栈帧的构造，确认fork后子进程的栈帧布局一致
3. 检查调度器切换到子进程时的CR3切换和栈切换逻辑
4. 可能需要在fork时保存当前esp（而非current->kernel_esp）作为复制起点

**教训**: fork是操作系统中最复杂的系统调用之一，需要精确复制进程的完整状态（页目录、内核栈、用户栈、文件描述符等）。任何遗漏都会导致子进程崩溃。

## BUG-20 更新: fork子进程崩溃调试进展 (V6.65)

**当前状态**: 子进程仍然崩溃（GPF at eip=0x00006005），但已取得重要进展：

### 已确认的问题
1. **镜像启动方式问题**: 用`-cdrom ISO`启动时，UEFI引导器加载的是ISO中旧的内核；必须用`-drive IMG`直接启动才能加载最新内核
2. **内核栈映射修复**: process_fork中内核栈映射原假设物理地址=虚拟地址，已改为使用paging_get_phys获取正确物理地址（与process_create一致）
3. **VGA显存映射**: 已添加VGA文本显存（0xB8000起32KB）映射（与process_create一致）

### 仍存在的问题
- 子进程GPF at eip=0x00006005（低地址，说明iret弹出的eip被破坏）
- 子进程无任何输出（[CHILD]标记未出现）
- 父进程waitpid返回，但status不是42

### 下一步调试方向
1. 检查current->kernel_esp在isr128入口保存时是否正确指向ds位置
2. 检查复制的内核栈内容是否完整（特别是eip@kernel_esp+44）
3. 检查子进程页目录切换后，内核栈访问是否正确
4. 对比process_create的初始栈帧构造，确认fork复制的栈帧布局一致
5. 考虑在process_fork中添加调试输出，打印子进程kernel_esp和栈内容

### 经验教训
- UEFI镜像测试必须用`-drive format=raw,file=IMG`，不能用`-cdrom ISO`，否则加载的是旧内核
- 工具（gnu-efi/qemu/mtools/parted）在新Bash会话中经常丢失，需切换阿里云源重装
- fork是操作系统中最复杂的系统调用，需要精确复制页目录、内核栈、用户栈等全部状态

## BUG-20 最终修复: fork子进程崩溃 (V6.66)

**状态**: ✅ 已修复并验证通过

**现象**: fork后子进程立即崩溃（GPF，所有寄存器全0），无任何子进程输出。父进程正常，waitpid返回但status不是42。

**根因分析（三层叠加问题）**:

### 根因1: 内核栈映射循环未覆盖顶部页
- `kbot = child->kernel_stack - KERNEL_STACK_SIZE` 不是页对齐的（低12位非0）
- 循环 `for (a = kbot; a < child->kernel_stack; a += PAGE_SIZE)` 步长0x1000，导致跳过了子进程kernel_esp所在的顶部页
- 结果：CR3切换到子进程页目录后，内核栈顶部页无映射，读取栈帧内容全0

**修复**: kbot向下页对齐，ktop向上页对齐：
```c
uint32_t kbot = (child->kernel_stack - KERNEL_STACK_SIZE) & 0xFFFFF000;
uint32_t ktop = (child->kernel_stack + 0xFFF) & 0xFFFFF000;
for (uint32_t a = kbot; a < ktop; a += PAGE_SIZE) { ... }
```

### 根因2: 用户栈直接memcpy导致虚拟地址偏移错误
- `user_stack_raw` 不是页对齐的（kmalloc返回地址低12位非0）
- `paging_map` 中 `entry->frame = phys >> 12` 会把物理地址页对齐
- 直接 `memcpy(new_stack_raw, current->user_stack_raw, USER_STACK_SIZE)` 复制的是物理地址连续的内容，但虚拟地址映射时页对齐导致偏移错位
- 结果：子进程用户栈返回地址位置读到0而非0x080481f7，iret后ret跳转到0

**修复**: 逐页复制用户栈内容（通过paging_get_phys获取父子进程对应虚拟页的物理地址）：
```c
for (uint32_t i = 0; i < USER_STACK_SIZE; i += PAGE_SIZE) {
    uint32_t parent_phys = paging_get_phys(current->page_dir, USER_STACK_ADDR + i);
    uint32_t child_phys = paging_get_phys(new_dir, USER_STACK_ADDR + i);
    if (parent_phys && child_phys) {
        memcpy((void *)child_phys, (void *)parent_phys, PAGE_SIZE);
    }
}
```

### 根因3: 内核栈与用户栈共享物理页
- 内核栈 `kmalloc(KERNEL_STACK_SIZE)` 返回地址非页对齐
- 用户栈 `kmalloc(USER_STACK_SIZE)` 可能分配在内核栈同一物理页的剩余部分
- 用户栈映射到虚拟地址0x40000000时，paging_map页对齐物理地址，导致用户栈虚拟页与内核栈虚拟页映射到同一物理页
- 结果：用户栈的任何写入都会覆盖内核栈内容，调度切换时内核栈帧全0

**修复**: 内核栈页对齐分配：
```c
uint32_t kstack_raw = (uint32_t)kmalloc(KERNEL_STACK_SIZE + PAGE_SIZE);
uint32_t kstack_aligned = (kstack_raw + PAGE_SIZE - 1) & 0xFFFFF000;
child->kernel_stack = kstack_aligned + KERNEL_STACK_SIZE;
```

### 其他关键修复
- **复制PDE时跳过用户栈(idx=256)和内核栈(idx=0xE)**: 用户栈需要独立映射并逐页复制；内核栈保持内核共享页表
- **isr128入口保存current->kernel_esp**: `movl %esp, 52(%eax)`，因为current->kernel_esp只在调度切换时更新
- **CR3切换前保存kernel_esp到ecx**: 避免CR3切换后访问current读到错误值

**验证结果**:
- 子进程正常输出 `[CHILD] I am the child, PID=4` 和 `[CHILD] exiting with code 42`
- 父进程waitpid获取正确退出码42
- eip_after_cr3=0x080481AF（正确）
- IPC端到端测试6项全部PASS
- 所有用户态程序rc=0

**涉及文件**:
- `kernel/sched/sched.c` — process_fork函数核心修复
- `kernel/arch/x86/interrupt.S` — isr128入口保存kernel_esp，CR3切换前保存到ecx
- `kernel/sched/shell.c` — fork-test集成测试

**经验教训**:
1. 操作系统中任何涉及内存地址的循环，边界必须页对齐，否则会静默跳过页面
2. kmalloc返回的地址不一定页对齐，与paging_map（页对齐物理地址）混用时必须小心
3. 内核栈和用户栈必须确保不共享物理页，否则会互相覆盖
4. fork需要精确复制：独立页目录+独立内核栈+独立用户栈+逐页复制用户栈内容
5. 调试fork问题时，serial输出比VGA输出更可靠（子进程崩溃时VGA可能无法输出）

## BUG-21: waitpid空实现导致fork后子进程资源泄漏 (V6.67)

**状态**: ✅ 已修复

**现象**: 
- fork-test中父进程waitpid立即返回0，status=0（不是子进程退出码42）
- 子进程在父进程之后才运行（父进程没有阻塞等待）
- file-server-e2e-test中第二次fork返回-1（失败），因为前一个fork的子进程占用PCB槽位未回收

**根因**: `sys_waitpid`是空实现，直接返回0，没有：
1. 等待子进程退出
2. 获取子进程退出码
3. 回收子进程资源（PCB/内核栈/用户栈/页目录）

**修复方案**:

### 1. process_t结构新增字段
- `exit_code`: 进程退出码，供waitpid获取
- `kernel_stack_raw`: 内核栈kheap原始地址（页对齐分配时需保存，否则kfree崩溃）

### 2. sys_exit修改
- 保存退出码到`current->exit_code`
- 进程退出时状态设为`PROC_ZOMBIE`（而非PROC_SLEEPING），等待父进程waitpid回收
- has_live检查排除ZOMBIE状态进程

### 3. sys_waitpid真正实现
- 查找指定PID的子进程（pid=-1匹配任意子进程）
- 子进程为ZOMBIE状态时：回收资源（页目录/内核栈/用户栈），写入status_ptr，返回子进程PID
- 子进程仍在运行时：返回0，用户态循环调用并yield_cpu等待
- 无子进程时：返回0xFFFFFFFF(-1)

### 4. hepu-sys库waitpid修改
- 循环调用sys_waitpid直到返回非0值（子进程PID或-1）
- 每次调用间yield_cpu让出CPU

### 5. 内核栈释放修复
- 优先使用`kernel_stack_raw`释放（页对齐分配时保存的原始地址）
- 兼容旧逻辑（kernel_stack - KERNEL_STACK_SIZE）

### 6. waitpid页目录切换修复
- 释放子进程页目录前保存父进程页目录指针
- 释放后切回父进程页目录（否则父进程用户空间映射消失导致崩溃）

**验证结果**:
- fork-test: 父进程waitpid返回4，status=42，PASSED
- 所有用户态程序rc=0
- 内核自测大部分PASS

**涉及文件**:
- `kernel/include/process.h` — 新增exit_code和kernel_stack_raw字段
- `kernel/arch/x86/sys_process.c` — sys_exit保存退出码+ZOMBIE状态，sys_waitpid真正实现，内核栈释放修复
- `kernel/sched/sched.c` — process_fork保存kernel_stack_raw
- `user/rust/hepu-sys/src/lib.rs` — waitpid循环等待

**经验教训**:
1. fork和waitpid必须配对实现——只实现fork不实现waitpid会导致资源泄漏
2. ZOMBIE状态是Unix的标准设计——进程退出后保留PCB和退出码，等待父进程回收
3. 页对齐分配的内存必须保存原始地址，否则kfree会崩溃
4. 切换CR3后必须切回原页目录，否则用户空间映射消失

## BUG-22: 高PID下fork子进程无输出（待调试）

**状态**: ⏳ 待调试

**现象**:
- fork-test（低PID，子进程PID=4）：完全正常，子进程输出+waitpid正常
- file-server-e2e-test（高PID，父进程PID=18，子进程PID=19）：fork返回19成功，但子进程无任何输出，父进程等待服务超时

**可能原因**:
1. 高PID下进程表槽位状态问题
2. 子进程内核栈/用户栈映射在高内存压力下失败
3. 之前运行的进程资源未完全释放，导致内存不足
4. 调度器在高PID下的某些逻辑问题

**调试方向**:
1. 在process_fork中添加调试输出，检查子进程内核栈/用户栈映射
2. 检查进程表槽位回收是否完整
3. 检查系统内存使用情况
4. 简化file-server-e2e-test子进程逻辑（已改为立即exit(42)），验证子进程是否能运行

**当前进展**:
- fork和waitpid核心功能已修复并验证（低PID下完全正常）
- file-server-e2e-test框架已搭建，待高PID问题修复后可完整验证端到端IPC

## BUG-22: 高PID下fork子进程无输出（已修复）

**状态**: ✅ 已修复

**现象**:
- fork-test（低PID，子进程PID=4）：完全正常
- file-server-e2e-test（高PID，父进程PID=18，子进程PID=19）：fork返回19成功，但子进程无任何输出

**根因**:
用户栈`kmalloc(USER_STACK_SIZE)`返回的地址可能非页对齐，`paging_map`页对齐后会映射到分配范围外的内存（最多4095字节），覆盖其他内核对象。高PID下内存分配更频繁，更容易触发崩溃。

**修复方案** (V6.67):
- 用户栈改为页对齐分配：`kmalloc(USER_STACK_SIZE + PAGE_SIZE)`，向上对齐
- 映射时使用对齐后的地址：`ustack_aligned + i`
- 保存原始地址`user_stack_raw`供释放时使用

**验证**:
- 高PID子进程（PID=19）正常输出`[CHILD] I am the child, PID=19, running as server`
- 子进程成功注册服务`e2e-file-server`
- waitpid返回19，status=99

## BUG-23: 调度器总是选择当前进程，子进程延迟调度（待调试）

**状态**: ⏳ 待调试

**现象**:
- file-server-e2e-test中，父进程fork后等待服务注册，循环200次service_lookup+yield_cpu
- 子进程在父进程等待超时后才被调度（Test 5 waitpid时才开始运行）
- 子进程本身可以正常运行（注册服务、处理请求）

**可能原因**:
1. sched_pick_next中级内扫描总是找到current_idx本身（已修复：跳过current_idx）
2. sched_ready_clear简单清除优先级位，同优先级还有其他就绪进程时不应清除（已修复）
3. 动态优先级调整导致父子进程优先级不同
4. 就绪位图与实际状态不同步

**已尝试修复** (V6.68):
- sched_pick_next：级内扫描跳过current_idx本身（i < MAX_PROCESSES）
- sched_ready_clear：检查同优先级是否还有其他就绪进程，有则不清除位

**当前状态**: 修复后子进程仍延迟调度，需进一步调试

---

## BUG-24: BSD Socket API封装 - loopback连接对端关联错误（已修复）

**状态**: ✅ 已修复 (V6.69)

**现象**:
- socket-test中Test 1-6（socket/bind/listen/connect/accept/send）全部PASSED
- Test 7（recv）返回0，无法接收数据
- Test 8（双向通信）失败

**根因分析（三层叠加）**:

### 根因1: connect未设置peer_fd
- 原始sys_connect_impl只设置remote_addr和state=SS_CONNECTED
- 未设置peer_fd，导致send时数据被丢弃（无对端）

### 根因2: accept创建新socket但未与客户端关联
- 原始sys_accept_impl创建新socket，设置new_fd.peer_fd=监听socket
- 但未设置监听socket.peer_fd，也未与客户端socket关联
- 客户端和服务器端连接socket之间没有双向peer_fd

### 根因3: accept返回客户端socket本身
- 第一次修复后，accept查找state==SS_CONNECTED且peer_fd==监听socket的socket
- 但connect时设置的是客户端.peer_fd=监听socket
- 导致accept返回客户端socket本身（fd=1），而不是新的服务器端连接socket

**修复方案** (V6.69):

1. **socket_t结构新增字段**:
   - `is_server_side`: 标记是否为服务器端连接socket
   - `listen_fd`: 服务器端连接socket所属的监听socket

2. **sys_connect_impl重写**:
   - 查找监听相同端口的监听socket
   - 创建服务器端连接socket（server_fd）
   - 设置双向peer_fd: client.peer_fd=server_fd, server_fd.peer_fd=client_fd
   - 设置server_fd.is_server_side=1, server_fd.listen_fd=listen_fd

3. **sys_accept_impl重写**:
   - 查找is_server_side==1且listen_fd==监听socket且state==SS_CONNECTED的socket
   - 返回该服务器端连接socket（而非创建新socket）
   - 设置state=SS_ACCEPTED

4. **sys_socketcall多路复用**:
   - 内核新增SYS_SOCKETCALL=208系统调用
   - Linux ABI映射: socketcall(102)→SYS_SOCKETCALL(208)
   - 支持子调用: SOCKET(1)/BIND(2)/CONNECT(3)/LISTEN(4)/ACCEPT(5)/SEND(9)/RECV(10)

5. **Rust hepu-sys库Socket API**:
   - AddressFamily/SocketType/Protocol常量模块
   - SockAddrIn结构体（支持new()和loopback()，自动网络字节序转换）
   - socket()/bind()/listen()/accept()/connect()/send()/recv()/close_socket()函数

**验证结果**:
- socket-test 8项测试全部PASSED:
  - Test 1: socket创建 - PASSED (fd=0)
  - Test 2: bind - PASSED
  - Test 3: listen - PASSED
  - Test 4: connect - PASSED (loopback)
  - Test 5: accept - PASSED (fd=2, 服务器端连接socket)
  - Test 6: send - PASSED (14字节)
  - Test 7: recv - PASSED (收到"Hello, Socket!"，数据匹配)
  - Test 8: 双向通信 - PASSED (服务器回复正确接收)

**修改文件**:
- kernel/include/socket.h: 新增is_server_side和listen_fd字段
- kernel/net/socket.c: 重写connect和accept逻辑
- kernel/arch/x86/sys_net.c: 新增sys_socketcall多路复用
- kernel/arch/x86/syscall.c: 注册sys_socketcall
- kernel/include/syscall.h: 新增SYS_SOCKETCALL=208
- kernel/compat/linux_abi.c: 新增{102, SYS_SOCKETCALL}映射
- user/rust/hepu-sys/src/lib.rs: 新增BSD Socket API模块
- user/rust/socket-test/: 新建测试程序（第10个Rust程序）
- kernel/sched/shell.c: socket-test加入自动测试
- Makefile/Cargo.toml: 集成构建

**经验教训**:
- loopback TCP模拟需要正确的对端关联：客户端↔服务器端连接socket（双向peer_fd）
- accept应返回connect时创建的服务器端连接socket，而非创建新socket
- 服务器端连接socket需要通过listen_fd字段与监听socket关联
- BSD Socket API通过Linux socketcall(102)多路复用实现，兼容标准POSIX接口

---

## BUG-23: 调度器子进程延迟调度（已修复）

**状态**: ✅ 已修复 (V6.70)

**现象**:
- file-server-e2e-test中，父进程fork后等待服务注册，循环调用service_lookup+yield_cpu
- 子进程在父进程等待超时（3-4次"still waiting"）后才被调度
- fork-test（低PID）正常，file-server-e2e-test（高PID）异常

**根因**:
- **Linux ABI映射表缺少sched_yield(158)的映射**
- Rust用户态yield_cpu()使用Linux系统调用号158（SchedYield=158）
- 内核linux_abi.c的映射表中没有{158, SYS_YIELD}条目
- 导致yield_cpu()的系统调用号158未被翻译，内核无法识别，实际上没有触发调度
- 父进程的yield_cpu()是空操作，子进程只能等待时钟中断（时间片用完）才被调度

**修复方案** (V6.70):
- 在kernel/compat/linux_abi.c的映射表中添加：`{158, SYS_YIELD}`
- SYS_YIELD=6（赫普OS内部系统调用号）

**验证结果**:
- 修复前：父进程输出3-4次"still waiting..."后子进程才运行
- 修复后：父进程第一次yield后子进程立即运行，注册服务，父进程立即找到服务
- file-server-e2e-test端到端通信正常（服务发现+IPC请求/响应）

**修改文件**:
- kernel/compat/linux_abi.c: 添加{158, SYS_YIELD}映射

**经验教训**:
- Rust用户态使用Linux系统调用号，必须确保所有用到的调用号都在Linux ABI映射表中有对应映射
- 新增系统调用封装时，要同时检查内核侧和ABI映射侧
- yield类系统调用如果失效，表现为"调度器不工作"，容易误判为调度器算法问题

---

