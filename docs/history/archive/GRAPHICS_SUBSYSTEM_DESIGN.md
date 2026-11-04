---
title: GRAPHICS_SUBSYSTEM_DESIGN
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

# HepuOS 可插拔图形子系统（融合VGA/VBE/virtio\-gpu优点，全新自研设计）

## 设计总纲

**不直接复用三套老旧驱动，而是提取三者的核心优点，重新设计一套统一图形框架**

> 吸收优点清单：
> 
> - VGA：**极简早期文本调试能力**，内核初始化最早期、内存管理器未就绪时，就能输出 panic 文字，代码极小、应急可靠性高
> 
> - VBE：**线性帧缓冲 LFB 模型**，“一块连续物理内存代表画面” 的抽象，CPU 软渲染简单，兼容传统 PC
> 
> - virtio\-gpu：**现代硬件加速命令模型、任意分辨率、PCI 保护模式原生驱动、资源分离管理**
> 
> 舍弃三者原生缺陷：
> 
> - 抛弃 VGA 的 bank 分页图形模式，只保留文本控制台
> 
> - 抛弃 VBE 对 BIOS/int10h 的强依赖，仅把 LFB 帧缓冲作为一种软渲染后端
> 
> - 抛弃 virtio\-gpu 复杂 Virgl 3D 解析（3D 攻击面巨大，暂不实现）
> 
> 

核心思想：**上层统一绘图 API，底层多后端自动探测切换；图形资源与屏幕表面分离；内核只做基础硬件驱动，复杂渲染逻辑交给用户态**。

## 整体分层架构（全新自研）

```Plain Text
┌─────────────────────────────────────────────┐
│ 【统一绘图上层API】（完全硬件无关）          │
│  矩形填充、像素绘制、字符绘制、Blit、分辨率设置、缓冲区交换
│  你的启动画面、终端、后续UI全部调用这一层，不需要修改任何绘图代码
└───────────────────┬─────────────────────────┘
                    ↓
┌─────────────────────────────────────────────┐
│ 【图形HAL硬件抽象层（核心自研）】            │
│  定义统一后端接口，管理图形资源、帧缓冲、渲染表面
│  自动探测硬件，选择后端；支持运行时切换（可选）
└───┬───────────────┬────────────────┬────────┘
    ↓               ↓                ↓
┌───────┐      ┌────────┐     ┌────────────┐
│VGA后端│      │VBE后端 │     │virtio-gpu后端│
│(仅文本)│      │(LFB软渲染)│  │(2D硬件加速) │
└───────┘      └────────┘     └────────────┘
```

### 1\. VGA 后端（只提取优点，裁剪掉缺陷）

**只保留 VGA 文本模式，废弃 bank 图形模式**

- 用途：**内核最早期应急控制台**
在页表、内存管理器还没初始化完成时，直接操作 VGA IO 端口输出字符。内核 panic、启动早期崩溃时，依然可以打印错误。

- 能力：80×25 文本，黑白 / 少量彩色，极小代码体积。

- 限制：**不作为图形渲染后端**，只用来打印文本调试信息。

> 学到 VGA 的核心价值：底层崩溃场景下的可观测性。
> 
> 

### 2\. VBE 后端（提取 LFB 线性帧缓冲优点，解除 BIOS 绑定）

只借用「线性帧缓冲」模型，**不依赖频繁 BIOS 中断**

- 初始化阶段一次性调用固件获取 LFB 物理地址、分辨率信息；初始化完成后，不再调用 BIOS。

- 一块连续物理内存作为屏幕表面，CPU 直接写像素，软渲染。

- 支持自定义分辨率遍历，自动选择合适的显示模式。

- 适用场景：传统 BIOS 物理机、简易虚拟机，适合开发调试（方案 A 启动画面）。

> 学到 VBE 的价值：简洁的帧缓冲抽象，快速实现 2D 软渲染。
> 
> 

### 3\. virtio\-gpu 后端（提取现代硬件加速优点，精简协议）

自研简化版 virtio\-gpu 前端驱动，只保留 2D 加速，砍掉 3D Virgl

- PCI 设备 \+ virtqueue 环形队列，保护模式原生驱动，完全脱离 BIOS。

- 支持硬件 Blit、矩形填充，大幅降低 CPU 开销，消除画面闪烁。

- 支持任意分辨率、多显示器。

- 资源模型：屏幕表面、离屏纹理资源分离，资源生命周期由内核统一管理。

> 安全约束：所有来自宿主机的命令、偏移、长度全部强制校验，单独做 POC/fuzz 测试。
> 学到 virtio\-gpu 价值：现代硬件加速、资源管理模型，作为高性能主后端。
> 
> 

## 硬件探测逻辑（启动阶段）

内核启动时自动探测，优先级从高到低：

