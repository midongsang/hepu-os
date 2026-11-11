---
title: FIXES 0.57.0
status: active
sys_version: 0.57.0
updated: 2026-11-11
owner: status
source_of_truth: null
supersedes: null
---

# v0.57.0 修复与增强记录

本版两条线：**图形层首次系统性增强**（此前全系统没有投影、没有悬停反馈），以及
**在验证图形层时挖出的两个输入链路真实缺陷**——它们的症状正是长期困扰使用者的
"鼠标看着能动、点哪都不中"。

---

## 一、输入链路：两个真实缺陷（本版最重要）

### 1.1 按钮沿报告重复上报位移 → 点击坐标漂移

**现象**：光标停在目标位置后按下鼠标，点击坐标与光标位置偏差 7~17px，
点击全部落空。取证日志：

```
[CLICK-TEMP] click=(27,7)  shell=(27,7)     # 光标实际在 (90,75)
[CLICK-TEMP] click=(162,59) shell=(162,59)  # 光标实际在 (169,71)
```

**根因**（`xhci64.c` 的 HID 报告解析）：

- 旧实现是 `d->mouse_dx = (int8_t)r[1]` **赋值**而非累加，同一轮询周期内到达
  两条报告时，前一条的位移被后一条覆盖丢失；
- 更要紧的是：主机在按钮状态变化的那份报告里，会把上一次未消费的位移
  **重复带一遍**。旧实现照单全收，于是每按/松一次键，光标就被拽回上一段位移的
  起点附近，点击自然落空。

**修法**：

```c
if (nb == d->mouse_btn) {          /* 按钮未变化: 正常累加位移 */
    d->mouse_dx += ndx;
    d->mouse_dy += ndy;
}
d->mouse_btn = nb;                 /* 按钮沿报告只更新按钮, 不吃位移 */
```

同时把 `mouse_dx/mouse_dy` 从 `uint32_t` 改为 `int32_t`（原类型存负增量后
再做 `(int)` 转换，语义脆弱且累加会溢出）。

**验证**：修复后 `click=(168,76)` 与光标位置一致，点击 Apps 标签成功切页。

### 1.2 PS/2 坐标覆盖 USB 累加坐标 → 光标被拽回

**根因**（`hsinput.c`）：USB 鼠标在位时，旧实现仍有兜底分支——"XHCI 本帧无增量
就跟随 PS/2 的绝对坐标"。而 QEMU 的 `mouse_move` / `input-send-event` 会把事件
**同时广播**给 PS/2 与 USB 两条链路，两侧坐标各自演进、互不同步。结果：
按下鼠标那一帧若 USB 没新增量，光标就被 PS/2 的旧坐标覆盖。

**修法**：USB 鼠标在位时彻底不再采用 PS/2 坐标（PS/2 的**按钮**仍参与合流，
不影响兼容性）。

**旁注**：这也是"用 HMP `mouse_move` 移动光标、再用 HMP 点击却点不中"的历史原因；
HMP 只驱动 PS/2，而渲染坐标来自 USB 累加值，两者从来就不是同一个数。

### 1.3 切页不请求重绘 → 标签"点了没反应"

`hpdesk_handle_click()` 的侧栏标签分支改了 `g_sidebar_tab` 就直接 `return`，
没有 `hpdesk_mark_dirty()`。在 v0.55 引入的按需合成下，不标脏就不会重绘，
于是状态已切换但画面不变——外观上完全等于"点了没反应"。

**修法**：切页后显式 `hpdesk_mark_dirty()`。

### 1.4 网卡接线只试 e1000 → 静态 IP 从未生效

`kernel_main_64.c` 的网络接线处只调 `e1000_init()`，而本机（QEMU 与多数被测环境）
实际网卡是 **RTL8139**，于是永远走 `else` 分支打印 `[NET] No NIC found`：
IP/网关从未设置，DHCP 发包必然失败，天气组件也就只能是 `-- / N/A`。

**修法**：先试 `rtl8139_init()`，失败再退回 `e1000_init()`。修复后串口出现
`[OK] Network up (RTL8139 + TCP/IP, static 10.0.2.15)`。

**同时把 DNS 服务器改成候选列表**（DHCP → `10.0.2.3`(QEMU 用户态网络) → `8.8.8.8`），
旧实现只认「DHCP 给的，否则 8.8.8.8」，而受限网络里 8.8.8.8 通常不可达。

**注意**：以上两步让网络**配置**正确了，但本轮实测 DNS 查询仍拿不到应答
（`[HTTP64] DNS resolution failed`）。网络栈的收发包链路需要单独端到端排查，
本版**不宣称网络已打通**；天气组件在取不到数据时的占位显示是预期行为。

---

## 二、图形层增强（新能力）

