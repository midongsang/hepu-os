---
title: 0.63.0 展示页实拍与任务管理器复测
status: active
sys_version: 0.63.0
updated: 2026-11-27
owner: status
source_of_truth: null
supersedes: null
---

# 0.63.0 展示页实拍与任务管理器复测

本目录记录 README 展示图对应的实拍来源，以及 0.63.0 收口时发现并修掉的
e1000 MMIO 跨地址空间缺映射问题。

## 构建与截图

- 构建：`HEPU_EV=/tmp/showcase bash tools/regress/build_all.sh`，清理 64/32 位
  构建目录后全量重编，避免 `version.h` 改动未触发旧对象重编。
- 64 位镜像：`/tmp/showcase/build/hepu.img`，268,435,456 B。
- 64 位内核：`build_x86_64/hepu-kernel64.bin`，1,023,696 B；32 位内核
  `build_v250/hepu-kernel.bin`，3,864,141 B。
- 启动链路：`HEPU_EV=/tmp/showcase bash tools/regress/boot64.sh`，菜单/锁屏/桌面
  实拍；加载屏由按键后立即 `screendump` 的补充脚本抓取（`boot64.sh` 的
  `loading_*` 帧在 TCG 下会落进锁屏，不能当加载屏证据）。
- 展示图统一用 **100% UI 缩放**（默认档）。构建夹具注入的 150% 仅用于回归取证；
  150% 下锁屏大时钟会左偏，因此展示镜像把 `/display.conf` 改为 `scale=100` 后重拍，
  不涉及代码改动。
- 任务管理器：桌面起后发送 `Win+M`，`hs_taskmgr_create()` 创建窗口并截图；
  `taskmgr-serial-tail.txt` 为本次串口尾部，无异常报告。

## 展示资产

- `docs/assets/boot-menu-0.63.0.jpg`
- `docs/assets/boot-loading-0.63.0.jpg`
- `docs/assets/lock-screen-0.63.0.jpg`
- `docs/assets/desktop-64-0.63.0.jpg`
- `docs/assets/taskmgr-processes-0.63.0.jpg`

## e1000 MMIO 修复

现象：`Win+M` 打开任务管理器时 64 位内核 #PF，RIP 落在 e1000 状态读取，
`CR2=0x81080008`，页表项 `PTe=0`。原因是 e1000 BAR0 只在启动 CR3 里做了恒等
映射，切到用户进程地址空间后该映射不在。

处理：`kernel/arch/x86_64/e1000_64.c` 保存 MMIO 物理基址与最后映射的 CR3；
`e1000_read/e1000_write` 前检查当前 CR3，变化时补 0x20000 字节页级恒等映射。
修复后同一 `Win+M` 路径能正常打开任务管理器，状态栏显示 `Hepu OS v0.63.0`。

## 边界

32 位 0.63.0 首帧仍只覆盖顶部约 410 行（OBS-06，本版不修）。README 的 32 位
展示图保留 0.60.7 基线，并在图下明确标注，不用残屏图冒充 0.63.0。
