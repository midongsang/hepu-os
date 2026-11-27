---
title: VISUAL_SPEC
status: active
sys_version: 0.63.0
updated: 2026-11-28
owner: architecture
source_of_truth: rootfs/etc/hepushell/config.hsqml
supersedes: null
---

# Hepu OS 视觉规格表（Visual Spec）

> **路线来源**：参考项目 Clavis Shell（`ref_projects/quickshell/`）的 `Common/Metrics.qml` + `Common/Typography.qml`。
> Clavis 把全部间距、图标尺寸、控件高度、圆角、字号收在**一处**声明，所有界面只引用不写字面值。
> Hepu 照这条路线走：本表是 Hepu 的"Metrics.qml"，任何视觉数值先登记在这里，再落到代码。
> 路线的原始论证见 [DESKTOP_SHELL.md](DESKTOP_SHELL.md) 第 1.1 节（设计令牌先行，P0）。

---

## 0. 换算式

所有尺寸写**逻辑值**（逻辑像素），渲染时按 UI 缩放换算：

```
hs_sy(v) = v * g_ui_scale_pct / 100     // 垂直（纵向）
hs_sx(v) = v * g_ui_scale_pct / 100     // 水平
hs_s(v)  = v * g_ui_scale_pct / 100     // 圆形/图标/圆角
```

当前默认档位 `scale=100`（2026-11 已从 150 下调，见 `TECH_DEBT_ROUTE` 的 D 项）、
`1920×1080`。下文"@150%"列**只作历史对照**，实际已不生效——它是按 150% 推出来的
像素值，现在所有 `hs_s()` 的结果都等于逻辑值本身。改代码请用逻辑值。

---

## 1. 布局

| 元素 | 配置键 / 常量 | 逻辑值 | @150% | 代码位置 |
|---|---|---|---|---|
| 顶栏高度 | `UI.bar_height` | **32** | 48 px | `config.hsqml`；`hpdesk.c` `hs_top_bar_render` |
| 顶栏上边距 | 固定 | 8 | 12 px | `hs_sy(8)`，`bar_top` |
| 顶栏左右边距 | 固定 | 10 | 15 px | `hs_sx(10)`，`margin` |
| 顶栏圆角 | 固定 | 18 | 27 px | `hs_s(18)` |
| 顶栏内图标 | 固定 | 24 | 36 px | `hs_s(24)` |
| 顶栏图标间距 | 固定 | 6 | 9 px | `hs_sx(6)` |
| 时钟胶囊宽 | `UI.island_width` | 120 | 180 px | `hs_dynamic_island_render` |
| 时钟胶囊高 | `UI.island_height` | **28** | 42 px | 同上 |
| 时钟胶囊圆角 | 固定 | 15 | 22 px | `hs_s(15)` |
| 时钟胶囊纵向位置 | 计算 | `bar_top + (bar_h - island_h)/2` | — | 同上 |
| 控制中心尺寸 | `UI.cc_width/cc_height` | 360 × 390 | 540 × 585 px | `cc_geometry` |
| 控制中心右缘 | 固定 | 10 | 15 px | `hs_sx(10)` |
| 控制中心纵向起点 | 计算 | `hs_sy(8) + bar_h + hs_sy(8)` | — | `cc_geometry`（与顶栏底边对齐） |
| 侧边栏尺寸 | `UI.sidebar_width/height` | 280 × 500 | 420 × 750 px | `hs_sidebar_render` |
| 侧边栏默认状态 | `UI.sidebar_collapsed` | **1（收起）** | — | 同上 |
| 天气卡尺寸 | `UI.weather_width/height` | 120 × 120 | 180 × 180 px | `hs_weather_widget_render` |
| 天气卡边距 | `weather_margin_right/bottom` | 140 / 160 | — | 同上 |
| Dock | `dock_width/height/icon` | 400 / 64 / 44 | — | `hs_dock_render` |
| Dock 底边距 | `dock_margin_bottom` | 12 | 18 px | 同上 |
| 窗口圆角 | `UI.corner_radius` | 12 | 18 px | `window64.c` / `hs_window_shadow` |

