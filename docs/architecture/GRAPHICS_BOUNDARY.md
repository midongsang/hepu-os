# 图形栈边界（GRAPHICS_BOUNDARY）

> 建立于 v0.59.x，起因：32 位图形正在攻坚（寄存器、色深），64 位图形早已验收。
> 两条线**不能互相污染** —— 32 位的试错不能把 64 位的已验证路径拖下水，
> 64 位的真彩色契约也不能被 32 位的 VGA 现实反向修改。
>
> 这不是"把 32 位当死代码"（那违反 `UNIFIED_SOURCE_TREE.md`）。
> 是**两套内核、两套图形实现、一层纯算法共享**。

## 一、三层模型

```
┌───────────────────────────────────────────────────────┐
│  共享层  kernel/shared/gfx/                            │
│  gfx_algo.c + gfx_shared.h  —— 纯算法，无硬件          │
│  两边都编，两边都用；不许出现端口 IO / 物理地址 / 模式  │
└───────────────────────────────────────────────────────┘
        ↑                                    ↑
┌───────────────────────┐        ┌───────────────────────┐
│ 32 位图形实现          │        │ 64 位图形实现          │
│ kernel/gfx/*（18 文件）│        │ arch/x86_64/gfx64.c    │
│ arch/x86/desktop_render│        │ arch/x86_64/desktop64.c│
│ arch/x86/vga_gfx*.c    │        │                        │
│ arch/x86/vga.c         │        │ 真彩色 VBE 线性帧缓冲  │
│ arch/x86/boot_gfx.c    │        │ 走 gfx_bpp == 32       │
│ arch/x86/elastic_desk  │        │                        │
│ arch/x86/gfx_selftest  │        │                        │
│ drivers/vga_cjk.c      │        │                        │
│  ★ 当前状态: VGA 0x13  │        │  ★ 当前状态: 已验收    │
│    8bpp，色深待升级     │        │                        │
└───────────────────────┘        └───────────────────────┘
```

## 二、精确清单

### 32 位图形实现（只属于 `hepu-kernel.bin`）

| 路径 | 内容 |
|---|---|
| `kernel/gfx/gfx_core.c` | 后端选择（VIRTIO→GOP→VBE→VGA）与绘制 API |
| `kernel/gfx/gfx_vga.c` | VGA 文本/图形模式 |
| `kernel/gfx/gfx_vbe.c` | VBE 线性帧缓冲（32 位未走过） |
| `kernel/gfx/gfx_gop.c` | GOP 后端（假 GOP，只看 `gfx_width != 0`） |
| `kernel/gfx/gfx_virtio.c` | virtio 后端 |
| `kernel/gfx/*` 其余 13 个 | 3D、字体、TTF、多屏、DRM、终端 |
| `kernel/arch/x86/vga_gfx.c` | mode 0x13 寄存器编程 + `gfx_flip()` |
| `kernel/arch/x86/vga_gfx_text.c` | 文本模式绘制 |
| `kernel/arch/x86/vga_gfx_aa.c` | 抗锯齿绘制 |
| `kernel/arch/x86/vga.c` | 文本控制台 |
| `kernel/arch/x86/boot_gfx.c` | 启动期图形 |
| `kernel/arch/x86/desktop_render.c` | 弹性桌面（`elastic_desktop_render` / `main_loop`） |
| `kernel/arch/x86/elastic_desktop.c` | 桌面初始化 |
| `kernel/arch/x86/gfx_selftest.c` | 图形自检 |
| `kernel/drivers/vga_cjk.c` | CJK 字模 → 平面 2 |

### 64 位图形实现（只属于 `hepu-kernel64.bin`）

| 路径 | 内容 |
|---|---|
| `kernel/arch/x86_64/gfx64.c` | 64 位真彩色绘制、`gfx64_fb_desc()` |
| `kernel/arch/x86_64/desktop64.c` | 64 位桌面主循环 |
| `kernel/arch/x86_64/display_backend.h` | 后端契约（3 入口 + `fb_desc_t`） |

