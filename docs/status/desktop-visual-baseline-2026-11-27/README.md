# 桌面固定态视觉基线（2026-11-27 建立，150% 历史对照）

> 2026-11-28 起 UI 默认缩放改为 100%，当前基线改用
> `docs/status/desktop-visual-baseline-2026-11-28-100pct/`。本目录仅保留 150% 历史截图。

本目录是桌面视觉批次的像素基线，不是功能验收的全集。全部截图由
`tools/regress/desktop_visual.sh` 生成；第二次独立复跑与基线比较为
`mean=0.000`、`changed>16=0.0000%`，9 张全部 `OK`。

2026-11-28 第 3 批更新：Settings Appearance 增加 Wallpaper Layout 五 chip，
窗口高度 430→470 逻辑值；因此除锁屏外的 8 张基线同步更新。布局本身在
4:3 分辨率单独取证，见 `docs/status/wallpaper-layouts-2026-11-28/`。

2026-11-28 第 4 批更新：顶栏 power/gear/speaker/Wi-Fi 与控制中心
Wi-Fi/BT/Air/DND/Loc/Torch 六开关改为同一套矢量 glyph；9 张基线全部复采。
第二遍独立复跑比较：`01-lock` 仅时钟胶囊区域 mean=0.028，其余 8 张 mean=0.000。

## 固定条件

- QEMU 1920×1080，UI scale 150%，`-vga std`
- `-rtc base=2026-11-27T12:00:00`
- `-nic none`（无网络）
- 无 `-device AC97`（无音频硬件）
- 锁屏使用 fixture 账号解锁，欢迎通知消失后再截桌面
- 截图前把指针移出 Dock 触发区；`08-dock` 单独把指针压到底部，验证悬停态

## 状态清单

| 文件 | 断言 |
|---|---|
| `01-lock.png` | 壁纸暗色锁屏、大时钟、日期、无假按钮文案 |
| `02-desktop.png` | 顶栏 32 逻辑值 / 48 px、Settings Appearance、天气空状态为 `Weather / No network` |
| `03-drawer.png` | 应用抽屉 12 个真实注册应用，两行应用名，图标不是字母占位 |
| `04-control-center.png` | Quick 页：Wi‑Fi/BT/Air/Loc/Torch 为 `N/A`，DND 高亮，Volume/Brightness 无滑轨且给出 `No AC'97 device / No backlight driver` |
| `05-settings.png` | 关闭控制中心后 Settings Appearance 保持可见；Wallpaper 卡内 Layout 五 chip（Fill/Fit/Stretch/Tile/Center），默认 Fill |
| `06-devices.png` | Devices 页六行真实状态：Ethernet/Audio/DND/Wi‑Fi/Bluetooth/Backlight |
| `07-audio.png` | Audio 页：两个禁用滑块、DND、Torch；无越界长文案 |
| `08-dock.png` | Dock 悬停：8 个真实应用图标，指针悬停项放大 |
| `09-weather.png` | 侧栏 Weather 标签无网络时显示 `No current reading`，不出现硬编码 26°C / 65% / 五天预报 |

## 复现

```bash
bash tools/regress/build_all.sh
bash tools/regress/desktop_visual.sh /tmp/desktop-visual \
  --baseline docs/status/desktop-visual-baseline-2026-11-27
```

只重新生成基线：

```bash
bash tools/regress/desktop_visual.sh /tmp/desktop-visual --update-baseline
```

`compare_visual.py` 会屏蔽顶栏时钟胶囊与右下天气卡的动态文本区域，其余区域按
平均绝对差 ≤ 2.0、变化像素占比 ≤ 0.8% 判定。

## 边界

- 这是 QEMU 合成环境：`Volume` 为 `N/A` 是因为启动参数没有 AC'97，`Ethernet`
  显示 `No driver` 是因为 `-nic none`；不能把这两条写成真机已验证。
- 本目录固定 16:9，内置壁纸也是 16:9，因此 Fill/Fit/Stretch 三态在几何上等价；
  五布局的差异取证放在 `docs/status/wallpaper-layouts-2026-11-28/`（1024×768）。
- DND 的点击/关闭已由串口断言覆盖；通知弹窗抑制的视觉前后对比不在本目录。
- DND 已改为矢量 glyph；Dock 8 个彩色应用图标、应用抽屉 12 个图标、电池图标仍是
  位图或专用绘制，全量图标字体管线尚未完成。
