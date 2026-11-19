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
2. **crashlog 不落盘**：只写 klog 环形内存缓冲，重启即清，所以"开机显示上次异常"
   没有持久化基础。崩溃上下文直接做 ext2 IO 会 GPF halt（代码注释已说明），
   需要独立设计（例如崩溃时只写内存、下次启动由正常路径落盘）。
3. **32 位没有对应的自检入口**：32 位从 `-kernel` multiboot 启动，根文件系统
   走 initrd，标记文件要么改 initrd 生成、要么改镜像引导，成本高于收益，
   暂缓。
4. **`bsod` 命令的"敲键盘"那一跳未验**：命令实现体、执行链路都验过，
   但 shell 输入到 `strcmp` 命中这一跳受 QEMU 串口注入限制，未成。

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
