# 性能基线（A-1 / A-2，2026-11-18 实测）

## A-1 启动基准（5 次连测）

口径：`kernel_main_64.c` 的 `boot_mark`，每个 `[INIT] phase` 前读 TSC 周期。
**只做相对比较，不换算绝对秒数**（TCG 软件模拟，硬件上绝对值无意义）。

| Phase | 中位数（周期） | min | max | 备注 |
|---|---|---|---|---|
| cores | 6.57M | 6.36M | 6.83M | 稳定 |
| hw_driver | 4.49M | 4.00M | **67.8M** | 有 15 倍离群 |
| security | 239.5M | 235.6M | 244.7M | 稳定 |
| fs | 276.8M | 272.3M | 283.9M | 稳定 |
| net | 389.1M | **16.9M** | 398.0M | 有 23 倍离群 |
| sys_service | 401.2M | **18.8M** | 404.9M | 有 21 倍离群 |
| app_framework | **2049.8M** | 1872.4M | 2198.9M | 最大头 |

**结论**：
1. `app_framework` ≈ 2.05G 周期，是最大单项，启动优化应从这里开始。
2. `hw_driver` / `net` / `sys_service` 三个 phase 在 5 次里出现 15–23 倍离群——
   说明**启动过程存在非确定慢路径**（候选：中断乱序、重试、设备探测超时）。
   这不是"跑得慢"，是"有时候特别慢"，稳定性视角比性能视角更值得查。

## A-2 帧率 P99（稳态采样）

```
[PERF] intervalK=0000E571 renderK=000099A3 presentK=00001E27 ratio=00000042
       p99K=0008BF4F maxK=0008E4EC
```

| 指针 | 值 | 相对 |
|---|---|---|
| 平均帧间隔 | 0xE571 ≈ 55.9M 周期 | 基准 |
| **P99 帧间隔** | 0x8BF4F ≈ 546k 周期 | **≈ 平均的 9.8 倍** |
| Max 帧间隔 | 0x8E4EC ≈ 566k 周期 | 与 P99 同量级 |

**结论**：平均值把卡顿抹平了，P99 是平均的 9.8 倍——桌面存在周期性顿挫。
只看平均帧率会漏掉这个问题，这是加 P99 埋点的主要价值。

## 未完成

- **A-3 `sysprof`**：命令已实现（`shell64.c:3289`），但需要进 guest 敲命令。
  串口 shell 通路已接通（`shell64.c` 输入源 + `kernel_main_64.c` 桌面旁路），
  但 pty 往返验证未完成（`-serial pty` 下输入输出同一条通道）。
- **present memcpy 收益**：`a50a4b1` 把 `blit_to` 改成整行 memcpy，缺前后 `presentK` 对比。

## 复现

```bash
# A-1：5 次启动，取 [BOOT] 行
for i in 1 2 3 4 5; do qemu-system-x86_64 ... -serial file:/tmp/a1_$i.log; done
grep '\[BOOT\]' /tmp/a1_*.log

# A-2：跑桌面，读 [PERF] 行
grep '\[PERF\]' /tmp/ser64.log | tail -3

# 拟合与出表
python3 tools/perf_fit.py /tmp/a1_*.log --md
```
