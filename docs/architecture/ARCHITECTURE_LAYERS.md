---
title: ARCHITECTURE_LAYERS
status: active
sys_version: 0.54.12
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
