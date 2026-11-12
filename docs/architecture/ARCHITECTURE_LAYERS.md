---
title: ARCHITECTURE_LAYERS
status: active
sys_version: 0.58.0
updated: 2026-11-11
owner: architecture
source_of_truth: kernel/arch/x86_64/Makefile
supersedes: null
---

# 图形与桌面分层架构

> 本文回答一个常见混淆：**谁在画像素，谁在摆界面，终端到底在哪一层。**
> 命名变更：`hepushell` 已重命名为 `hpdesk`（2026-11-11），详见文末。

## 一、一句话分层

| 模块 | 角色 | 类比 |
|---|---|---|
| `gfx64.c` | 图形子系统：帧缓冲、图元、字体、alpha 混合 | 笔 |
| `hpdesk.c` | 桌面环境：窗口、面板、主题、光标合成 | 手 |
| `hsqml.c` | 配置解析（Theme / UI 段） | 说明书 |
| `hs_apps.c` | 应用窗口（终端、文件、编辑器…） | 房间里的家具 |
| `desktop64.c` | 主循环：输入 → 渲染 → 事件 | 心跳 |
| `cursor64.c` | Xcursor 主题解析与光标绘制 | 指针 |

关键判据：**`gfx64` 不含任何界面语义**（不知道什么是面板、窗口、光标热点）；**`hpdesk` 不含任何像素计算**（不直接写帧缓冲，全部通过 `gfx64_*` 图元完成，共 214 处调用）。

## 二、一帧的调用链

```
desktop64 主循环
 ├─ hsinput_poll()                     ← 键盘/鼠标/PS2/XHCI 合并坐标与按键
 │    └─ hpdesk_handle_click(x, y)      点击分发
 └─ hpdesk_render()                    ← 合成一帧
      ├─ hs_wallpaper_render()          背景（纯色 / Dusk / Ocean 渐变）
      ├─ 面板层（7 个，按 panel_registry 启停）
      │    ├─ HS_PANEL_TOP_BAR          顶栏
      │    ├─ HS_PANEL_DYNAMIC_ISLAND   灵动岛
      │    ├─ HS_PANEL_DOCK             Dock
      │    ├─ HS_PANEL_SIDEBAR          侧边栏
      │    ├─ HS_PANEL_CONTROL_CENTER   控制中心
      │    ├─ HS_PANEL_DESKTOP_CLOCK    桌面时钟
      │    └─ HS_PANEL_WEATHER          天气
      ├─ 窗口层：按 z_order 从底到顶遍历
      │    └─ w->render(w)              各窗口自绘（Settings / 终端 / 编辑器…）
      ├─ 上下文菜单 / 配色面板 / 通知
      └─ cursor64_draw(mouse_x, mouse_y) ← 光标最后画，保证在最上层
           └─ 未装载主题时回退 gfx64_draw_mouse_cursor()

所有绘制最终落到 gfx64_* 图元：
gfx64_fill_rect / fill_rounded_rect / draw_line / fill_circle /
blend_pixel / blit_alpha / blur_region / font64_draw_string
```

## 三、终端在哪一层

终端**不是** `hpdesk` 本体，而是它管理的一个应用窗口：

```c
/* hs_apps.c:2001 */
hs_window_t *hs_terminal_create(void);
```

`hpdesk.c` 全文对终端的引用只有 2 处，都是调用创建（`HS_APP_TERMINAL` 启动时），没有 VT100/命令解析逻辑。所以：

- 想改终端行为 → 改 `hs_apps.c` / 终端模拟模块
- 想改终端窗口的位置、层级、标题栏 → 改 `hpdesk.c`
- 想改字符怎么变成像素 → 改 `gfx64.c` 的字体渲染

## 四、主题与配置从哪来

```
/etc/hepushell/config.hsqml
    Theme { mode: light|dark|mono, background: #RRGGBB, accent: ... }
    UI    { panel_y, island_width, dock_* ... }
        │
        ├─ hsqml.c 解析 → load_theme_from_config() / load_ui_from_config()
        └─ 图形界面写入：设置→外观页色板点选后直接改这个文件的 7 个字符
```

注意：配置文件路径仍为 `/etc/hepushell/`，改名时刻意保留，避免已部署镜像失配。

## 五、与参考实现 Quickshell 的对应

