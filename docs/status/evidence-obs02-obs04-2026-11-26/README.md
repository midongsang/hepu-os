# OBS-02 / OBS-04 观感修复（2026-11-26）

状态：OBS-02 resize 场景已闭环（边缘拖拽缩放 + 4/5/6 光标）；no-drop 代码路径已接；
wait 形态已装载但还没有真实长任务触发点。OBS-04 已闭环（窗口外 1px 高光边）。

## 1. OBS-02：光标形态与边缘缩放

### 1.1 改了什么

- `cursor64.c`：`CUR_KIND_MAX` 从 4 扩到 9；Modern 映射补齐
  `size-hor/size-ver/size-diag/wait/not-allowed`，槽位约 81KB。
- `hpdesk.c`：新增 `hs_window_edge_flags()`；浮动窗口四边/四角 4px 抓取带内
  返回 `CUR_SIZE_H(4)` / `CUR_SIZE_V(5)` / `CUR_SIZE_D(6)`。
- 新增真实边缘缩放：`hs_window_resize_begin_at/move/end()`，支持左/右/上/下/四角，
  最小 320×200，并钳制在桌面工作区内，不把标题栏推入顶栏。
- 右上角三个窗口按钮区域不参与顶边缩放，避免按钮点击被 resize 抢走。
- `hs_apps.c`：Files 拖拽时，有效落点返回 move(3)，非法落点返回 not-allowed(8)。
- Settings 内容块在加宽后水平居中，避免缩放后右侧大片空白。

### 1.2 实测

最终镜像 `/tmp/hepu-visual5/build/hepu.img`，默认 HPET 64 位 UEFI，
1920×1080、150% UI 缩放。Settings 初始 690×645，位置 (615,180)。

串口（`obs02-serial.txt`）：

```
[CURSOR] kind=4 at 1305,499
[CURSOR] kind=6 at 1305,824
[RESIZE] begin win=0 edges=10 at 1305,824 size=690x645
[RESIZE] end win=0 size=840x750
[CURSOR] kind=5 at 1035,929
```

- 右边缘：kind=4（`size-hor`）。
- 右下角：kind=6（`size-diag`），edges=10（右+下）。
- 拖拽后窗口 690×645 → 840×750，内容随宽度重新居中。
- 下边缘：kind=5（`size-ver`）。

截图：

- `obs02-edge-right.png`：右边缘 resize 光标。
- `obs02-edge-corner.png`：右下角斜向光标。
- `obs02-corner-resized.png`：斜向拖拽后的 840×750 窗口。

### 1.3 仍未闭环

- `wait`（kind=7）素材已装载，但当前没有可观测的长任务状态机，未做真实忙态触发。
- `not-allowed`（kind=8）只在 Files 拖拽非法落点返回；本轮没有做 GUI 拖拽取证，
  不以代码路径代替运行时证据。
- 其余 Modern 形态（help/hourglass/cross/pen/sb_up 等）仍未接场景。

## 2. OBS-04：深色壁纸窗口分离度

### 2.1 改了什么

在浮动窗口投影之后、窗口本体之前，画一圈 **窗口外 1px、alpha=20/255 的白色高光边**。
不覆盖窗口本体，不依赖壁纸取色，只在 `window_shadow` 开启时生效。实现为
`hs_stroke_round_blend()` + `hs_window_outer_highlight()`。

### 2.2 像素级对照

同一默认壁纸 `#103F50`、同一 Settings 窗口位置，改动前镜像
`/tmp/hepu-obs01/build/hepu.img` 与最终镜像 `/tmp/hepu-visual5/build/hepu.img`：

| 采样点 | 改前 RGB | 改后 RGB | 结论 |
|---|---|---|---|
| 右边缘 x=1305,y=300 | (21,40,44) | (39,56,60) | 外沿亮约 18 阶 |
| 右边缘 x=1305,y=500 | (28,44,45) | (45,60,61) | 外沿亮约 17 阶 |
| 右边缘 x=1305,y=700 | (33,47,46) | (50,63,62) | 外沿亮约 17 阶 |
| 紧邻背景 x=1306,y=500 | (28,45,46) | (28,45,46) | 只改 1px，不扩散 |
| 顶边 y=179,x=900 | (8,34,43) | (27,51,59) | 顶边同样亮约 19 阶 |
| 窗口边框 y=180,x=900 | (201,169,110) | (201,169,110) | 窗口本体不变 |

完整采样见 `obs04-pixel-compare.txt`。截图：

- `obs04-before.png`：改动前，深色壁纸上窗口边界与背景接近。
- `obs04-after.png`：1px 外高光后，窗口外沿可辨，内部 UI 不变。

## 3. 构建与回归

- 构建：`HEPU_EV=/tmp/hepu-visual5 bash tools/regress/build_all.sh`，64/32 均成功。
- 64 位：默认 HPET 到锁屏、VKB 登录到桌面，Settings 窗口和 resize 实测通过。
- 文档检查、入口/栈断言与 32 位冒烟的最终输出见同目录 `check_docs.txt`、
  `check_stage2.txt`、`smoke32-*.txt`。

## 4. 证据文件

| 文件 | 内容 |
|------|------|
| `obs02-edge-right.png` | 右边缘 kind=4 光标 |
| `obs02-edge-corner.png` | 右下角 kind=6 光标 |
| `obs02-corner-resized.png` | 斜向拖拽后 840×750 |
| `obs02-serial.txt` | `[CURSOR] kind` 与 `[RESIZE]` 原始行 |
| `obs04-before.png` / `obs04-after.png` | 同位置改前/改后截图 |
| `obs04-pixel-compare.txt` | 外沿/背景/本体逐点 RGB |
