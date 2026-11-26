# OBS-06 复核证据（2026-11-26）

## 结论

32 位启动后，画面在顶部约 410 行保留深色，底部约 670 行纯黑，并持续到测试结束：

- `partial-frame.png`：`NONBLACK=786,391`；y=0..409 为 RGB(26,32,44)，y=410..1079 为纯黑。
- `last-frame.png`：约 20 秒后仍为 `NONBLACK=786,391`，没有恢复满屏。
- 串口确认已选中 32 位：`[BOOT] Mode: 32-bit i386`；并走到图形首帧：
  `[EDESKTOP] res=0x00000780x0x00000438`、`[EDESKTOP] first frame via registry`。

顶部 410 行对应 `1920 × 410 × 4 = 3,148,800` 字节，约 3MB，和
`kernel/gfx/gfx_vbe.c` 里旧注释“只提交前 3MB（约 402 行）”同量级。当前源码的
`vbe_present()` 已改为按 `gfx_pitch * gfx_height` 提交，但实测仍只覆盖 410 行；
根因没有闭环，不能把这次复核写成“已修复”。

## 复现

测试镜像为 `a5dab29` 构建的 `hepu.img`；ESP `HEPBOOT.CFG` 改为 `timeout=30`、
`default=0`，QEMU 启动后 7 秒按 `2` 选 32 位，连续 `screendump`。完整串口见
`serial-32-raw.txt`，关键行见 `serial-excerpt.txt`。

## 边界

本次只证明当前基线仍存在“32 位图形帧只覆盖顶部约 410 行”的现象；没有定位到具体
写者。原 OBS-06 记录的“加载屏被覆盖”与本次“首帧/提交只覆盖顶部”在现象上可能同源，
但需要 32 位 VBE/双缓冲提交链路插桩才能区分。本版按不修登记，保留为下一版本图形
后端项，不宣称已修复。
