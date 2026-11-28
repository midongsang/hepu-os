# 桌面固定态视觉基线（2026-11-28，100% 缩放）

本目录是 UI 缩放默认值从 150% 收回 100% 后的像素基线，替换
`desktop-visual-baseline-2026-11-27/` 作为当前 100% 回归基准。旧目录保留为
150% 历史基线，不再作为默认档位比较目标。

## 固定条件

- QEMU 1920×1080，UI scale **100%**（`build_all.sh` 注入 `/display.conf scale=100`）
- `-rtc base=2026-11-27T12:00:00`
- `-nic none`（无网络）
- 无 `-device AC97`（无音频硬件）
- 锁屏使用 fixture 账号解锁，欢迎通知消失后再截桌面
- 截图前把指针移出 Dock 触发区；`08-dock` 单独把指针压到底部，验证悬停态
- 顶栏 power/gear/speaker/Wi‑Fi 使用用户提供的 `assets/icons-status64` 同源 64×64 位图；
  控制中心 Wi‑Fi 同源，不再是手绘矢量。QEMU 无 ACPI 电池，电池槽不占位

## 状态清单

| 文件 | 断言 |
|---|---|
| `01-lock.png` | 壁纸暗色锁屏、大时钟、日期、无假按钮文案 |
| `02-desktop.png` | 顶栏 32 逻辑值 / 32 px、Settings Appearance、天气空状态为 `Weather / No network` |
| `03-drawer.png` | 应用抽屉 12 个真实注册应用，两行应用名，图标不是字母占位 |
| `04-control-center.png` | Quick 页：Wi‑Fi/BT/Air/Loc/Torch 为 `N/A`，DND 高亮，Volume/Brightness 无滑轨且给出 `No AC'97 device / No backlight driver` |
| `05-settings.png` | 关闭控制中心后 Settings Appearance 保持可见；Wallpaper 卡内 Layout 五 chip，默认 Fill |
| `06-devices.png` | Devices 页六行真实状态：Ethernet/Audio/DND/Wi‑Fi/Bluetooth/Backlight |
| `07-audio.png` | Audio 页：两个禁用滑块、DND、Torch；无越界长文案 |
| `08-dock.png` | Dock 悬停：8 个真实应用图标，指针悬停项放大 |
| `09-weather.png` | 侧栏 Weather 标签无网络时显示 `No current reading`，不出现硬编码 26°C / 65% / 五天预报 |

## 复现

```bash
bash tools/regress/desktop_visual.sh /tmp/hepu-visual-100
bash tools/regress/desktop_visual.sh /tmp/hepu-visual-100-rerun \
  --baseline docs/status/desktop-visual-baseline-2026-11-28-100pct
```

`compare_visual.py` 只屏蔽时钟/天气动态文本。2026-11-28 顶栏图标改用用户
`assets/icons-status64` 位图后，按本目录重新采集并独立复跑：9 张全部 `OK`；
`03/04/05/07/08/09` 的 `mean=0.000`，`01-lock` 为 `mean=0.015`（174 px）、
`02-desktop` 为 `mean=0.028`（520 px）、`06-devices` 为 `mean=0.014`（207 px），
均为指针位置/动态文本差异，顶栏与控制中心图标区无差异。
