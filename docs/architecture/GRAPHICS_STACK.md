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

### 1. 脏矩形 —— 两步已落地（光标局部刷新 + 窗口拖动局部重绘）

`hpdesk_render()` 每帧把整个桌面（含所有窗口）重画一遍，再整帧 blit：
1920×1080×4B ≈ **8MB / 帧**，还要和 8MB 帧拷贝抢锁。60fps 靠限速硬压（desktop64.c
约 16ms 一次），输入延迟与它解耦过一轮，但掉帧风险在这儿。
**已落地（V0.59.4）**：无非内容变化、无动画、右键菜单未展开时，鼠标移动只做
`gfx64_present_region()` 两次（擦旧光标 + 铺新背景）+ 前台画光标，不再重画壁纸/全部窗口、
不再整帧拷 8MB。实测 30 次连续移动后画面无拖影（暗像素单连通块 20×20）。
**第二步（V0.59.5）**：窗口拖动也走局部。脏矩形框架 `hs_dirty_add/hs_dirty_rect/
hs_dirty_hits`（单矩形并集），拖动时并入"旧位置 ∪ 新位置"（外扩 `hs_s(96)` 覆盖投影）；
`hs_wallpaper_render_rect()` 只铺脏区壁纸；`hs_window_render_all()` 跳过与脏区不相交的
窗口（省下文字栅格）；`present_region` 只提交脏区。
实测：内核注入自测拖 28 帧，`dirty_region_frames=26`（另 2 帧是每 30 帧一次的强制全量），
窗口精确右移 280px，旧位置像素检查无残留。

**仍缺**：内容变化（时钟跳秒、15s 一次的状态刷新、通知弹出）仍标记整帧
（`g_content_dirty` 是 bool）。升级它需要各 render 回调上报自己的矩形，
属于"组件库上报脏矩形"那一层的工作，留给控件基类一起做。

### 2. Surface —— 窗口目前没有自己的画布

现在所有窗口**直接画到同一个后台缓冲**。后果：
- 窗口做不了 `opacity`（没有可混的独立层）；
- 做不了裁剪（没有可裁的独立区域）；
- 动画期间整窗重画（没有上一帧缓存可比对）；
- App 画错坐标直接污染整屏（`render` 回调拿的是全局 framebuffer）。

Surface 是三次模型里最"地基"的一项，Wayland/X11/Windows 都是这个模型。

### 3. 控件基类 —— 布局层已落地（hs_ui + Settings 试点）

`hs_widget_t`（hpdesk.h:81）只是**面板上的小部件**（CPU/内存指示条那种），
不是按钮/输入框的控件基类。终端、文件管理器、任务管理器各自用 `gfx64_fill_rounded_rect`
+ `font64_draw_string` 手拼 UI —— 视觉能对齐全靠人肉对常量。
上一轮「侧栏 18px vs 顶栏 15px」「通知卡右边越界」两个对齐问题，根因就是没有统一布局容器。

**第一步已落地（V0.59.6）**：`hs_ui.h/.c` 提供 `hs_ui_box_t`（矩形 + 内边距 + 子项间距
+ 排列方向 + measure/paint 回调 + `hs_ui_hit()` 命中）。布局是单遍
`arrange → paint`，不做约束求解。Settings 左侧 5 项导航已迁到容器：
原来位置是 `cy + i * SETK(32)`（间距常数直接写在循环里），现在由容器算，
paint 回调只拿到最终矩形。实测 5 项落在 234/282/330/378/426（间距 48 = 28+gap 4），
与旧实现逐像素一致；改间距只需改一处。

### 4. Surface —— 第一步已落地（拖动快照，V0.59.7）

完整 Surface（每窗口常驻离屏缓冲 + 绘制目标隔离 + 裁剪）的代价先说清：
`gfx64` 的写像素路径**不统一** —— `fill_rect`(333)、`blit_to`(95)、`blit_background`(147)、
`put_pixel`(299)、`fill_rounded_rect`、`draw_rounded_rect`、`blur_region` 等十几个函数
各自 `uint8_t *dst = gfx_target ? gfx_target : front_buffer;` 之后直接写指针。
要做「窗口本地坐标 (0,0) + origin 偏移 + clip」，这十几处都要加变换与裁剪，
而且 `font64_draw_string(fb,w,h,...)` 还单独持有 fb 指针。改动面 15-20 处、验证面覆盖所有界面。