1. 扫描 PCI 设备，检测 virtio\-gpu → 存在则启用 virtio\-gpu（高性能首选）

2. 无 virtio\-gpu，尝试获取 VBE LFB 帧缓冲 → 启用 VBE 软渲染后端

3. VBE 不可用 → 回退 VGA 文本控制台（应急调试）

额外支持手动指定后端，内核启动参数：
`hepu graphics=virtio` / `graphics=vbe` / `graphics=vga`

## 安全设计（贴合 HepuOS 安全目标）

1. **模块化编译裁剪**
不用的后端，编译阶段直接剔除，不会链接进内核，减少攻击面。
例如发布虚拟机版本，只编译 virtio\-gpu；老物理机版本编译 VBE\+VGA。

2. **内核与渲染职责分离**
内核图形层只负责：映射显存、硬件命令转发、资源内存管理。
矢量字体、图片解码、窗口合成**全部放在用户态沙箱进程，不进内核**。

3. 内存权限控制
帧缓冲 / 图形资源内存标记：**可写，不可执行**，防止利用图形缓冲区注入代码。

4. 输入校验
virtio\-gpu 的命令、资源偏移、缓冲区长度全部严格校验，编写对应的漏洞 POC 测试。

5. 降级安全策略
VBE 依赖固件，固件无法审计。所以安全环境默认优先 virtio\-gpu；VBE/VGA 仅作为兼容降级方案。

## API 头文件 `graphics.h`（全新自研统一接口）

```c
#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

// 颜色：32bit ARGB
typedef uint32_t gfx_color_t;

// 图形后端抽象结构体
struct gfx_backend {
    // 初始化后端，返回0成功
    int (*init)(void);
    // 关闭图形后端
    void (*shutdown)(void);
    // 设置分辨率
    int (*set_mode)(uint32_t w, uint32_t h, uint32_t bpp);
    // 矩形填充
    void (*fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, gfx_color_t color);
    // 绘制ASCII字符（点阵字体）
    void (*draw_char)(uint32_t x, uint32_t y, char c, gfx_color_t fg, gfx_color_t bg);
    // 硬件Blit，把离屏表面拷贝到屏幕（virtio-gpu硬件加速，VBE为软件实现）
    void (*blit)(void* src, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy);
    // 获取屏幕帧缓冲虚拟地址
    void* (*get_fb_vaddr)(void);
    // 刷新画面到屏幕（交换缓冲区）
    void (*present)(void);
};

// 自动探测并选择图形后端
int gfx_probe_backend(void);
// 获取当前激活后端
struct gfx_backend* gfx_get_active_backend(void);

// 全局上层绘图API，硬件无关
static inline int gfx_set_mode(uint32_t w, uint32_t h, uint32_t bpp)
{
    struct gfx_backend* be = gfx_get_active_backend();
    return be->set_mode(w, h, bpp);
}

static inline void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, gfx_color_t color)
{
    struct gfx_backend* be = gfx_get_active_backend();
    be->fill_rect(x, y, w, h, color);
}

static inline void gfx_draw_char(uint32_t x, uint32_t y, char c, gfx_color_t fg, gfx_color_t bg)
{
    struct gfx_backend* be = gfx_get_active_backend();
    be->draw_char(x, y, c, fg, bg);
}

static inline void gfx_blit(void* src, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy)
{
    struct gfx_backend* be = gfx_get_active_backend();
    be->blit(src, sx, sy, w, h, dx, dy);
}

static inline void gfx_present(void)
{
    struct gfx_backend* be = gfx_get_active_backend();
    be->present();
}

#endif
```

## 开发实施顺序

1. 当前阶段：**实现 VGA 应急文本控制台 \+ VBE 后端**。基于这套统一 API 开发方案 A 启动画面，用于安全测试。virtio\-gpu 后端暂时不编译。

2. 内核基础稳定、安全 POC 测试全部完成后：实现 virtio\-gpu 后端。**上层绘图代码完全不用改动**，直接切换后端。

3. 切换启动画面到方案 B（默认隐藏日志，F12 调出调试控制台）。

4. 上层用户态开发：矢量字体渲染、窗口管理器，运行在用户态沙箱。

## 这个自研架构的核心亮点

✅ 不是简单堆砌三套驱动，**萃取三者设计优点，剔除原生硬件缺陷**
✅ 一套绘图代码，自动适配三种硬件场景
✅ 兼顾底层调试可靠性（VGA）、开发简易性（VBE）、现代硬件加速（virtio\-gpu）
✅ 安全友好，可按需裁剪内核驱动代码，攻击面可控
✅ 可以渐进开发，不用一次性完成全部后端

如果你需要，我可以继续写：

1. 后端探测实现代码 `gfx_core.c`
或者

2. 完整架构文档《HepuOS 统一可插拔图形子系统设计说明书》

你想要哪一个？

> （注：部分内容可能由 AI 生成）
