# 壁纸五布局取证（2026-11-28）

本目录只回答一个问题：Settings→Appearance→Wallpaper 的
`Fill / Fit / Stretch / Tile / Center` 是否真的改变了壁纸像素，而不是只有
chip 高亮。截图由 `tools/regress/wallpaper_layouts.sh` 生成。

## 为什么用 1024×768

仓库内置壁纸全部是 16:9（`wallpaper.png` 512×288，`wallpaper-1/2.png` 1600×900）。
在 1920×1080 下，Fill / Fit / Stretch 对 16:9 源图会得到同一张满屏图，无法区分。
因此取证使用镜像副本 + `display.conf scale=100` + 引导配置 `1024x768`，即 4:3 屏；
同一张 16:9 壁纸在五种布局下产生五种可肉眼区分的几何。

## 五态语义与本次实际像素

源图 512×288，目标 1024×768：

| 状态 | 语义 | 本次结果 |
|---|---|---|
| `01-fill.png` | 等比放大，居中裁剪填满 | 1365×768，左右各裁 170 px |
| `02-fit.png` | 等比缩小，完整入屏，留背景边 | 1024×576，上下各 96 px 近黑边 |
| `03-stretch.png` | 直接拉伸到屏幕宽高，允许变形 | 1024×768，纵向拉伸 |
| `04-tile.png` | 原尺寸平铺重复 | 512×288 平铺，纵向 2.67 行 |
| `05-center.png` | 原尺寸居中，大图则居中裁剪 | 512×288 居中，四周近黑 |
| `06-reboot-center.png` | 二次启动读回 Center | `config.hsqml wallpaper_layout: 4` 生效 |

`Panorama` 不在本轮 UI；Clavis 文档里的 `Cover` 与 `Fill` 的居中裁剪语义合并，
当前五种是用户本轮确认的 `Fill/Fit/Stretch/Tile/Center`。

## 固定条件与复现

```bash
bash tools/regress/build_all.sh
bash tools/regress/wallpaper_layouts.sh /tmp/wallpaper-layouts
```

脚本会：

1. 复制 `$EV/build/hepu.img`，只改副本：ext2 根分区的 `/display.conf` 写入
   `scale=100`（`resolution` 行随文件保留，但内核不据此 SetMode）；真正生效的是
   FAT 区 `HEPBOOT.CFG` 第一处 `resolution=1920x1080`，改为等长的
   `resolution=1024x768 `（空格占位）。
2. 以固定 RTC `2026-11-27T12:00:00`、`-nic none`、VNC 5904 启动；
   解锁后 Settings 已打开。
3. 依次点 Layout chip（中心 x=463/517/571/625/679，y=395），每次截图；
   串口断言 `[WALLPAPER] layout=Fit/Stretch/Tile/Center`。
4. 停 QEMU 后从 ext2 分区读 `config.hsqml`，断言 `wallpaper_layout: 4`；
   再启动同一副本，断言 `[WALLPAPER] layout loaded=Center` 并截 `06`。

## 边界

- 这是 QEMU 合成环境，不是真机；4:3 只是取证分辨率，不改发行镜像。
- `06` 证明“写回 + 启动读回 + 渲染”，但不证明跨真实磁盘掉电一致性。
- 1920×1080 / UI scale 150 的固定态基线仍在
  `docs/status/desktop-visual-baseline-2026-11-27/`，两边用途不同。