所以先取能吃到真实收益的切口：**拖动期间的内容快照**。拖动时窗口内容不变，
而"重画窗口内容（尤其文字栅格）"正是拖动帧里最贵的一段。

落地：
- `gfx64_back_snapshot/back_restore`（gfx64.c :96/:117）：从后台缓冲抓一块矩形到调用方缓冲，再贴回。
- `hs_window_t` 新增 `surface/surface_w/surface_h`（hpdesk.h :219）：只在拖动期间存在，结束即释放；
  分配失败为 0，自动退回"每帧重画"的老路径。
- `hs_window_drag_start()` 末尾抓快照（此时后台缓冲保存的是上一帧完整画面），
  `hs_window_drag_end()` 释放。
- `hs_window_render_all()`：`g_dragging && g_drag_window == w && w->surface` 时
  画完投影直接 `gfx64_back_restore` 并 `continue`，**跳过内容重画**。

实测（拖动自测 28 帧，窗口 615,180 → 895,180）：`dirty_region_frames=27`，
窗口内容（5 项导航、色板、全部文字）完整贴回，无残影。回归 1/1，内核 915296 B。

**已知副作用**：快照矩形四角是"抓取那一刻的背景"，贴到新位置后四角有极小块旧背景。
壁纸是同色系渐变，实测不可辨；若要彻底干净，需给快照加圆角 alpha 遮罩（未做）。

### 5. 裁剪 —— 已落地（V0.59.9）

`gfx64` 新增统一裁剪矩形（`gfx64_clip_set(x,y,w,h)` / `gfx64_clip_disable()`），
**只作用在四个叶子函数上**：`put_pixel` / `get_pixel` / `fill_rect` / `blend_pixel`。
圆角、直线、圆、TTF 文字最终都落到这几个入口（AA 与半透明走 `blend_pixel`），
所以上层绘制函数不必各自处理 —— 这是"渲染原语统一"第一次有了单一来源。

接入点：`hs_window_render_all()` 在调用 `w->render(w)` 前把裁剪设为窗口矩形，
之后立刻关闭。窗口内容不再可能画到窗口外。

**确定性验证**（自测已从树里移除）：在裁剪生效的上下文中，从窗口底边往下
画一条 `w->width+120 × 60` 的品红条，串口打印 `[CLIPTEST] drew 0xFF00FF bar below win bottom`；
截图逐像素扫描（1920×1080 原始 PPM，步长 2）**品红像素 = 0 个** —— 绘制确实执行了，
但越界部分被完整裁掉。

**顺带修掉一个真 bug**：终端 `t_put()` 加入新行后没有置脏。桌面处在
「无脏区 → 跳帧」状态时，输出整屏日志（如 `dmesg`）后画面一直停在旧帧，
表现为**终端窗口一片空白**。现在 `t_put` 末尾调 `hpdesk_mark_dirty()`，
实测终端内容正常刷新（见 `docs/assets/window-clip.jpg`）。

### 6. alpha 合成 —— 已落地（V0.59.10）

新 `gfx64_blend_image(void *src, int sw, int sh, int dx, int dy, int opacity)`：
把 32bpp **ARGB** 源图按每像素 alpha（再乘整体 `opacity`）混合到当前渲染目标，走裁剪。
像素格式与 gfx64 内部一致（内存 32 位值 `0xAARRGGBB`）。

**为什么必须先有它**：gfx64 原有绘制全是覆盖式写实色，没有 alpha 通道。窗口一旦
画进自己的 buffer，圆角外的像素就没有"透过"语义 —— 不是脏数据就是黑色。
合成器必须有办法把"带透明的窗口"贴到屏幕上，这个入口就是那件事。

