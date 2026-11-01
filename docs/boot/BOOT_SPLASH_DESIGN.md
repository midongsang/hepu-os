# 赫普OS 启动画面系统设计文档

## 版本
- v1.0 (2026-09-24): 初始设计，方案A（开发调试版）+ 方案B（成品简约版）

## 设计理念
先用方案A保证开发调试能力，稳定后切换方案B提供干净的用户体验。两阶段共享同一套绘图库和日志系统，避免重复开发。

---

## 阶段1：方案A（开发调试版）— 当前阶段

### 目标
优先保证调试、快速定位崩溃/启动故障，安全校验可视化。

### HepuBoot 画面
- 背景色：`#0A0A12`（深蓝黑）
- 顶部文字：`HepuBoot` / `Hepu OS Bootloader`
- 中间：`Loading kernel...`
- 右下角：进度百分比（0%→100%）
- 加载完成后输出：
  ```
  Hash: 0xXXXXXXXXXXXX
  Verified OK
  ```
- 校验失败：文字变红，停止启动，显示错误码

### 内核启动画面
- 沿用深色背景（不切换文本模式）
- 左上角滚动内核初始化日志：
  ```
  HepuOS Kernel v0.1
  [INIT] PM: Memory manager initialized
  [INIT] IDT: Interrupt table setup
  [INIT] PROC: Scheduler ready
  [INIT] FS: Root mount success
  [INIT] VBE: Framebuffer ready
  System ready. Starting shell...
  ```
- 日志末尾输出启动总耗时
- 然后直接进入文本shell

### 方案A开发优势
1. 内核panic、初始化报错直接打印在屏幕，不用串口抓日志
2. 引导哈希直接显示，安全篡改测试一眼发现
3. 代码量小，几乎不增加启动耗时
4. 启动耗时统计直接输出

---

## 切换到方案B的触发条件（满足再做）
1. 内核、HepuBoot基础功能稳定，不再频繁panic
2. 本地安全测试全部跑完，引导哈希校验、内存保护机制稳定
3. VBE帧缓冲、点阵字体绘制函数已封装成独立工具函数

---

## 阶段2：方案B（成品简约版）— 稳定后

### HepuBoot 画面
- 深色背景
- 顶部细蓝色进度条
- 中间ASCII艺术LOGO
- 下方文字：`Booting Hepu OS ...`
- 进度条随内核读取前进
- 校验失败：进度条变红，提示完整性错误

### 内核初始化画面
- 不再全屏滚动日志
- 居中简短状态提示：
  ```
  Initializing memory... OK
  Initializing processes... OK
  Mount root filesystem... OK
  ```
- 内核日志**默认隐藏**
- 快捷键`F12`：切换显示/隐藏完整内核日志（复用方案A的日志输出代码）
- 全部初始化完成，画面平滑过渡，进入shell

### 设计取舍
- 正常用户：干净启动画面
- 开发者/排错：F12一键调出调试日志，保留方案A的调试能力

---

## 代码复用规划

### 1. 绘图库（方案A时一次性实现）
文件：`kernel/arch/x86/boot_gfx.c` + `kernel/include/boot_gfx.h`
- `boot_gfx_init()`: 初始化帧缓冲
- `boot_fill_rect(x, y, w, h, color)`: 填充矩形
- `boot_draw_char(x, y, c, color)`: 绘制字符（8x16点阵）
- `boot_draw_string(x, y, s, color)`: 绘制字符串
- `boot_progress_bar(x, y, w, h, percent, color)`: 进度条
- `boot_clear(color)`: 清屏

HepuBoot侧：`tools/hepboot/uefi/hepboot_gop.c`（UEFI GOP版本，接口一致）

### 2. 日志系统（两套模式）
- `debug`模式（方案A）：全屏滚动日志
- `quiet`模式（方案B）：居中状态提示，F12切换
- 共用同一套日志缓冲区，只是显示方式不同

### 3. 哈希校验
- HepuBoot计算内核CRC32，与内置期望值比对
- 方案A：直接显示哈希值
- 方案B：哈希值放到F12调试面板，不删除校验

### 4. 启动耗时统计
- 记录各阶段耗时（HepuBoot→内核初始化→shell）
- 方案A：日志末尾输出
- 方案B：F12调试面板显示

---

## 开发优先级

### P0（方案A基础）
1. 内核侧boot_gfx绘图库（fill_rect/draw_char/draw_string）
2. HepuBoot侧GOP绘图库（接口对齐）
3. 内核启动画面（深色背景+滚动日志）
4. 启动耗时统计

### P1（方案A安全）
5. 内核CRC32哈希计算
6. HepuBoot哈希校验+显示
7. 校验失败处理（红字+停止启动）

### P2（方案B，稳定后）
8. 安静模式（隐藏日志+居中状态）
9. F12切换调试日志
10. ASCII艺术LOGO
11. 平滑过渡效果

---

## 技术细节

### 帧缓冲信息传递
HepuBoot通过Multiboot信息结构传递帧缓冲：
- `framebuffer_addr`: 物理地址
- `framebuffer_pitch`: 每行字节数
- `framebuffer_width/height`: 分辨率
- `framebuffer_bpp`: 色深（32bpp）

### 颜色格式（32bpp BGRA）
- 背景`#0A0A12` → 0x120A0A（小端：0x0A,0x0A,0x12,0x00）
- 文字白色 → 0xFFFFFF
- 文字绿色（OK）→ 0x00FF00
- 文字红色（错误）→ 0xFF0000
- 进度条蓝色 → 0x4A9EFF

### CRC32校验
- 多项式：0xEDB88320（标准CRC32）
- 校验范围：内核二进制文件全部内容
- 期望值存储在HepuBoot配置中或内核末尾
- 校验失败：显示`Hash mismatch! Expected 0xXXX, got 0xXXX`

---

## 与现有系统的集成

### HepuBoot集成点
- `tools/hepboot/uefi/hepboot_uefi.c`: `efi_main()`中加载内核前后调用绘图
- 加载内核前：显示`Loading kernel...`和进度条
- 加载内核后：计算CRC32，显示`Hash: 0xXXX`和`Verified OK/FAILED`
- 跳转内核前：清屏或保留画面

### 内核集成点
- `kernel/arch/x86/kernel.c`: `kernel_main()`入口处初始化boot_gfx
- 各初始化模块：通过`boot_log("[INIT] XXX...")`输出日志
- 初始化完成：输出`System ready`和启动耗时，然后切换到文本shell

### 与BSOD的关系
- 启动画面使用独立的boot_gfx库，不依赖vga_gfx
- BSOD（蓝屏死机）继续使用现有实现
- 内核panic时：如果boot_gfx已初始化，用boot_gfx显示panic信息；否则回退文本模式

---

## 验证标准

### 方案A验证
1. HepuBoot显示加载进度和哈希校验结果
2. 内核启动显示深色背景+滚动初始化日志
3. 日志末尾显示启动总耗时
4. 修改内核二进制后哈希校验失败，启动停止并显示错误
5. 内核panic时信息显示在屏幕上（不只是串口）

### 方案B验证
1. 默认隐藏日志，显示居中状态提示
2. 按F12切换显示完整日志
3. 进度条随加载进度前进
4. 校验失败进度条变红
5. 启动完成后平滑过渡到shell
