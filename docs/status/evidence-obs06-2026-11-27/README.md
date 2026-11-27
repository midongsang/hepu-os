# OBS-06 / BUG-61 修复取证（2026-11-27）

- 分支：`fix/obs06-32bit-frame`（基于 `dbb5007`）
- 修复提交：`2ac2708`（仅 `kernel/gfx/gfx_vbe.c`）
- 构建：`HEPU_EV=/tmp/obs06clean bash tools/regress/build_all.sh`
- 32 位启动：`HEPU_EV=/tmp/obs06clean bash tools/regress/boot32.sh`
- 64 位回归：`HEPU_EV=/tmp/obs06clean bash tools/regress/boot64.sh`

## 结论

32 位 VBE 整帧提交的**目标基址**错了：`vbe_detect_from_boot_params()` 把
`vbe_fb_phys` 硬编码为旧 VGA LFB 窗口 `0xFD000000`；当前 bochs dispi 的 1920×1080
真实 BAR0 是 `0x80000000`（16MB）。8,294,400 字节的拷贝确实执行，但落在只可见约
3MB 的旧窗口，屏幕只剩顶部约 410 行。修复为取当前 `gfx_fb_addr`（BAR0），仅在地址
无效时回退旧窗口。

## 修复前后

| 项目 | 修复前 | 修复后 |
|---|---|---|
| 提交目标 | `0xFD000000`（旧 VGA 窗口） | `0x80000000`（dispi BAR0，16MB） |
| 32 位桌面 NONBLACK | 786,391 / 2,073,600（37.92%） | **2,073,600 / 2,073,600（100.00%）** |
| 顶部以下 | y=410 起纯黑 | 整屏有内容 |
| 连续 4 帧 | 均 37.92% | 均为 100% |

## 串口关键行（修复后）

```
[GFX] LFB phys=0x0x80000000
[BD-VBE] pci 0:2.0 bar0=0x0x80000000
[BD-VBE] bd_fb_phys=0x0x80000000
[GFX-VBE] detected from boot params: 0x00000780x0x00000438@0x00000020bpp
[EDESKTOP] res=0x00000780x0x00000438 desk_start rc=0x00000000
```

修复前的取证打印（临时代码，已清理）为：

```
[OBS06-PRESENT] dst=0xFD000000 phys=0xFD000000 bytes=0x7E9000 sentinel=0xDEADBEEF
```

改为 BAR0 后的取证打印：

```
[OBS06-PRESENT] dst=0x80000000 phys=0x80000000 bytes=0x7E9000
src3m=0xFFFAF6EE dst3m=0xFFFAF6EE src5m=0x000D3D50 dst5m=0x000D3D50
```

## 回归汇总（2026-11-27）

- `tools/regress/run_tests.sh`：主机端 ASan/UBSan 8/0 通过；图形栈边界 8 项全 ok。
- 32 位带盘冒烟：`serial=1 initrd=1 net=1 desk=1 fault=0 lines=755`，
  `ktest_result=PASS fail_suites=0 fail_cases=0`，p3-security 2/0。
- 64 位 UEFI：锁屏/登录/桌面链路全过，桌面 `NONBLACK=2,073,597/2,073,600`。
- 本修复不影响 64 位代码；64 位 `kernel64.bin` 仍为 1,023,696 B。

## 文件

- `after-desktop.png`：修复后 32 位桌面，1920×1080，NONBLACK 100%。
- `before-partial.png`：修复前同链路桌面，顶部深色、y=410 起纯黑。
- `pixel-after.txt` / `pixel-before.txt`：逐行像素与 NONBLACK 统计。
- `serial-after.txt`：修复后串口关键行。
- `serial-before-excerpt.txt`：修复前串口摘录（来自 2026-11-26 证据）。
- `sha256.txt`：本目录文件校验和。

## 边界

- 验证仅在 QEMU 6.2 + bochs dispi（`std-vga`，16MB VRAM，1920×1080@32bpp）完成，未上真机。
- 64 位 UEFI 回归同轮通过；32 位仅验证 UEFI 链路的桌面终态，未单独重跑全部 32 位冒烟项。
- 临时 `[OBS06-PRESENT]`、`[BD-VBE] virt` 取证打印已在最终构建前清理，最终镜像不含这些打印。
