# 图形栈现状对照（三层模型 × 代码实证）

> 用途：把「显示后端 → 合成器 → GUI 组件库」这套三层模型与 Hepu 现役代码逐项对齐，
> 标出**已有 / 部分 / 缺**，并给出证据行号。所有判断以代码为准，不以文档规划为准。

## 一、现役分层（事实）

```
hs_apps.c     应用内容（终端/文件管理器/任务管理器各自 render 回调）
hpdesk.c      窗口管理 + 面板 + 合成（单进程，一体化）
gfx64.c       绘图原语 + 前后缓冲 + blit（直接操作 LFB）
hsinput.c     键鼠原始输入
font64.c      字形栅格
```

`docs/architecture/ARCHITECTURE_LAYERS.md` 描述的就是这条链路；`GUI.md` 明确标注为
32 位遗留树（`theme.c`/`widgets.c`/`mobile.c`），**不在 Makefile 里，不是现役实现**。

结论先说：**现在是「单进程一体化」（三层模型里的方案 1），已走到阶段 2 的后期** ——
多窗口、Z 序、拖动、焦点、遮挡都有了。缺的是三层模型里最关键的三个抽象：
**Surface、脏矩形、DisplayBackend**。

## 二、六项统一规范 × 代码现状

| 规范 | 状态 | 证据 / 差距 |
|---|---|---|
| **1. 统一坐标系** | 部分 | 全局用物理像素整数坐标，一致。但缩放是**每个调用点自己乘** `hs_sx/hs_sy/hs_s`（150% 下逐个换算），不是全局变换；窗口**没有"客户区 (0,0)"约定** —— `render(hs_window_t*)` 拿到的是窗口对象，各 app 自己算 `win->x + 偏移`，等于每家一套本地坐标 |
| **2. 统一事件总线** | 部分 | 窗口级已统一：`hs_window_at()`（hpdesk.c:1791）按 Z 序命中 → 焦点窗口；键盘单点分发 `hs_apps_handle_key()`（hpdesk.c:5366）。**缺控件级 hit-test**（没有控件树）；**缺输入服务层**（无键位映射表、无鼠标加速/校准） |
| **3. 统一渲染原语** | 有（缺裁剪） | 上层只调 gfx64（`fill_rounded_rect` / `draw_rounded_rect` / `fill_circle` / `font64_draw_string` / blit），不碰帧缓冲。**没有裁剪矩形（clip/scissor）** —— 窗口遮挡靠"先画底再画顶"的绘制顺序，不是 clip；**没有 alpha 合成** |
| **4. 统一窗口模型** | 部分 | `hs_window_t`（hpdesk.h:197）有 `x/y/w/h`、`visible`、`focus`、`always_on_top`、`workspace`、`title`、拖动状态、`render` 回调；Z 序在 manager 的 `z_order[]`。**缺**：最小/最大尺寸、`opacity`、`clip`、不透明区域、父子窗口（弹窗全是顶层浮动） |
| **5. 字体/颜色/资源** | 部分 | 颜色：主题变量 + Material 3 色板 + `config.hsqml` 配置 ✓。字体：`font64_draw_string(fb, fw, fh, x, y, s, color, size)` 每次传参，**无全局字号/字重管理**。图像：PNG 解码在使用点各自持有 `png64_image_t`（壁纸 `g_wp_png`、`v_png` 等），**没有全局图像缓存**，同一图标多处解码 |
| **6. DisplayBackend 抽象** | 缺 | gfx64 内有 `front_buffer` / `back_buffer` / `gfx_target` 三个渲染目标，但没有「后端接口」这层：**无法在 FB / GPU 之间切换**，代码直接操作 LFB。`docs/architecture/GRAPHICS.md` 写过 VGA/VBE/virtio 三后端，那是规划，未落地 |

## 三、三个最关键的空缺（按 ROI 排序）

### 1. 脏矩形 —— 现在每帧全量重绘

`hpdesk_render()` 每帧把整个桌面（含所有窗口）重画一遍，再整帧 blit：
1920×1080×4B ≈ **8MB / 帧**，还要和 8MB 帧拷贝抢锁。60fps 靠限速硬压（desktop64.c
约 16ms 一次），输入延迟与它解耦过一轮，但掉帧风险在这儿。
加脏矩形是投入最小、收益最直接的一项：窗口拖动时只重画"旧位置 ∪ 新位置"两块。

### 2. Surface —— 窗口目前没有自己的画布

现在所有窗口**直接画到同一个后台缓冲**。后果：
- 窗口做不了 `opacity`（没有可混的独立层）；
- 做不了裁剪（没有可裁的独立区域）；
- 动画期间整窗重画（没有上一帧缓存可比对）；
- App 画错坐标直接污染整屏（`render` 回调拿的是全局 framebuffer）。

Surface 是三次模型里最"地基"的一项，Wayland/X11/Windows 都是这个模型。

### 3. 控件基类 —— 每个 App 自绘 UI

`hs_widget_t`（hpdesk.h:81）只是**面板上的小部件**（CPU/内存指示条那种），
不是按钮/输入框的控件基类。终端、文件管理器、任务管理器各自用 `gfx64_fill_rounded_rect`
+ `font64_draw_string` 手拼 UI —— 视觉能对齐全靠人肉对常量。
上一轮「侧栏 18px vs 顶栏 15px」「通知卡右边越界」两个对齐问题，根因就是没有统一布局容器。

## 四、方向判断

三层模型的**方向与 Hepu 现役完全一致**，`ARCHITECTURE_LAYERS` 就是同一个分层，
只是词汇不同（他们叫 compositor，我们叫 hpdesk）。他的「渐进路线」也正是我们已经在走的：
先一体化跑通，再拆协议。

所以**不需要推翻重写**，需要的是补三个抽象，顺序建议：

1. **脏矩形**（1~2 天量级）：给 `hpdesk_render` 加 dirty 区域，窗口移动/内容变化标记，
   帧末只 blit 脏区。立刻降低 8MB/帧 的开销。
2. **Surface**（中等）：`hs_window_t` 增加 `surface`（离屏缓冲 + 尺寸 + origin），
   `render` 回调改为画到自己的 surface，合成器逐窗口 blit 到后台缓冲，顺带获得
   裁剪（blit 时按窗口矩形裁）与 `opacity`。
3. **控件基类**（中等）：抽出 `widget`（`on_paint` / `on_event` / `layout`），
   终端、文件管理器先迁移，**只保留一套**，系统自带工具复用同一套。
4. **DisplayBackend**（长期）：等 GPU 驱动成熟再做，接口先定义不实现也可以。

## 五、明确不做的事

- 不新建第二套组件库（对齐本项目「一套组件库喂所有自带程序」的原则）；
- 不在 App 里碰 framebuffer（现状已经守住这条，靠 gfx64 API）；
- 不为了分层而把现有 `hpdesk` 拆成独立进程 —— 隔离性收益要在「阶段 4」才值得付 IPC 成本。
