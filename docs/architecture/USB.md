---
title: USB 与无线输入支持（V0.13.62）
status: active
sys_version: 0.56.2
updated: 2026-11-11
owner: kernel
source_of_truth: kernel/arch/x86_64/usb64.c
supersedes: null
---

# USB 与无线输入支持

## 现状（V0.13.62）

- 主机控制器（64 位）：USB 核心 `usb64.c` + **EHCI（USB 2.0）`ehci64.c`** + **XHCI（USB 3.0）`xhci64.c`**（Makefile:102-104）；`usb64_init()` 先试 EHCI，失败再试 XHCI（`usb64.c:347/357-358`）。UHCI（USB 1.1）仅存于 32 位遗留树 `kernel/drivers/usb/uhci.c`，未编入 64 位构建。
- 设备类支持：
  - **HID 输入**（鼠标/键盘/绝对定位平板）
  - **Mass Storage（BOT 批量）**——U 盘挂载/读写
  - **HUB 级联**——根端口下游设备经 hub 枚举
- 热插拔检测：每 ~500ms 检查端口连接变化，新设备自动枚举。

## 无线鼠标/键盘（2.4GHz 接收器）支持原理

市面无线鼠标（罗技、雷柏等）的 **2.4GHz 接收器在 USB 侧就是一个标准 HID 设备**——
可能是**复合设备**：一个物理接收器含多个 HID 接口（键盘+鼠标+多媒体键）。
系统按以下方式支持：

1. **复合接口枚举**（V0.13.62）：解析配置描述符时收集**所有** HID 接口
   （每接口：协议 1=键盘/2=鼠标/0=绝对定位 + 中断 IN 端点），逐一注册，
   不再只认最后一个接口。
2. **多设备**：最多 4 只鼠标 + 2 块平板 + 1 键盘同时在线；每设备独立
   QH+TD，水平串在周期调度链上，每帧轮询全部中断端点（控制器对
   NAK 自动跳过）。
3. **报告解析增强**：报告缓冲 16 字节；标准 3 按钮+滚轮（4 字节），
   5 字节扩展支持侧键（报告 byte4 低 2 位 → 按钮 4/5）。
4. **输入合流**（hsinput）：tablet 绝对定位优先 → USB 相对鼠标次之 →
   PS/2 兜底；按钮多源合并（任一来源按下即生效）；滚轮消费式累积。

## 指针渲染（V0.13.62 修复）

桌面此前从未绘制鼠标指针（gfx64_draw_mouse_cursor 已实现但无调用点）。
现于 hepushell_render 每帧绘制（箭头 + 黑色描边 + 边缘 AA）。

## 已知限制

| 项 | 说明 | 落点 |
|----|------|------|
| USB 2.0/3.0 高速设备 | EHCI/XHCI 驱动已编入并在 `usb64_init()` 中回退尝试（`ehci64.c`/`xhci64.c`，Makefile:103-104）；QEMU 异步列表不执行，真机行为待实机验证 | `kernel/arch/x86_64/ehci64.c`、`kernel/arch/x86_64/xhci64.c` |
| 蓝牙输入 | 蓝牙鼠标需 USB BT 适配器 + HCI/L2CAP/HID-over-GATT 协议栈（独立大工程） | ROADMAP P0-5 |
| 键盘单槽 | 复合接收器第二键盘接口被跳过（USB_MAX_KBDS 未扩） | usb64.c |

## 验证方法

QEMU：`-device piix3-usb-uhci -device usb-mouse -device usb-kbd -device usb-tablet`
→ 日志应出现三设备 ready；QMP `input-send-event`（abs/rel/btn）驱动指针移动
→ screendump 对比指针位置。
