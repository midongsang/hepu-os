# Hepu OS 诊断体系（2026-11-19）

崩溃现场原本只能靠串口，屏幕在图形模式下是空的；诊断程序也没有自证手段。
这一轮把"能看"补齐成"两条通道 + 三个自证入口"。

## 一、崩溃时：两条通道

| 通道 | 内容 | 谁在用 |
|---|---|---|
| 串口 | STOP 码、异常名、诊断提示、CR2 解码、rbp 回溯、raw stack | 事后分析、自动化回归 |
| 屏幕 | 深蓝底白字：STOP / Exception / RIP / CR2 / 停机提示 | 现场的人 |

- 32 位：`kernel/arch/x86/bsod.c` 的 `bsod_show()`，先判 gfx 可用则
  `bsod_gfx_draw()`，否则退回文本模式；LFB 由 `bochs_dispi_bar0()` 现读
  PCI BAR0 保证映射（`.bss` 里缓存的值会被清掉，这是曾经"看不见"的根因）。
- 64 位：`kernel/arch/x86_64/lib64.c` 的 `hepu_panic_kernel_fault()`，
  末尾调 `bsod64_gfx_report()`。它**不经过 gfx 状态机**（崩溃时未必完好），
  直接取 front_buffer 逐像素填充，并先做尺寸合理性检查，不合理就退回只走串口。

## 二、报告字段（两端统一）

```
=== HEPU OS - DIAGNOSTIC REPORT (NN-bit kernel) ===
STOP: 0x0000000E            <- 8 位定长，可检索
Exception: Page Fault       <- 32 向量英文名表
Hint: page fault: ...       <- 每个异常一句诊断提示
vector / error / rip / cs / rflags / rsp
CR2（仅 #PF）+ 解码（not present / protection violation / read / write）
Stack trace（32 位近似扫描；64 位 rbp 链 + raw stack 兜底）
```

`STOP` 码定长是刻意的：不定长会导致日志检索错位，自检里专门有一项查它。

## 三、三个自证入口

| 入口 | 触发方式 | 会停机？ | 验什么 |
|---|---|---|---|
| `/diag_selftest` | 根分区存在该文件 | 否 | 串口通道 / 图形表面 / 异常名表 32/32 / STOP 码格式 |
| `/bsod_selftest` | 根分区存在该文件 | 是（真崩溃路径） | 整条崩溃链：从触发到完整报告 |
| `bsod` 命令 | shell 里敲 | 是 | 同上，交互式 |

前两个入口的意义：**无头环境也能验证诊断链路**。之前串口注入时机窗口只有
一两秒、桌面终端又要 GUI 交互，两条路都不适合自动化。

用法（64 位）：

```bash
bash tools/make_single_img64.sh
echo 1 > /tmp/marker
debugfs -w -R "write /tmp/marker /diag_selftest" /tmp/hepu64_build/rootfs.ext2
dd if=/tmp/hepu64_build/rootfs.ext2 of=/tmp/hepu64_single.img bs=512 seek=13000 conv=notrunc
qemu-system-x86_64 -drive file=/tmp/hepu64_single.img,format=raw,if=ide -m 512 \
  -display none -vga std -serial file:/tmp/ser.log
```

正常镜像里没有这两个标记文件，不会触发。

## 四、仍然没有的

1. **OBS-08 写入者未指名**：`vfs_node_sane()` 拦野指针、`magic` 拦被覆盖的节点，
   但都只能报"哪个节点坏了"，报不出"谁写坏的"。TCG 下数据断点（DR0/DR7）
   已确认零命中且触发 watchdog，这条路在纯软件模拟里走不通。
2. **crashlog 不落盘 → 已用 CMOS 摘要兜住**（提交 `9352803`）：崩溃时把
   STOP/RIP/CR2 写进 CMOS NVRAM（纯端口 IO，不碰磁盘），下次启动读回来打
   `[CRASH-LAST]` 横幅并清掉。完整堆栈仍只在串口/屏幕，跨重启保留的只是摘要。
   踩过一个坑：最初放在 CMOS 0x30，实测 0x31 是 BIOS 的"扩展内存大小"字节，
   reset 时被重写（0x41→0x17），改到 0x50 起的空闲区后正常。

