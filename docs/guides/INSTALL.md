---
title: INSTALL
status: active
sys_version: 0.55.0
updated: 2026-11-01
owner: guides
source_of_truth: null
supersedes: null
---

> ⚠️ **版本说明**：sys_version 已对齐 0.54.6，但正文口径仍为 v0.13.x ~ v0.33.x 设计稿。源码事实请以 `kernel/arch/x86_64/Makefile` 实际编入为准。


# 赫普OS 安装与试用指南 (V3.82)

本指南说明两种使用方式：
- **A. U盘试用**：一块 U 盘写入 combo 镜像，即插即用（不碰电脑硬盘）
- **B. 安装到硬盘**：像安装 Linux 发行版一样，把系统装进电脑硬盘

> 数据盘说明：combo 镜像自带一个 ext2 数据分区（`/data`）。
> 系统与数据在**同一块盘**上，不需要第二块 U 盘。

---

## A. U盘试用（Live 模式）

### 1. 准备
- 一块 U 盘（建议 ≥ 16MB；镜像约 9MB）
- 镜像文件：`hepu-os-v1.02-combo.img`
- 写入工具任选其一：**Rufus / balenaEtcher / Ventoy / dd**

### 2. 写入 U 盘（三选一）

**Windows + Rufus（推荐）**
1. 打开 Rufus，设备选择你的 U 盘
2. 引导类型选择：**镜像文件**，选中 `hepu-os-v1.02-combo.img`
3. 分区类型：**MBR**；目标系统：**BIOS 或 UEFI-CSM**
4. 点"开始"，弹出提示选 **DD 镜像方式写入**（ISO 方式可能不被识别）
5. 等待完成，拔出 U 盘

**Windows + balenaEtcher**
1. 打开 Etcher，选择镜像文件 `hepu-os-v1.02-combo.img`
2. 选择目标 U 盘，点 Flash，等待完成

**Linux/macOS + dd**
```bash
sudo dd if=hepu-os-v1.02-combo.img of=/dev/sdX bs=4M status=progress conv=fsync
```
> 把 `/dev/sdX` 换成你的 U 盘设备名（用 `lsblk` 确认，切勿写错系统盘）。

### 3. 从 U 盘启动
1. 插上 U 盘，开机立即按 **F12**（戴尔）或 F2/F10/F11/Esc（其他品牌）进入引导菜单
2. 选择 **U 盘（USB HDD / UEFI: U盘名）** 启动
3. 屏幕依次出现引导日志（`H1 → G0/G1/G2/G3 → P0/P1/P2`），随后进入赫普OS

> 试用不写电脑硬盘任何数据，数据都落在 U 盘的数据分区里。

---

## B. 安装到硬盘

### 1. 准备
- combo 镜像 + `install.sh`（放同一目录）
- 目标电脑硬盘（将被整盘覆盖，请提前备份数据）

### 2. 执行安装
```bash
# 在 Linux 环境(或从赫普OS U盘试用启动后的终端)执行:
sudo bash install.sh /dev/sdX
```
把 `/dev/sdX` 换成目标硬盘。脚本有三重确认（拒绝系统盘/两次盘符核对/键入 YES）。

### 3. 从硬盘启动
1. 重启，进入 BIOS 引导菜单，选择目标硬盘
2. 引导日志后进入系统，数据分区 `/data` 自动挂载

---

## 引导日志对照（诊断用）

| 标记 | 含义 |
|------|------|
| `H1` | MBR 引导桩已执行（自复制到 0x8000 后读 stage2） |
| `G0` | stage2 开始（设文本模式/清屏） |
| `G1` | 读取内核到 0x10000 完成（V3.82 起缓冲下移至 0x10000，避开 0xC0000-0xEFFFF ISA 空洞） |
| `G2` | E820 内存映射收集完成 |
| `G3` | A20 开启，准备进保护模式 |
| `P0/P1/P2` | 保护模式切换/跳转内核 |
| 内核 `[FS] MBR: ext2 data part LBA=...` | 数据分区挂载成功 |

> 引导日志是正式特性（替代蜂鸣器诊断），社区可在无蜂鸣器环境下排查引导问题。

---

## 常见问题

| 现象 | 处理 |
|------|------|
| 写入后 U 盘在 Windows 不显示盘符 | 正常。combo 是整盘镜像，分区表为 Linux 格式，Windows 不识别；用 lsblk/磁盘管理可见 |
| F12 里没有 U 盘项 | BIOS 开启 Legacy/CSM 启动；或换一个 USB 口 |
| 引导日志闪一下即黑屏 | 重新用 **DD 模式**（Rufus）或 Etcher 写入，确认镜像完整 |
| 想回到 U 盘正常使用 | 用 Rufus/磁盘管理把 U 盘重新分区格式化即可 |

---

## C. 开发环境验证（QEMU，社区开发用）

无需真机，在 Linux 下用 QEMU 即可完整验证启动链与 Shell 交互。

```bash
# 1. 启动（后台，串口日志落盘）
LD_LIBRARY_PATH=tools/qemu/x/usr/lib/x86_64-linux-gnu:tools/qemu/x/usr/lib:tools/qemu/x/lib/x86_64-linux-gnu \
  tools/qemu/x/usr/bin/qemu-system-x86_64 \
  -L tools/qemu/x/usr/share/qemu -m 256M -nic none -vga std -display none \
  -drive file=build/hepu-os-v1.02.img,format=raw,if=ide,cache=writethrough \
  -boot order=c -monitor unix:/tmp/hmon.sock,server,nowait \
  -serial file:/tmp/hserial.log &

# 2. 主判据：串口文本（约 55-60s 后出现提示符）
tail -f /tmp/hserial.log    # 看到 [SHELL] main started 与 hepu@os:~$ 即启动成功

# 3. 键盘注入 / 截图（QEMU monitor）
printf 'sendkey g\n' | timeout 2 nc -U /tmp/hmon.sock   # 键入 g
printf 'screendump /tmp/t.png\n' | timeout 4 nc -U /tmp/hmon.sock
```

> **图形命令与重启（V3.82 实测确认）**：
> - 图形命令（`gfxtest`/`gfx`）退出后会自动复位重启以重载干净字体——QEMU 实测完整闭环通过（演示→自动重启→回到 Shell 提示符）。
> - 图形模式（0x13）会污染 VGA 字体平面（plane 2），QEMU std VGA 下内核内字体恢复读回恒 0x0F（环境限制）；字体恢复代码已保留，真机可生效；QEMU 下由自动重启兜底。
> - **V3.82 根治二次启动卡死**：IDE 启动自检原写 LBA1（stage2 区）会污染引导器，现改写到磁盘末尾扇区——同一镜像可反复启动不再损坏。