### 共享层（两边都编）

| 路径 | 约束 |
|---|---|
| `kernel/shared/gfx/gfx_algo.c` | 265 行，0 处硬件细节 |
| `kernel/shared/gfx/gfx_shared.h` | 类型、`gfx_point_in_rect`、`gfx_blend`、后端枚举 |

## 三、三条规则

**规则 1 —— 硬件在两边，算法在中间。**
写模式、碰端口、算物理地址的代码只能落在 32 位或 64 位目录里。
能被两边复用的只能是纯函数（输入输出都是值，没有副作用）。

**规则 2 —— 不许"顺手抽一个共享层"。**
把 32 位的绘制逻辑搬去 `shared/gfx` 看着很优雅，但那会把 VGA 的现实
（8bpp、索引色、`0xA0000`）塞进 64 位的真彩色契约。
共享只允许**增量**：先有一份纯函数、两边都真的要用，再抽。

**规则 3 —— 32 位的试错不外溢。**
32 位图形当前处在"能写显存但色深不通"的阶段，会持续改寄存器、改模式。
这些改动一律只动 32 位文件。64 位图形路径（`gfx64.c` 等）不参与。

## 四、守卫机制

边界不靠人记得，靠两处硬拦截。

### 1) 构建期守卫（`kernel/arch/x86_64/Makefile`）

```make
GFX32_IN_64 := $(filter $(KERNEL_DIR)/gfx/% %/desktop_render.c %/vga_gfx.c \
                       %/vga_cjk.c %/elastic_desktop.c %/boot_gfx.c \
                       %/gfx_selftest.c %/vga.c,$(C_SOURCES))
ifneq ($(strip $(GFX32_IN_64)),)
$(error 图形栈边界被打破: 64 位构建引入了 32 位图形实现 -> $(GFX32_IN_64))
endif
```

已验证：命令行注入 `kernel/gfx/gfx_core.c` 到 `C_SOURCES`，构建**立即失败**：

```
Makefile:173: *** 图形栈边界被打破: 64 位构建引入了 32 位图形实现 -> .../kernel/gfx/gfx_core.c.  Stop.
```

### 2) 静态自检（`tools/check_gfx_boundary.sh`）

5 条判据：

| # | 判据 | 现状 |
|---|---|---|
| 1 | 64 位 Makefile 不含 `kernel/gfx/` | ok |
| 2 | 守卫宏 `GFX32_IN_64` 在位 | ok |
| 3 | `shared/gfx` 无 `outb/inb/端口/0xA0000/0xB8000` | ok |
| 4 | 32 位图形文件全部还在 32 位目录 | ok |
| 5 | 64 位图形实现恰好 3 处 | ok |

当前输出：`[BOUND] === 图形栈边界完好 ===`，退出码 0。

判据 3 只认真正的硬件访问形态（`outb(`、`0x3C4`、`0xA0000` 等），
**不检后端枚举名**（`GFX_BACKEND_VGA/VBE` 是契约词汇，不是硬件细节）。

## 五、当前状态与下一步

| | 32 位 | 64 位 |
|---|---|---|
| 能否写显存 | ✅ 已验证 `nonzero=0xFA00` | ✅ |
| 能否画对颜色 | ❌ **8bpp 索引色 vs 24 位真彩** | ✅ |
| 能否进桌面主循环 | ✅ `[EDESKTOP] entering main loop` | ✅ |
| 桌面是否正确显示 | ❌ 全黑（色深不匹配） | ✅ |

**32 位的正解**：走 bochs dispi VBE + 线性帧缓冲，切到真彩色，与 64 位共用
`gfx_bpp == 32` 语义。缺三样：PCI config 读 BAR0、dispi 端口设模式、接到
`kernel/gfx/gfx_vbe.c`（该文件存在但 32 位从未走过）。

**这条路只在 32 位图形实现里做**，共享层不动、64 位不动 —— 这正是本文件要保证的事。
