---
title: DESKTOP_SHELL
status: active
sys_version: 0.54.12
updated: 2026-11-01
owner: architecture
source_of_truth: null
supersedes: null
---

# Hepu OS 桌面外壳开发指南（参考 Clavis Shell 实现）

> 本文档把参考视频（宿主 Arch Linux + niri + Clavis Shell 桌面演示）中展示的系统功能与交互，
> 对照参考项目源码（`extracted/os-source/hepu_os_src/ref_projects/quickshell/`），
> 逐项给出 Hepu OS 的落地开发路径。
> 最后更新：2026-10-22 ｜ 适用系统版本：0.54.6

---

## 0. 参考来源

| 来源 | 位置 |
|------|------|
| 参考视频 | 用户提供 `album_261022_072938_87762.mp4`（宿主机 Arch+niri+Clavis Shell 桌面演示，363s） |
| 参考项目源码 | `ref_projects/quickshell/`（Clavis Shell：QML/Quickshell/Qt6/C++，507 QML + 120 C++ 文件） |
| Hepu 现有桌面实现 | `kernel/arch/x86_64/hepushell.c`（桌面总控渲染）、`desktop64.c`（主循环）、`window64.c`（窗口管理）、`taskmgr64.c`、`icons_*.h`、`font64.c` |

**视频观察到的目标效果（功能与交互清单）：**

1. 顶栏：居中时钟（12:04 大字）、右侧天气（26°）、网络延迟（34ms）、内存占用、OS Age/Uptime、托盘图标区
2. 桌面右下角大时钟/日期小组件（时间 + "9月13日周日" + 温度）
3. 壁纸设置窗口（Settings→Wallpaper）：布局（Stretch/Fit/Fill/Tile/Cover/Panorama）、每显示器壁纸、过渡动画（Crossfade/Pixelate/Portal 等）
4. 主题设置（Settings→theme）：matugen 动态取色（Light/Dark）、光标主题/大小、锁屏样式选择、Super 键样式
5. 系统监控悬浮面板（内存条形图、延迟、时间）
6. 悬浮音乐播放器控件（APlayer：歌曲信息 + ◀▶▶▶⟳× 控制）
7. 多窗口平铺/堆叠（Discord/浏览器/终端/3D 地图/系统信息）
8. 深色/浅色主题切换、毛玻璃/圆角窗口质感

---

## 1. 从参考项目提炼的核心架构原则

### 1.1 设计令牌（Design Tokens）先行
Clavis 用 `Common/Metrics.qml` + `Common/Typography.qml` 统一全部尺寸与字体：

```
间距:   2/4/8/12/16/24 (XXS→XL)
图标:   16/24/32 (S/M/L)
控件高: 32/40/48/56
圆角:   4/12/17/23/28 (XS→XL)
锁屏:   时间字号 112px、认证框高 64、锁屏面板宽 600、卡片圆角 33
字体:   Material 3 字阶 (display 36 / headline 28·24 / title 22·16·14 / body 16 / label 14·12)
```

**Hepu 落地**：在 hepushell.c 顶部定义一套常量表（`HS_SP_XS=2` 等），逐步替换散落的魔法数字。
优先替换：按钮圆角（当前四角异常）、顶栏高度、图标尺寸、窗口内边距——即用户多次反馈的"圆角突出/间距混乱"根因。

### 1.2 图标 = 矢量字体，不是位图
Clavis 的 `Components/MaterialSymbol.qml`：用 **Material Symbols 可变字体**渲染图标（Text + FILL/opsz 双轴），
天然矢量（任意尺寸清晰）、单色（颜色=前景色，随主题）、可做填充动画。配合 `ThemeIcon`（系统主题图标）/`SvgIcon`（SVG）/`FileThemeIcon`（文件类型图标）分层。

**Hepu 落地**（解决"图标画不好/模糊/分辨率低/单色不统一"）：
1. 将一套单色矢量图标（如 Material Symbols 或自绘 16×16 点阵矢量化）编译为**字体位图数据**（复用现有 `font_data.c` 的字体渲染管线，扩展为"图标字体"段）；
2. `hepushell` 增加 `hs_draw_icon(x, y, glyph_id, size, color)`：按 size 从图标字体栅格化（矢量插值），颜色取主题色——替代 `icons_*.h` 的固定尺寸位图与"图标背景块"；
3. 图标语义分级（参考 `Bar/QuickSettings/Network.qml`）：以太网→`settings_ethernet`、断开→`wifi_off`、按信号强度 80/60/40/20 分级→wifi 4/3/2/1/0 格——解决"网络图标反的/意思不明确"。

