# 桌面第 4 批取证（2026-11-28）

本目录回答第 4 批的三个问题：锁屏是不是“真实桌面冻结”而不是换一张壁纸、
登录/解锁动画是不是逐帧可观测、窗口是不是真的按 niri 语义横向成列。
截图全部由 `tools/regress/desktop_batch4.sh` 在 QEMU 中生成。

## 锁屏快照

进入锁屏时先从当前前台帧取 1/4 分辨率快照（1920×1080 → 480×270，约 518 KB），
做 4×4 盒式降采样、两遍 3×3 盒式模糊，再最近邻放大到全屏，最后叠暗色玻璃。
取帧前会跳过桌面大时钟面板的合成，因此锁屏上只有一只清晰时钟；侧栏收起后的
紫色短柄等真实桌面元素仍保留。

开机直接进锁屏时没有可冻结的桌面帧，回退为“壁纸 + 暗色玻璃”，不伪造快照。
这条路径在 `01-desktop.png` 之前的启动锁屏，不在本目录重复截。

| 文件 | 阶段 | 断言 |
|---|---|---|
| `02-lock-snapshot.png` | 桌面冻结后 | 可见模糊的 Settings、顶栏、天气卡；无第二只桌面时钟；中央为锁屏时钟 |
| `03-reveal-early.png` | 回车后揭示早期 | 时钟清晰，头像/输入框/Unlock 以低 alpha 浮出 |
| `04-login-final.png` | 揭示完成 | 登录 UI 完整，背景仍是同一张模糊桌面 |
| `05-unlock-fade.png` | 认证成功、桌面可见 52% | 桌面清晰，登录层为半透明残影；不是整层突然消失 |
| `06-desktop-after.png` | 淡出完成 | 回到原桌面，Settings 几何与锁屏前一致 |

`05` 不是按固定毫秒截的：`hs_lock_tick()` 在揭示进度到 52 时打印
`[LOCK] fade mid (desktop 52% visible)`，脚本等到本次启动的第 2 次该行再截图，
避免把“刚提交认证”误当成淡出中途。

串口关键行：

```text
[LOCK] auth ok, starting unlock fade
[LOCK] state: login -> unlocking (fade)
[LOCK] fade mid (desktop 52% visible)
[LOCK] state: login -> desktop (unlock fade done)
[LOCK] snapshot 480x270 blur=2
[LOCK] state: desktop -> lock (snapshot frozen)
[LOCK] state: lock -> login (key)
[LOCK] reveal done (login ui)
```

## niri 横向滚动列

列布局按窗口创建顺序一列一窗，列宽为屏幕 3/5（1920 下 1152），列间距和左右
边距为 `hs_s(8)`（150% 下 12），顶部从顶栏下沿 `hs_sy(52)`（78）开始，底部留
`hs_sy(88)`（132），列高 870。三列总宽超出屏幕，1920 下最大横向滚动约 1564 px。

- 每工作区独立保存当前/目标偏移，滚动按 `screen_width/20`（1920 下 96 px/帧）逼近。
- `hs_columns_ensure_visible()` 把焦点列完整滚进视口；F10 循环焦点后由视口中心列接管键盘。
- 顶栏左侧显示 `COL 当前/总数`，不占桌面额外空间。
- F9 在列布局与 Tiled 之间切换；Tiled 的工作区顶边改为按实际顶栏高度计算，切回后不再压住顶栏。

| 文件 | 断言 |
|---|---|
| `07-three-windows.png` | Settings、Files、Terminal 三窗已注册并可见 |
| `08-columns-3of3.png` | F9 后 `COL 3/3`，Terminal 在右，Files 左缘被视口裁切，证明列总宽超屏 |
| `09-columns-1of3.png` | F10 后 `COL 1/3`，Settings 完整回到视口，Files 从右缘进入 |
| `10-columns-2of3.png` | F10 后 `COL 2/3`，Files 居中完整，Settings 右侧被裁 |
| `11-columns-3of3.png` | F10 后 `COL 3/3`，Terminal 完整，Files 右侧被裁 |
| `12-tile-back.png` | 再按 F9 回 Tiled，三窗按 2/3 + 1/3 重新排布，顶栏可见 |

串口关键行：

```text
[WM] layout=Columns
[WM] columns focus 1/3
[WM] columns focus 2/3
[WM] columns focus 3/3
[WM] layout=Tiled
```

## 图标体系

本轮建立单色语义矢量图标入口 `hs_glyph_t` / `hs_draw_icon(x, y, size, color, glyph)`，
按目标尺寸实时描线段、圆环、多边形和圆，不缩放 32×32 位图。已迁移：

- 顶栏：电源、齿轮、扬声器、Wi‑Fi。
- 控制中心：Wi‑Fi、蓝牙、AirPlay、DND、定位、手电筒六个开关。

Dock 8 个彩色应用图标、应用抽屉 12 个应用图标、电池图标仍是原有位图或专用
绘制。这不是“全量图标字体管线已完成”，只是系统单色语义层先统一。

## 32 位兼容修复

顶栏扬声器是 32 位桌面第一次真正调用 `fill_polygon`。首个 32 位启动卡在
`[HPD] wallpaper done` 之后、`panels done` 之前；逐步打点定位到
`HS_GLYPH_SPEAKER`。根因不是图标算法，而是 `kernel/shared/desktop/desk_surface.c`
按两数组 `(xs, ys, n, color)` 调用 `gfx64_fill_polygon`，32 位
`gfx64_compat.c` 却实现成单交错数组 `(pts, n, color)`；链接期不查类型，
运行时 `n` 实际拿到 `ys` 指针，扫描线数量爆炸并撞进 poison page。

现已把 32 位实现改为两数组签名。修复后 `smoke32.sh --disk 35` 通过：
`ktest_result=PASS`、`p3-security: passed=2 failed=0 skipped=0`、
`ext2_mounted=1`。这条不是“32 位图形没坏”，而是第 4 批图标迁移顺带暴露并修掉的
真实兼容缺陷。

## 构建与复现

```bash
bash tools/regress/build_all.sh
bash tools/regress/desktop_batch4.sh /tmp/desktop-batch4
```

脚本固定 1920×1080、UI scale 150%、`-rtc base=2026-11-27T12:00:00`、`-nic none`，
用 fixture 账号完成两次解锁，再打开 Files/Terminal、按 F9/F10 取列布局证据。

## 边界

- 这是 QEMU 合成环境，不是真机；没有 AC'97，Volume 仍只能 `N/A`，电池状态不由
  本批证明。
- 列布局的滚轮入口已接在 `hpdesk_update_wheel()`，但 QEMU HMP 没有可用的横向滚轮
  注入路径，串口没有 `[WM] columns scroll` 行。本目录只证明 F9/F10 焦点滚动，
  不声称滚轮已验证。
- 锁屏快照是内存中的 1/4 帧，不写盘；跨重启不保留，也不证明掉电一致性。
- 快照取当前前台帧；光标由呈现层单独叠加，不写入快照。
