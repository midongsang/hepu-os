# OBS-09 复核证据（2026-11-26）

## 结论

状态采样块不再每 30 帧无条件置 `g_content_dirty=1` 后，64 位默认 HPET 桌面在
真实鼠标移动下重新进入光标局部刷新路径：

```
[LOCK] login -> desktop (no account configured)
[LOCK] state: login -> desktop (unlocked)
[P0] cursor-only fast path active (x=984 y=552)
[P0] frames: redraw=233 skip=1567 skip_pct=87
[P0] cursor-only frames=82
[P0] dirty-region frames=0
```

`cursor-only frames=82` 出现在连续移动鼠标后的第一个 1800 帧统计窗口；停止移动后的
下一个窗口回到 0，属于没有鼠标事件的空闲窗口，不是路径失效。

## 测试条件

- 构建：`HEPU_EV=/tmp/hepu-obs9 bash tools/regress/build_all.sh`
- 镜像：`/tmp/hepu-obs9/build/hepu.img`
- 64 位默认 HPET，QEMU `-device usb-mouse`，VNC 仅用于观察，鼠标注入走 QEMU
  monitor 的 `mouse_move`；测试脚本没有成功用 VNC 拖动窗口，因此
  `dirty-region frames=0` 不能当成脏矩形路径的结论。
- 为进入桌面，测试镜像的 `/account.conf` 被临时改为 `user=dev`、`role=2`、无口令；
  这是测试镜像内的临时配置，不是产品默认，也不进入仓库。

## 文件

- `serial-64-default-raw.txt`：本次完整串口日志。
- `serial-excerpt.txt`：关键行摘录。
- `desktop-after-fix.png`：进入桌面后的截图，鼠标位于设置窗口上方。

## 边界

本次只证明“状态采样不再打断 cursor-only 局部刷新”。`dirty-region` 拖动路径仍未
独立注入验证；`g_frame_no % 300` 的独立兜底仍保留，用于漏标兜底，不属于本修复范围。

## 静态门禁

- `python3 tools/check_docs.py`：`warns=0 errors=0`
- `python3 tools/check_stage2_bss.py`：6/6 PASS