### 1.3 状态与 UI 分离（Services 单点状态源）
Clavis：`Services/`（NetworkService/MediaManager/AutostartService/WallpaperService...）持长期状态，
`Modules/` 只读状态渲染。任何功能只有一个状态源。

**Hepu 落地**：把 hepushell.c 中的散落状态（`g_top_bar_dirty`、托盘、天气、音量、网络速率）整理为"桌面状态结构体"（`g_shell_state`），
顶栏/托盘/面板只读该结构；网络速率计数在 e1000 层更新（已实现 `e1000_get_net_stats`），UI 层只消费。

### 1.4 模块化：顶栏 = 6 个独立子模块
Clavis `Modules/Bar/`：ActiveWindow / Media / QuickSettings / SysMonitor / Tray / Workspaces 各自独立 QML，Bar 只是装配器。
**Hepu 落地**：hepushell 顶栏渲染拆成独立函数（`hs_bar_clock`/`hs_bar_weather`/`hs_bar_monitor`/`hs_bar_tray`/`hs_bar_net`），
各自维护 dirty 位，状态变化才重绘对应区（现有 `g_top_bar_dirty` 机制扩展为分区位图）。

---

## 2. 分模块开发实现

### 2.1 顶栏（时钟 / 天气 / 系统监控 / 托盘 / 网络）

| 项 | 视频效果 | Clavis 参考 | Hepu 落地 |
|----|---------|-----------|----------|
| 时钟 | 居中大字 HH:MM | `Keystone/ClockContent` 时间格式化（12/24 小时制） | `hs_bar_clock`：复用现有 RTC，分钟变化才重绘 |
| 天气 | 右上 26° + 图标 | `Bar/QuickSettings` 天气按钮 + `WeatherContent` 详情 | 已实现 `hepushell_fetch_weather`（open-meteo，温度/weathercode 已解析），补图标分级（按 weathercode 映射天气图标） |
| 系统监控 | 延迟 34ms / 内存 / Uptime | `Bar/SysMonitor/ResourcePie.qml` | `hs_bar_monitor`：e1000 网络速率（已实现）+ 内存百分比条（taskmgr 已有绘制可复用） |
| 托盘 | 图标区 | `Bar/Tray/TrayItem.qml`（status notifier） | 现有托盘绘制改走图标字体渲染 + 主题色；修复"图标时有时无"（托盘 dirty 标记问题） |
| 网络 | — | `Network.qml` 分级图标 | 按 1.2 落地分级图标（以太/断开/信号 4 档） |

### 2.2 桌面壁纸管理器（Settings→Wallpaper）

| 视频效果 | Clavis 参考 | Hepu 落地 |
|---------|-----------|----------|
| 布局模式：Stretch/Fit/Fill/Tile/Cover/Panorama | `Services/WallpaperService` + `Modules/ControlCenter` 壁纸页 | hepushell 壁纸渲染增加 `hs_wallpaper_layout`（拉伸/适应/填充/平铺），绘制时按布局计算源矩形 |
| 每显示器壁纸 | Per-monitor wallpapers | 单屏阶段留接口，状态字段预置 |
| 过渡动画：Crossfade/Pixelate/Portal | 壁纸切换过渡枚举 | 先做 Crossfade（双缓冲 alpha 混合），其余留接口 |
| 壁纸浏览：Previous/Random/Next | `cyclePrevious/cycleNext/cycleRandom` | hepushell 壁纸目录扫描 + 切换命令 |

### 2.3 主题系统（Settings→theme）

| 视频效果 | Clavis 参考 | Hepu 落地 |
|---------|-----------|----------|
| matugen 动态取色（Light/Dark） | `matugen/` 取色引擎 + `Services/MatugenTemplateService` | 从壁纸主色提取主题色（取壁纸平均色/主色），写入现有主题配置（已有 `load_theme_from_config`） |
| 光标主题/大小 | Cursor theme（Bibata-Modern-Ice）+ Cursor size 24px | 现有光标绘制改为"主题光标"：定义光标图形表（指针/手/文本/忙），按主题加载 |
| 锁屏样式选择 | Lock screen: default / caelestia | 现有锁屏（时间位置 5 档已实现）增加"样式"字段，默认/卡片两套布局 |
| Super 键样式 | Super key appearance | 按键图标表（字体图标），可选样式 |

### 2.4 锁屏（快照 + 揭示动画 + 认证）

