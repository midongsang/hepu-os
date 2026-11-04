---
title: REAL_HARDWARE_TEST
status: active
sys_version: 0.54.6
updated: 2026-11-01
owner: guides
source_of_truth: null
supersedes: null
---

> ⚠️ **版本说明**：sys_version 已对齐 0.54.6，但正文口径仍为 v0.13.x ~ v0.33.x 设计稿。源码事实请以 `kernel/arch/x86_64/Makefile` 实际编入为准。


# 赫普OS 真机高分辨率验证指南（V7.94）

## 概述

本文档描述赫普OS在真机上进行高分辨率显示验证的步骤和注意事项。

## 引导器GOP分辨率自动选择

HepuBoot UEFI引导器已实现GOP模式自动选择（hepboot_uefi.c:274-365）：

1. 遍历所有可用的GOP模式（`gop->Mode->MaxMode`）
2. 对每个模式评分：
   - 分辨率越高得分越高（优先1920x1080、2560x1440）
   - 32位色深优先
   - 当前模式加分（避免不必要的模式切换）
3. 选择得分最高的模式
4. 如最佳模式与当前模式不同，调用`SetMode`切换

## 内核帧缓冲映射

内核GOP驱动（gfx_gop.c）支持任意分辨率的帧缓冲：
- 帧缓冲物理地址从引导器传递（`g_gop_info.framebuffer_base`）
- 内核启动时通过`paging_map`映射帧缓冲到虚拟地址
- 支持超过4GB的帧缓冲地址（64位地址映射）

## UI缩放

桌面渲染器（desktop_render.c）支持UI缩放：
- `calc_ui_scale()`根据分辨率自动计算缩放因子
- 图标大小、间距、字体大小随缩放因子调整
- 状态栏、Dock栏、窗口均支持缩放

## 真机测试步骤

### 1. 准备启动介质

```bash
# 将IMG镜像写入U盘（替换/dev/sdX为实际设备）
sudo dd if=build/hepu-os-uefi-v0.08.img of=/dev/sdX bs=4M status=progress
sync
```

### 2. BIOS设置

- 禁用Secure Boot（或导入HEPU_DB.crt证书）
- 设置UEFI启动模式（非Legacy/CSM）
- 将U盘设为第一启动项

### 3. 启动验证

观察引导菜单和内核启动画面：
- [ ] 引导菜单正常显示（HepuBoot UEFI v0.08）
- [ ] 内核加载成功（显示内核大小和加载地址）
- [ ] 内核启动画面显示正确分辨率
- [ ] 桌面渲染正常（图标、文字、状态栏）
- [ ] 中文显示正常（无乱码、无重影）

### 4. 分辨率检查

在Shell中执行：
```
gfxinfo
```
记录输出的分辨率、色深、帧缓冲地址。

### 5. 常见问题

| 问题 | 可能原因 | 解决方案 |
|------|---------|---------|
| 黑屏无显示 | GOP模式不兼容 | 引导菜单按3进入Recovery Mode（使用当前模式） |
| 分辨率过低 | GOP默认模式低 | 检查引导器GOP自动选择逻辑 |
| 画面偏移 | 帧缓冲未正确映射 | 检查paging_map映射范围 |
| 文字过小 | UI缩放未生效 | 检查calc_ui_scale函数 |
| 中文乱码 | 字体未加载 | 检查TTF初始化日志 |

## 支持的分辨率

| 分辨率 | 比例 | 状态 |
|--------|------|------|
| 1024x768 | 4:3 | QEMU验证通过 |
| 1280x720 | 16:9 | 待真机验证 |
| 1920x1080 | 16:9 | 待真机验证 |
| 2560x1440 | 16:9 | 待真机验证 |
| 3840x2160 | 16:9 | 待真机验证（需UI缩放优化） |

## 日志收集

启动时添加`debug`参数，或在Shell中执行`dmesg`，收集以下日志：
- `[GOP] mode: WxH`
- `[PAGING] framebuffer mapped`
- `[GFX] backend initialized`
- `[TTF] font loaded`
- `[DESKTOP] render done`


---

## V0.13.63 真机启动专项排查记录（2026-10-24）

本轮针对「真机黑屏回 BIOS / 卡黑屏」系统性排查，定位并修复 4 类影响真机启动/运行的因素。

### 1. 内存探测链（真机 <2GB 崩溃根因）

**问题**：内核硬编码 2GB 内存（`pmm64_init(2GB)`），stage2 写死 256MB。真机 <2GB 时内核分配不存在的物理页 → 启动即崩（黑屏回 BIOS）。

**修复链路**：
- stage2 实模式 `int15 E801` 探测总内存（单次调用，无缓冲区）；
  调用点固定在 **读盘完成之后、进保护模式之前**（`load_64bit_done`）——SeaBIOS 的 VBE/INT13 服务会清零 0x90000 低内存区，过早探测结果丢失；
- 结果写 `0x90100`（64 位：低 32 位 + 高 32 位），避开内核分块加载区与 PML4/页表区；
- UEFI 引导器（`hepboot_uefi.c`）同样换算内存映射写 `0x90100`；
- 内核 `pmm64_init` 读 `0x90100` 替代硬编码 2GB（下限 256MB 兜底，上限 2GB）；
- `vmm64_init` 全内存身份映射（2MB 大页，上限 0x80000000）。