**数值验证**：128/255 的红块叠到背景 `0xFAF6EE` 上，实测 `after=0xFC7A76`
（R=252 / G=122 / B=118），与理论 `255*128/255 + 250*127/255 = 252`、
`246*127/255 = 122`、`238*127/255 = 118` 逐通道吻合。视觉见 `docs/assets/alpha-blend.jpg`
（粉红块不是纯红，半透明感正确）。

### 7. 窗口 surface —— 已落地（V0.59.11）

三块改动合起来，窗口第一次真正"画进自己的画布再合成"：

1. **gfx64 渲染目标上下文**（`gfx64_target_begin(buf,w,h,ox,oy)` / `gfx64_target_end()`）：
   目标指针 + origin 平移 + 自身 pitch。上层仍用屏幕绝对坐标，写入时减 origin 落进缓冲。
   只作用在四个叶子函数（put_pixel/get_pixel/fill_rect/blend_pixel），所以窗口内容
   （圆角、线、圆、图标、AA 文字）全部自动跟随，调用点一行没改。
2. **font64 适配**：`draw_char` 入口检测 surface 模式，忽略调用方传的屏幕 framebuffer，
   改指向 surface + 平移坐标。`draw_string` 与所有调用点零改动。
3. **窗口底色**用新 `gfx64_fill_round_rect_argb()` 写入：边缘像素写 **coverage alpha**
   而不是与背景混色 —— 圆角外是真透明（alpha=0），合成时透出壁纸。
   旧 `fill_round_rect_aa` 是把边缘与目标已有内容混色，目标是透明 buffer 时会混出灰边，
   所以这一路必须换掉。

合成：`gfx64_blend_image(surface, w, h, win->x, win->y, 255)`，透明度由每像素 alpha 决定。

**踩到的两个坑（都已修）**：
- `blend_pixel`/`put_pixel` 在 surface 模式下最初没写 alpha 字节（高字节留 0）→ 整块被
  合成器当透明，表现为"窗口按钮和色板圆点消失、露出壁纸"。修法：surface 内一律写
  `0xFF000000 | color`，透明只由"圆角外空白"承担。
- `fill_rounded_rect` 走的是 vg64 矢量管线，但 vg64 最终落到 `gfx64_blend_pixel`，
  所以只要叶子函数适配了它也跟着适配 —— 不用单独改。

视觉与迁移前**逐点比对**（1920×1080 原始 PPM，步长 2，窗口区域）：差异仅 31 个采样点，
集中在右下圆角 1px 抗锯齿过渡带，属预期（AA 数学由"混背景"改为"写 coverage"）。

**代价（要如实记）**：每帧每窗口多一次 memset + 一次全窗 blend，窗口内容绘制开销约翻倍。

### 8. 窗口级脏标记 —— 已落地，但收益不及预期（V0.59.12）

`hs_window_t` 增加 `content_dirty`，外加 `hover_rel_x/y`、`hover_inside`。
关键设计：**鼠标相对窗口的位置变了才置脏，绝对位置变化不算** —— 拖动时两者相同，
所以拖动帧理论上能命中"免重画"。命中时直接
`gfx64_blend_image(surface, ...)` 贴上一帧结果，跳过 memset + 全部绘制。

实测（拖动自测 28 帧，窗口 690×645）：

```
dirty_region_frames=27 surface_reuse=16 surface_repaint=38
```

复用只有 16 帧（约 57% 的拖动帧命中了免重画），但**整机拖动帧耗时没有下降，
反而约为迁移前的 2 倍**（拖动 28 帧从 <10s 变成 >25s 才跑完）。

这说明瓶颈**不在窗口内容绘制**。省掉绘制之后仍然慢，增量只能来自每窗口那次
整窗 blend（445K 像素 × 每帧）+ 脏区 present 到 VGA MMIO。

**结论如实记**：窗口级标记把"重画"这件事做对了，但没有解决性能问题。

### 9. 分段计时基础设施（V0.59.13，代码已就位，数据未取到）

渲染链里埋了 rdtsc 计时累加器：

| 累加器 | 位置 | 含义 |
|---|---|---|
| `g_cyc_panel` | `hpdesk_render()` 包装 | 整帧 |
| `g_cyc_render_all` | `hs_window_render_all()` 首尾 | 窗口渲染段 |
| `g_cyc_blend` | 两处 `gfx64_blend_image` | surface 合成段 |
| `gfx64_prof_present` | `gfx64_present` / `present_region` | 上屏段 |