Clavis `Modules/Lock/`：冻结桌面快照（Screencopy）→ 模糊背景 → 点击时间区显示认证 UI（Escape 恢复）→ PAM 认证 → 解锁淡出。
与 Hepu 已定锁屏流程（点击时间溶解消失→头像+密码溶解出现→右下关机键/虚拟键盘→密码后溶解进桌面）一致。

**Hepu 落地**：
1. 锁屏时保留桌面帧缓冲副本（现有图形层已双缓冲，直接引用）；
2. 点击时钟区：时钟做溶解淡出（alpha 渐变，复用窗口切换器动画框架 `g_sw_anim`）；
3. 头像/密码框溶解淡入；
4. 认证：现有锁屏密码校验扩展（对接用户账户密码哈希）；
5. 右下角关机键/虚拟键盘：虚拟键盘=屏幕上的按键矩阵，点击注入键盘事件（模拟 `hepushell` 键盘输入路径）。

### 2.5 悬浮组件（音乐播放器 / 系统监控面板）

| 视频效果 | Clavis 参考 | Hepu 落地 |
|---------|-----------|----------|
| 悬浮音乐控件（歌曲+◀▶▶▶⟳×） | `Modules/Bar/Media/MediaBar.qml` + `Services/MediaManager` | hepushell 增加悬浮面板层（z 序高于窗口）：当前播放标题/进度 + 播放控制按钮（调用音频后端播放/暂停/切歌）；拖动可移动 |
| 系统监控悬浮面板（内存条/延迟/时间） | `Bar/SysMonitor` | 同 2.1 监控数据，悬浮窗口化渲染 |

### 2.6 多窗口体验（平铺/堆叠 + 圆角 + 毛玻璃质感）

视频中多窗口（Discord/浏览器/终端）并行、深色/浅色主题下窗口圆角与背景质感统一。
**Hepu 落地**：window64.c 窗口装饰统一走令牌圆角（2.1 的 tokens）；窗口背景分层（内容层 + 装饰层）；
毛玻璃效果 = 窗口背景对桌面帧做降采样模糊（软件实现，限激活窗口，性能优先时先做"半透明+噪点"近似）。

---

## 3. 开发优先级路线

| 优先级 | 模块 | 说明 | 预计成本 |
|--------|------|------|---------|
| P0 | 设计令牌 + 图标字体引擎 | 一切 UI 的地基；直接解决圆角/间距/图标三大痛点 | 中 |
| P0 | 顶栏分区模块化 + 网络分级图标 | 视频核心视觉；修复托盘/网络图标问题 | 小 |
| P1 | 锁屏流程完善（溶解动画 + 虚拟键盘） | 用户已定流程，Clavis 有成熟参照 | 中 |
| P1 | 壁纸布局 + Crossfade 过渡 | Settings→Wallpaper 落地 | 中 |
| P2 | 主题系统（壁纸取色 + 光标主题 + 锁屏样式） | 个性化核心 | 中大 |
| P2 | 悬浮音乐/监控面板 | 提升桌面完成度 | 中 |
| P3 | 每显示器壁纸 / Portal 等高级过渡 / matugen 完整取色 | 留接口，条件成熟再实现 | 大 |

**依赖关系**：P0（tokens+图标）→ P1（锁屏/壁纸）→ P2（主题/悬浮）→ P3（高级项）。
图标字体引擎是唯一横切依赖，建议最先做。

---

## 4. 附录：参考源码路径速查

| 功能 | Clavis 源码 |
|------|-----------|
| 顶栏装配 | `Modules/Bar/Bar.qml`、`HorizontalBarContent.qml` |
| 网络图标分级 | `Modules/Bar/QuickSettings/Network.qml` |
| 托盘 | `Modules/Bar/Tray/Tray.qml`、`TrayItem.qml` |
| 系统监控 | `Modules/Bar/SysMonitor/SysMonitor.qml`、`ResourcePie.qml` |
| 锁屏（快照+揭示） | `Modules/Lock/CaelestiaLock.qml`、`DefaultLock.qml`、`Cards/AuthCard.qml` |
| 壁纸服务 | `Services/AwwwWallpaperService.qml`（布局/过渡枚举） |
| 主题取色 | `matugen/`、`Services/MatugenTemplateService.qml` |
| 设计令牌 | `Common/Metrics.qml`、`Common/Typography.qml` |
| 字体图标 | `Components/MaterialSymbol.qml`、`Components/SvgIcon.qml`、`Components/ThemeIcon.qml` |
| 音乐媒体 | `Modules/Bar/Media/MediaBar.qml`、`Services/MediaManager.qml` |
| 控制中心（壁纸页） | `Modules/ControlCenter/ControlCenterWindow.qml`（Wallpaper/theme 分页） |