**规则**

1. 胶囊的居中基准是**顶栏自身的整屏宽度**，不是"扣掉侧栏后的净宽"。左边距与右边距必须相等，与侧栏、Dock、控制中心宽度无关。
2. 胶囊高度小于顶栏高度时，纵向按上式居中；算出的 y 小于 `bar_top` 时钳到 `bar_top`。
3. 面板（控制中心 / 侧栏 / 天气卡）的纵向起点一律跟着顶栏底边走（`bar_top + bar_h + 间距`），不写死绝对值。

### 1.1 设计令牌（2026-11-27 新增）

`hpdesk.c` 顶部已登记成套逻辑值；本轮只在新代码和新命中区引用，尚未做全文件替换。

| 类别 | 令牌 |
|---|---|
| 间距 | `HS_SP_XS=2` / `S=4` / `M=8` / `L=12` / `XL=16` / `2XL=24` |
| 圆角 | `HS_RADIUS_XS=4` / `S=12` / `M=17` / `L=23` / `XL=28` |
| 图标 | `HS_ICON_S=16` / `M=24` / `L=32` |
| 控件高 | `HS_CONTROL_S=32` / `M=40` / `L=48` / `XL=56` |
| 顶栏 | `HS_BAR_HEIGHT_DEFAULT=32`（配置、解析兜底、`hpdesk_init` 三处同源） |
| 天气内边距 | `HS_WEATHER_PAD=24` |

---

## 2. 颜色与半透明

主题色板（`config.hsqml` 的 `Theme` 段，唯一来源）：

| 键 | 值 | 用途 |
|---|---|---|
| `background` | `#F0EBE0` | 桌面底色 |
| `panel_bg` | `#E8DFD0` | 顶栏 / 面板底 |
| `panel_fg` | `#5C4A32` | 正文前景 |
| `border` | `#D4C8B0` | 描边 |
| `accent` | `#C9A96E` | 强调色（开关 On / 选中态） |
| `window_bg` | `#FAF6EE` | 窗口内容底 |
| `window_fg` | `#3D3226` | 窗口内文字 |
| `titlebar_bg` | `#E8DFD0` | 窗口标题栏 / 胶囊填充 |

半透明：`hs_panel_render_all` 中 `HS_PANEL_TOP_BAR` 与 `HS_PANEL_DYNAMIC_ISLAND` 的合成不透明度固定 **224/255**（约 88%），其余面板 255。

---

## 3. 阴影

所有浮层走同一套实现：`hs_sh_paint(slot, x, y, w, h, rc, div)`。

- 遮挡体 = 实心圆角矩形 → 盒式模糊出 alpha 掩膜 → 预渲染成 ARGB 位图 → 每帧 `gfx64_blend_image` 一次
- 缓存按槽独立（`g_shc[HS_SHC_N]`，`HS_SHC_N=4`），不同槽尺寸不同，不能共用 key
- **必须画在层合成之前、直接写主 fb**；面板 render 回调跑在 surface 上下文里，在回调内部调用会画错坐标

| 槽 | 对象 | 半径口径 | div | 偏移 |
|---|---|---|---|---|
| 0 | 控制中心 | 短边 / 30 | 30 | 4 |
| 1 | 侧边栏 | 短边 / 24 | 24 | 4 |
| 2 | 时钟胶囊 | 短边 / 40（下限 10px） | 40 | 4 |
| 3 | 天气卡 | 短边 / 22 | 22 | 4 |

窗口阴影仍走 `hs_window_shadow`，半径 = 短边 / 18（钳在 10~40），`off=3`，`pad = 3r + 6`。

成本：每槽一份 ARGB 缓存，四槽合计约 10 MB，全 CPU 逐像素 blend；`KHEAP_SIZE = 32 MB`。**再加浮层前先看剩余堆。**

---

## 4. 一致性状态（2026-11-27）

已收口：