## 五、相关提交

```
a5979ce feat(diag): 开机诊断自检套件（/diag_selftest）
e77fbfc feat(diag): /bsod_selftest 自动自测入口
461dff6 fix(uart64): 串口 shell 旁路从不触发的根因
6d7772d feat(diag): 64 位崩溃屏幕上可见（图形蓝屏）
34ce591 feat(diag): 64 位 shell 增加 bsod 命令
380c9c1 feat(vfs): OBS-08 升级 —— vnode 加自证魔数
69ec0c3 feat(diag): 64 位内核接入统一的 DIAGNOSTIC REPORT 格式
8b688e7 feat(diag): 蓝屏图形版面（3 倍字、1920x1080 可读）
da813a4 fix(gfx): 32 位 LFB 地址被 .bss 清零导致从未映射
```

## 附：32 位磁盘级验证路径（2026-11-19）

32 位是 multiboot 内核，`stage2` 只引导 64 位，镜像启动没有 32 位入口 ——
这是 readdir 行为、32 位自检、`bsod` 交互三处验证卡住的**同一个原因**。实测找到替代路径：

```
qemu-system-i386 -kernel build_v250/kernel.elf -drive file=<带 ext2 的盘>
```

串口输出 `[FS] ext2 filesystem found on IDE drive partition LBA=0x32C8`
+ `[EXT2] selftest: mounted`，ext2 正常挂载、桌面正常起来。

用它跑通了 32 位 readdir 的镜像级验证（提交 `4f42795` 的行为证据）：

```
[RD-TEST] root entries=14  first=lost+found
```

（临时探针已在提交前移除，工作树干净；`regress32` 复跑 `fault=0 lines=869` PASS）

## 附二：32 位诊断实现现状（2026-11-19 更新）

| 能力 | 32 位入口 | 状态 |
|---|---|---|
| 自检套件 | `diag_selftest32()`（`bsod.c`），5 项 | 前 4 项实测全 PASS（`3ac31f4`）；第 5 项代码就位未取证 |
| 崩溃触发 | 根分区 `/bsod_selftest` | 实测可用（`4ae5117`，串口打出完整 DIAGNOSTIC） |
| 崩溃摘要 | `crash_nvram_save/load/clear`（CMOS 0x50） | 写入路径已执行；跨重启横幅未取证 |
| 编解码自检 | `crash_nvram_selftest()`（diag 第 5 项） | 代码就位；镜像级未取到 |

**32 位标记注入**（不需要 stage2，multiboot 直接带盘）：

```bash
qemu-system-i386 -kernel build_v250/kernel.elf -m 256 -display none \
  -drive file=<带 ext2 的盘> -serial file:/tmp/ser32.log
```

盘要求：MBR 签名 `0x55AA` + 一个 `0x83` 分区。实测分区起始 LBA 2048 可被识别
（`[FS] ext2 filesystem found ... LBA=0x800`），但同一套盘在不同构建里会随机
出现 `[EXT2] selftest: mount FAIL`，这个不稳定点本身值得单独查。

注入：

```bash
mke2fs -t ext2 -F /tmp/part.img
echo 1 > /tmp/marker
debugfs -w -R "write /tmp/marker /diag_selftest" /tmp/part.img
```

**两条未取证的验证**（代码与 64 位同构，64 位侧已验证）：

1. CMOS 跨重启横幅：`system_reset` 在无头 QEMU 下不可靠，第二次启动不产日志
2. 编解码自检第 5 项：验证过程中 QEMU 进程被 SIGKILL、自造 ext2 盘 mount 不稳

**新登记的疑点**：`[EXT2] selftest: mount FAIL` 与 `[SELFTEST] /bsod_selftest found`
在同一日志里同时出现 —— mount 报失败但文件又能读到，说明 `ide_ext2` 的
挂载状态判断和实际可用性不一致，值得单独排查。