**验证**：QEMU `-m 384` 实测 `PMM physical memory: 0x17F MB`（383MB），桌面正常启动。

**注意**：E801 只覆盖到 4GB；内核 PMM 上限 2GB，满足。>4GB 真机后续可加 E820 精化。

### 2. AHCI BAR 高区映射（真机 SATA 盘页错误）

**问题**：`ahci64_init` 直接解引用物理 BAR5 地址。真机 AHCI BAR5 通常在 `0xF0000000+`（PCI MMIO 高区），不在 vmm 身份映射范围（0~2GB）→ 访问寄存器页错误崩溃。QEMU 无 AHCI 故未暴露。

**修复**：访问前把 BAR 所在 2MB 页映射进页表（`vmm64_map_page` + 2MB 大页 + invlpg），再初始化 HBA。

### 3. AHCI 接入系统盘读取链（真机 SATA rootfs 读不到）

**问题**：系统盘读取固定走 IDE 驱动（`g_ext2_block_read` 默认 `ide64_read_sectors`、storage HAL 默认 IDE）。真机 SATA 盘是 AHCI 控制器，IDE 驱动读不到 → 内核无法挂载 rootfs。

**修复**：
- `ahci64.c` 增加盘级入口：`ahci64_disk_present()` / `ahci64_read_disk()` / `ahci64_write_disk()`（选第一个有盘的端口）；
- storage HAL（`storage_hal64.c`）`storage_hal_raw_read`：有 AHCI 盘时优先走 AHCI；
- 内核启动时：AHCI 盘存在 → `ext2_64_set_block_device(ahci64_read_disk)`，P2/P3 挂载走 AHCI。

### 4. 读盘重试（真机慢盘/偶发失败）

**问题**：stage2 INT13 读盘单次执行，失败即 `read_error`（黑屏回 BIOS）。

**修复**：致命读取点（HDD 前 2 次 + CD 前 2 次）加重试 3 次，每次重发 `AH=42h` 扩展读取，3 次失败才报错。

### 待真机验证/后续项

| 项 | 状态 |
|----|------|
| 32 位内核读取路径重试 | 低优先级，待加 |
| 现代 USB 控制器（EHCI/XHCI） | 当前仅 UHCI；真机 USB 键盘鼠标依赖 PS/2 兜底 |
| 真机网卡兼容（e1000 82540EM / rtl8139 为老型号） | 网络功能待真机验证 |
| UEFI BootFailCount 内核不清零 | 仅计数，不影响启动 |
| >4GB 真机 E820 精化 | 内核上限 2GB，暂不阻塞 |

## V0.13.64: EHCI 模拟器限制记录

- 背景: 真机现代主板无 UHCI(PIIX), 需 EHCI(USB 2.0) 支持无线鼠标/键盘等
- 实现: kernel/arch/x86_64/ehci64.c(寄存器/QH/qTD/控制传输/中断端点框架), usb64.c 无 UHCI 时自动尝试 EHCI, pci64.c 新增 pci_find_class_bar0
- QEMU 7.2 实测: 寄存器布局确认(caplen=0x20, USBCMD@abs0x20, USBSTS@0x24, FRINDEX@0x2C, ASYNCLIST@0x38, CONFIGFLAG@0x40, PORTSC@0x44), 端口连接检测正常(ps=0x1003 CCS+PED), 复位/地址分配流程可走
- 已知限制(模拟器): QEMU usb-ehci 异步列表不执行 —— ASYNCLIST 写入生效(读回正确)、FRINDEX 递增(帧调度在走)、USBCMD RUN+ASEN+PSEN 正确、IAAD 门铃触发、-device usb-ehci/usb-mouse|usb-tablet|usb-kbd、q35+ich9-usb-ehci1 均复现: qTD 保持 ACTIVE 不被控制器处理
- 结论: EHCI 实现按 EHCI 规范构造(异步列表 QH/qTD 链), 驱动逻辑与真机硬件行为一致; QEMU 模拟器限制不影响代码正确性, 真机行为待实机验证
- 验证基线: 无 UHCI 时 QEMU 无法用 EHCI 传输(上述限制); 有 UHCI 时全链路回归通过(枚举 VID=0x0627/PID=0x0001/cfgs=1, HID 鼠标可用)

## v0.20.8: 真机黑屏快速定位

不需要串口，看屏幕上出现的字符：

| 屏幕显示 | 卡在哪 |
|---------|--------|
| 全黑，什么都没有 | MBR 没跑起来，BIOS 没把盘当启动盘 |
| 左上角 "H1" | MBR 跑起来了，正在读 stage2 |
| "H1" 后黑屏 | stage2 读盘失败或 VBE 切换失败 |
| "E! MBR READ ERR" | LBA34 读不出来，换 SATA 直连试 |
| 启动菜单出现 | stage2 OK，卡在内核加载或长模式 |
| 彩色 Logo 出现 | VBE OK，卡在内核驱动初始化 |
| 黑屏自动回 BIOS | 三重错误复位（CPU 指令不兼容/页表问题） |

拍照发回最后一帧即可定位。