`hpdesk_render()` 拆成 `hpdesk_render_inner()` + 计时包装，后续任何优化都能直接看增量。

**数据没取到，原因如实记**：QEMU 里桌面的主循环会被同步 HTTP 请求（Weather/驱动仓）
阻塞十几秒，渲染调用到 45 帧（拖动自测的触发点）的时间完全不可控 —— 试了 8s / 12s /
20s / 22s / 30s / 40s 都没有稳定到达。每帧打印更是把串口写爆，反而拖慢主循环。

**所以现在不能拿帧率换算"渲染耗时"** —— 之前"拖动 28 帧从 <10s 变成 >25s"的对比里，
帧率本身就被主循环阻塞污染了，这个 2 倍的结论**不可靠**。

### 10. 取证尝试与失败记录（V0.59.14）

为了拿到第 9 节的计时数据，做了三件事：

1. **缩短天气请求的上包等待**：`http64_get` 抽出 `http64_get_timeout(url, resp, max, timeout_s)`，
   天气传 3 秒（原固定 15 秒），驱动仓下载仍走默认 15 秒不会被截断。
2. 把自测触发帧号从 45 降到 5，试图绕开启动期阻塞。
3. 试过 8/12/20/22/30/40 秒多个等待窗口，`-nic none` 与 `-nic user` 都试。

**结果**：`[GEOM-RENDER] frame=60` 证明渲染确实跑到了 60 帧，但自测块
（`[ST] fno=` 探针）**一次都没执行**，拖动与分段耗时数据依然没拿到。

**这件事的教训**：现在缺的不是计时能力，是一个**不依赖帧号、不依赖启动时序**的取证入口。
下次应该直接在 `rootfs/etc/hepushell/autostart.cmd` 里起终端跑命令做一次性统计，
而不是继续调 sleep 时长。

### 11. 把同步网络请求挪出启动路径（V0.59.15）

两处同步 HTTP 在桌面启动路径上：

1. **驱动仓索引**（`drv_mgr_update` → `http64_get`，默认 15 秒超时），而且它被调在
   **渲染函数里**。网络一就绪，那一帧就被卡住 15 秒。
2. **天气**（`desktop64.c` 主循环，每 `wx_tick >= 300` 轮触发，还带 3 次重试）。

改动：
- `drv_mgr_update` 改用 `http64_get_timeout(..., 3)`；调用点加 `g_frame_no > 1800`，
  错开桌面首屏。
- 天气改为 `wx_tick >= 60000`、只试 1 次。
  **注意 `wx_tick` 数的是主循环轮数不是帧数**（一轮约 1ms），所以 300 轮只有约 0.3 秒 ——
  这就是"启动后立刻发请求、立刻卡住"的真正原因；按墙钟算 60000 轮 ≈ 60 秒。

实测：启动后 10 秒内到 `frame=60`（改前要 20–30 秒），且这 10 秒里没有任何天气/驱动仓
请求；天气卡片显示 `--` / `N/A` 占位（本来就有占位逻辑）。

**下一步该做的不是再猜性能**：天气/驱动仓的同步 HTTP 已经确认会堵主循环（这次缩短了
天气那一路），真正的解法是把它们挪出主循环（后台线程或状态机分帧推进）。这件事的收益
比"优化合成"更直接，而且是桌面卡顿的已知主因。

**仍缺**（这才是完整 Surface）

**仍缺**（这才是完整 Surface）：
1. 绘制目标隔离 —— 窗口内容直接画进自己的 buffer，而不是先画屏幕再抓；
2. 裁剪 —— clip 到窗口矩形（现在靠各绘制函数自觉不出界）；
3. 常驻 surface + 合成 —— 现在只有拖动期间有快照，静止窗口仍每帧重画。

**仍缺**：控件树事件分发（现在点击仍由 hpdesk 的输入处理按坐标判定）、
控件级脏区上报。这两项要与 Surface 一起做，否则脏区无法从控件流到合成器。

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
