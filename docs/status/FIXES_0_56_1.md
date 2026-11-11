---
title: 任务管理器假数据与天气组件空白修复（0.56.1）
status: active
sys_version: 0.57.0
updated: 2026-11-11
owner: status
source_of_truth: kernel/arch/x86_64/
supersedes: null
---

# 任务管理器假数据与天气组件空白修复（0.56.1）

## 一、任务管理器：CPU/内存是编出来的

### 症状

打开任务管理器，CPU 曲线、进程 CPU%、内存数字看不到真实占用。

### 取证

`hsservices.c` 的 `hs_process_monitor_update()` 里写着：

```c
g_proc.total_cpu = 15 + (tick * 3) % 40;          /* CPU 总量 */
g_proc.mem_used  = 480 + (tick % 5) * 32;         /* 内存 */
p->cpu_usage = (procs[i].state == 2) ? (5 + (tick * 7 + i * 3) % 30) : 1;  /* 进程 CPU */
```

三行全是按 tick 推出来的伪随机数，**与系统实际负载无关**。注释自己也写着
"模拟波动，真实统计后续接入" —— 这个"后续"一直没来。
`hs_process_monitor_init()` 里还有写死的 `mem_total = 2048`（2GB），与实际机器无关。

### 修复

真实数据源其实一直存在，只是没被调用：

| 指标 | 原来源（假） | 现来源（真） |
|---|---|---|
| CPU 总量 | `15 + (tick*3)%40` | `process64_cpu_usage()`：调度 tick 中非 idle 的比例 |
| 内存总量/已用 | `2048` / `480 + (tick%5)*32` | `pmm64_get_stats()` |
| 进程 CPU% | `5 + (tick*7+i*3)%30` | `runtime / process64_total_ticks()`，即该进程占用的调度份额 |

为避免"进程 CPU% 无法算"，在 `process64_full.c` 新增导出函数
`process64_total_ticks()`（`g_total_ticks` 原是文件内 static）。

## 二、内存占用长期显示 0%（PMM 统计口径错误）

### 症状

桌面 System Info 面板 `MEM` 恒为 `000%`；任务管理器内存行同样接近 0。

### 取证

`pmm64_get_stats()` 原实现：

```c
if (used) *used = pmm64.used_pages * PAGE_SIZE;
```

而 `pmm64_init()` 中 `used_pages = 0`，初始化时把内核镜像、页表、固件保留区标为
占用时**只增加 `reserved_pages`，不增加 `used_pages`**。`used_pages` 只统计运行期
`pmm64_alloc_page()` 的分配 —— 而内核主要用静态堆，几乎不经过 PMM 分配。

结果：机器用掉几百 MB，`used` 仍接近 0。

### 修复

改用"非空闲物理内存"口径：

```c
if (used) *used = (pmm64.total_pages - pmm64.free_pages) * PAGE_SIZE;
```

这样包含内核镜像、页表、固件/PCI 保留区与运行期分配。
验证：同一台 1GB QEMU 机器，`MEM` 从 `000%` 变为 `003%`。

## 三、天气组件只剩一块空白方块

### 症状

桌面右下角天气卡片是一块纯色圆角矩形，没有温度也没有描述。

### 两次成因叠加

1. **数据链路只在第 60 帧试一次**：`desktop64.c` 用 `wx_done` 一次性门控，
   网络（DHCP/DNS/TCP）没就绪时请求失败，之后就永久不再尝试。
2. **渲染层不判断取数成功**：`hs_weather_widget_render()` 直接
   `font64_draw_string(..., g_weather_temp, ...)`，而 `g_weather_temp` 初始为空串。
   失败时既不显示占位，也不显示错误，卡片看起来就是"渲染坏了"。

### 修复

- `hpdesk_fetch_weather()` 改为返回 `int`（1 成功 / 0 失败），失败路径显式 `return 0`；
- `desktop64.c` 改为**最多 6 次重试**，每次间隔 60 帧，成功后停止；
- 渲染增加占位：未取到数据时温度显示 `--`、描述显示 `N/A`，
   让"没有数据"在界面上是可见的，而不是一块空色块。

验证（QEMU 无外网环境）：卡片显示 `--` / `N/A`，符合预期降级行为。

## 四、系统信息里的写死机型文案

同一块面板上还有两处与实际机器无关的静态字符串：

- 左栏 Info 页底部：`CPU: 4 cores`、`Memory: 2GB`
- Settings → System 页：`"4 cores"`、`"2GB"`

即使跑在 `-smp 1` / 1GB 的 QEMU 上也照样显示 4 核 2GB。已改为读
`smp_get_cpu_count()` 与 `pmm64_get_stats()` 的总量，实测同一镜像显示
`CPU: 1 core` / `Memory: 1023 MB`。

## 五、验证

| 项目 | 结果 |
|---|---|
| 全量重编 | 0 error，89 warning（未新增） |
| QEMU `-smp 1` | 正常进桌面；System Info `MEM` = 003%（此前恒 000%） |
| 天气卡片 | 显示 `--` / `N/A`（QEMU 无外网，降级可见） |
| 主机回归 | 8 / 8 |
| 文档 | `check_docs.py` 0 warn 0 error |

## 六、遗留

- **任务管理器界面未截到图**：QEMU 的鼠标注入不稳定（HMP `mouse_button` 与
  QMP `input-send-event` 均未被桌面接收到），"Apps" 页签点不开。
  数据链路已由 System Info 面板同源验证（两者都走 `pmm64_get_stats` 与
  `process64_cpu_usage`），进程级 CPU% 逻辑为 runtime/总 tick，未经界面确认。
- 天气依赖 `api.open-meteo.com` 外网可达；离线环境按设计显示 `N/A`。
- 天气坐标仍写死在常德（29.03N/111.69E），未按位置配置。
- `-Wunused-parameter` 89 条按约定保留。