hpdesk 的目标是复现 [Quickshell](https://quickshell.org/about/) 的核心能力——用声明式配置驱动桌面组件。

| Quickshell | 本系统 | 差距 |
|---|---|---|
| QML / QtQuick 渲染 | `gfx64.c` 自研光栅化 | 无 GPU、无场景图 |
| `PanelWindow`（锚定面板） | `HS_PANEL_*`（7 个） | 无 anchor 边距系统 |
| `FloatingWindow`（普通窗口） | `HS_WINDOW_FLOATING` | 无窗口内容离屏缓冲 |
| Wayland layer-shell | 直接写 VBE framebuffer | 无协议层 |
| 配置热重载、进程/套接字服务 | 无 / 部分（`hsipc.c`） | 配置改动需重启 |

## 六、改名记录

| 项 | 变更 |
|---|---|
| 文件 | `hepushell.c/h` → `hpdesk.c/h` |
| 标识符 | `hepushell_*` → `hpdesk_*`，guard `HEPUSHELL_H` → `HPDESK_H` |
| 日志前缀 | `[HepuShell]` → `[Hepu Desktop]` |
| 用户可见文案 | "HepuShell Desktop Environment" → "Hepu Desktop Environment" |
| **保留不变** | 配置文件路径 `/etc/hepushell/`、`rootfs/etc/hepushell/` |

原因：该模块早已从"终端 shell"演进为"桌面环境"，旧名与新职责错位，容易在改动定位时误判层归属。

## 七、P0 渲染与资源升级（V0.54.13）

此前每帧无条件重绘整屏、无动画、无图片解码，与 Quickshell 的观感差距主要来自这三点。P0 阶段一次补齐：

### 1. 按需合成（离屏缓冲 + 脏矩形）

`hpdesk_render()` 不再无条件绘制，帧循环里先判三件事：

| 判据 | 含义 |
|---|---|
| `g_content_dirty` | 内容层脏（交互、数据变化、显式 `hpdesk_mark_dirty()`） |
| `hs_anim_active()` | 有动画在跑（窗口进场、灵动岛过渡） |
| 鼠标是否移动 | 光标位置变化 |

三者皆假则整帧直接 `return`，一轮零绘制。光标**不进后台缓冲**，只在 `gfx64_present()` 之后画在前台帧上——这样鼠标移动不会触发内容重绘，且每帧提交天然覆盖上一帧的光标残影。

兜底：每 30 帧强制置脏一次，防止漏标导致画面停更（最坏情况内容 0.5s 刷新一次）。

实测（QEMU 静止桌面，1024×768）：每 300 帧中 `redraw=10 / skip=290`，即 **96.7% 的帧被跳过**，同时时钟仍正常走字（兜底生效）。

调用链变为：

```
hpdesk_render()
  ├─ dirty || animating ?  gfx64_set_target_back() → 壁纸/面板/窗口/菜单/通知
  ├─ hs_present_with_cursor(mx,my)
  │    ├─ gfx64_present()          后台 → 前台
  │    ├─ gfx64_set_target_front()
  │    └─ cursor64_draw(mx,my)     光标只在最前层
  └─ 清脏、记录鼠标位置
```

标脏入口：`hpdesk_handle_click/key/right_click/mouse_up` 四个输入函数、状态数值节流点（0.5s 采样一次）、锁屏/登录切换、窗口与面板状态变化。

### 2. 通用缓动

`hs_ease_out_cubic(p) = 100 - (100-p)³/10000`，进度量 0..100。

- **灵动岛**：`g_island_t` 每帧向 `g_island_target` 逼近 8（≈120ms @60fps），宽高由收起尺寸与 360×120 线性插值，内容在进度过半时切换，点击只设目标不直接跳变。
- **窗口**：沿用进场时的 `anim_start/anim_dur` 滑入，`hs_anim_active()` 保证动画期间持续重绘。
- **弹出浮层**（右键菜单、桌面配色面板）：`g_menu_t` / `g_color_t` 复用同一缓动，起点比目标位置低 12px，ease-out 后归位，消除"瞬间弹出"。
- 启动时自动演示一次展开→收回，串口可看到 `t=8,16,...,100,...,0` 的渐变序列。

### 3. PNG 解码 + 图像缓存

`png64.c` 自包含实现，不引 zlib：

| 模块 | 覆盖范围 |
|---|---|
| inflate | stored / fixed huffman / dynamic huffman（RFC 1951） |
| chunk | IHDR / PLTE / tRNS / IDAT / IEND |
| 反滤波 | None / Sub / Up / Average / Paeth（RFC 2083） |
| 颜色类型 | 0 灰度 / 2 RGB / 3 调色板 / 4 灰度+alpha / 6 RGBA |
| 位深 | 8 / 16（取高字节）/ 1、2、4（灰度与调色板按位展开） |
| 不支持 | 隔行 Adam7（返回 -3，调用方回退） |

输出统一 RGBA8888，像素缓冲由 `pmm64_alloc_pages` 分配，`png64_free` 归还。错误码 -1 参数 / -2 非 PNG / -3 不支持 / -4 inflate / -5 内存 / -6 损坏。

接入点：

1. **图片查看器** —— 文件管理器打开 PNG 时解码并按窗口等比缩放绘制。
2. **PNG 壁纸** —— `/usr/share/wallpapers/wallpaper.png` 存在则优先于内置三种渐变样式；
   首次使用时解码并缓存（`g_wp_png`），按屏幕比例居中覆盖，不拉伸变形。
   打包入口在 `assets/samples/wallpaper.png` → 镜像 `/usr/share/wallpapers/`。
3. **启动自检** —— `/usr/share/samples/sample.png`，串口打印尺寸与首像素。

> 注意：从对话上传的素材常被 CDN 转成 WebP（文件头 `RIFF`），落盘前需确认真正的 PNG 签名 `89 50 4E 47`，否则解码器返回 -2。

主机回归：`tools/host-tests/png_test.c` + 5 个夹具（RGBA / RGB / 调色板透明 / 灰度 8bit / 灰度 1bit），逐像素比对，已纳入 `run_host_tests.sh`。