| 项 | 状态 |
|---|---|
| `bar_height` | 内置配置串、`hsqml_get_number` 兜底、`hpdesk_init` 默认值统一为 32；`HS_BAR_HEIGHT_DEFAULT` 是唯一常量 |
| `island_y` | 已从 `hpdesk.h`、内置配置与 `config.hsqml` 删除；灵动岛绘制、阴影包围盒与点击命中共用 `hs_island_rect()` |
| 天气卡 bounds | `hs_panel_bounds` 已与 render / shadow 对齐为“卡片矩形 + `HS_WEATHER_PAD`”；无网络时改为紧凑空状态，不再画巨号 `-- / N/A` |
| 控制中心几何 | 导航 / 卡片 / 滑块 / 设备行 / Audio 页共用 `hs_cc_layout_t`，render 与 click 不再各算一套坐标 |
| 壁纸五布局 | Settings 壁纸卡五 chip 与 `g_wp_layout` 同源；Fit/Center 留边用近黑，Tile/Center 用原尺寸；4:3 五态与二次启动读回均有证据 |

仍存欠账（不影响本基线判定，但不应被当作已完成）：

| 项 | 现状 | 目标 |
|---|---|---|
| 图标体系 | 顶栏 4 枚 + 控制中心 6 开关已走 `hs_draw_icon` 矢量描边；Dock 8 图标、应用抽屉 12 图标、电池仍为 `hs_icons_data.h` 位图或专用绘制 | 全量矢量图标字体，见 `DESKTOP_SHELL.md` 1.2 |
| 全文件令牌替换 | 仅新代码 / 新命中区引用 | 逐模块替换裸数值 |

---

## 5. 落地规则

1. **先登记，后改码**：新增或修改任何视觉数值，先改这张表，再改代码。
2. **单一来源**：同一数值只允许出现一次权威定义（config 键或宏），其余位置只引用。
3. **不许写字面值**：新增渲染代码不出现 `224`、`hs_s(15)` 这类裸数字，改为引用常量/配置。
4. **改动要能自检**：本轮视觉改动应能由像素级基线回归判定（见下一步）。

---

## 6. 与参考对象的对应

| Clavis（`Common/Metrics.qml`） | Hepu 现状 |
|---|---|
| 间距 2 / 4 / 8 / 12 / 16 / 24 | 仅用到 8（顶栏上/侧边距）、10（左右边距）；无统一梯度 |
| 图标 16 / 24 / 32 | 24（顶栏，矢量 glyph）、26（面板卡六开关，矢量 glyph）；Dock/抽屉仍位图缩放 |
| 控件高 32 / 40 / 48 / 56 | 32（面板 nav 按钮 42@150% ≈ 28 逻辑） |
| 圆角 4 / 12 / 17 / 23 / 28 | 12（窗口）、15（胶囊）、18（顶栏）——未成梯度 |
| Material 3 字阶 | 12 / 14 / 18（`font_small/normal/large`）+ 各面板局部字号 |

结论：**路线已对齐，梯度未对齐**。Clavis 的 token 是成套的量阶（间距/圆角各有 5~6 级），Hepu 目前是"每个元素单独取值"，只做到了"集中登记"，还没做到"共享量阶"。这是下一步要补的。

---

## 7. P0 剩余与已接入

- **图标字体引擎（部分落地）**：已实现单色语义矢量入口 `hs_draw_icon(x,y,size,color,glyph_id)`，顶栏 power/gear/speaker/Wi‑Fi 与控制中心 Wi‑Fi/BT/Air/DND/Loc/Torch 已迁移；DND 不再使用沙漏位图。Dock 彩色应用图标、应用抽屉图标、电池仍走位图/专用绘制，完整图标字体管线仍是欠账。

**视觉基线已接入**：`tools/regress/desktop_visual.sh` 固定 RTC / 无网络 / 1920×1080 / UI 150%，采集 9 个状态；`tools/regress/compare_visual.py` 屏蔽时钟与天气动态文本后做阈值比较。基线在 `docs/status/desktop-visual-baseline-2026-11-27/`。