此前 `hpdesk.c` 中 `grep -n 'shadow|hover'` 只命中 Dock 的 `hover_zone`：
全系统没有窗口投影、没有任何悬停反馈，多窗口重叠时只能靠边框分辨前后。

### 2.1 窗口三层阶梯投影

`hs_window_shadow()`：三层圆角矩形（`k=3..1`，每层缩放 4px 递增），
由外向内逐渐加深（`hs_mix_color(base, 0x000000, 10 + (3-k)*9)`），
整体向下偏移 6px 模拟光源在上方。仅 `HS_WINDOW_FLOATING` 且非最大化时绘制。

第一版用 `mix(window_bg, bg, k*12) + 8% 黑`，在浅色桌面上几乎不可见；
加深并向纯黑靠拢后，2× 放大可清晰看到阶梯过渡。

### 2.2 悬停反馈

- **侧栏标签**：未选中标签在指针下混 28% 强调色（`hs_mix_color(panel_bg, accent, 28)`）
- **Apps 图标**：指针落在单元内时，绘制 60×60 圆角(14) 底座（26% 强调色）+ 强调色描边。
  底座必须比 48×48 图标大一圈（每边 6px），否则会被图标本体完全盖住、看不出变化
  （第一版做 56×56 就是这个问题，实测无效）

### 2.3 悬停高亮的可见性前提：脏矩形触发

只改配色不够——`hpdesk_update_mouse()` 原本完全不 `mark_dirty()`，
按需合成下"移动指针"不触发任何重绘，悬停状态永远刷不出来（这也是本轮
最初"光标停在 Apps 标签上却看不到高亮"的原因）。

新增 `hs_hover_id(x, y)` 统一返回悬停目标编号（`0..2` 标签 / `100+i` 图标 / `-1` 无），
在 `hpdesk_update_mouse()` 里比较前后值，变化即标脏；指针处于侧栏内时持续标脏。

### 2.4 任务管理器刻度线

中位参考线替换为 25%/50%/75% 三条。必须用 `gfx64_fill_rect(x+2, gy, w-4, 1, ...)`——
`gfx64_draw_line` 在窗口内容层本就不可见（v0.56.2 已踩过这个坑）。
`hs_mix_color` 在 `hpdesk.c` 是 `static`，`hs_apps.c` 内另建私有 `hs_apps_mix()`
避免跨文件耦合。

---

## 三、开发工具（`tools/dev/`，宿主机侧零侵入）

| 文件 | 作用 |
|---|---|
| `qemu-common.sh` | 统一 QEMU 参数 + `stop_qemu()` / `wait_desktop()`，把已知约束写进注释 |
| `run-qemu.sh` | 后台启动并等待 `Hepu Desktop started`，支持 `--fresh` |
| `debug-kernel.sh` | `-s -S` 挂起，打印可直接粘贴的 gdb 命令 |
| `shot.py` | HMP `screendump` → PNG，支持 `-o` / `-c x,y,w,h` 裁剪 |
| `smoke-test.sh` | 启动 → 等桌面 → 扫致命错误关键字 → 截图 → 颜色数判黑屏 |
| `pointer.py` | 闭环把指针精确移到指定坐标（小步 rel + 截图读回光标 + 校正） |

踩坑记录：

- `qemu_common_args` 必须**单行 echo**。用反斜杠续行的多行 `echo` 在 `$(...)` 里
  会把反斜杠当字面参数传出，导致 QEMU 参数错乱、串口无日志、误判"未进入桌面"。
- `shot.py -c` 是 `x,y,w,h`，而 PIL `crop()` 要的是 `(left, upper, right, lower)`，
  直接透传会得到 0 高图像（`SystemError: tile cannot extend outside image`）。
- 本机 QEMU 6.2 + `usb-mouse`：`input-send-event` 的 **abs 事件不可用**
  （`Input handler not found`，需 tablet 设备）；`rel` 可用，但系统对增量有缩放。
  `query-mice` 显示 `QEMU HID Mouse` 为 current。

---

## 四、回归基线

- 全量干净重建：**0 error / 89 warning**（`-Wunused-parameter` 基线不变）
- 主机测试：**8/8**
- 文档检查：`python3 tools/check_docs.py` **0 warn 0 error**
- QEMU 冒烟：`tools/dev/smoke-test.sh` 通过（颜色数 912，无致命错误关键字）
- 端到端点击闭环：光标定位 → 点击 Apps 标签 → 页面切换 → 图标悬停底座，逐项截图确认

## 五、遗留

- 光标图形的视觉箭头尖端与逻辑指针点存在约 7~12px 固定偏移（hotspot 语义），
  不影响点击准确性，但视觉上略有偏差，待后续按 xcur hotspot 校准
- 真机（物理鼠标）尚未验证；以上均在 QEMU + XHCI 下取证
- 进程表仍只有 idle，任务管理器的进程列表内容待补
