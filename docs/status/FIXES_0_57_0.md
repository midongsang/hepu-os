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

### 1.5 以太网抽象层完全不接 RTL8139

`net64_eth.c` 的 `eth64_send` / `eth64_recv` 只有 `e1000_send` / `e1000_recv` 两个分支。
RTL8139 驱动初始化得再成功，**上层一个帧也发不出去** —— 这是"网卡显示 OK、网络却全废"
的真正断点。修法：先走 `rtl8139_send/recv`（`rtl8139_available()` 判定），失败再退 e1000。

### 1.6 MAC 来源写错 → 应答被硬件过滤

`eth64_init` 里注释写着"从RTL8139获取MAC地址"，实际调的却是 `e1000_get_mac(mac)`。
那时 e1000 还没初始化 → MAC 全 0 → 落进"默认 MAC"分支（`00:1A:36:5D:A7:C9`）。
而 RTL8139 硬件按自身 IDR（`52:54:00:12:34:56`）做地址过滤，对端也按帧里的源 MAC 回应，
源 MAC 与硬件 MAC 不一致时应答会被本机网卡丢掉。修法：`eth64_update_mac()` 优先取
RTL8139 的真实 MAC，并在网卡初始化之后再回写一次。

### 1.7 RTL8139 寄存器位定义错误（两处）

- **RCR**：旧常量 `APM=0x02 / AM=0x04 / AB=0x08`。按手册 `RCR[2:0]=RBLEN`、`bit3=AAP`、
  `bit4=APM`、`bit5=AM`、`bit6=AB`、`bit7=WRAP`。旧值正好压在 RBLEN 与 AAP 上，
  RCR 低 3 位被配成 `0b110` = **64K+16 接收缓冲**，而驱动只分配了 `8192+16`，
  硬件 DMA 写出界。
- **CR**：旧常量 `RESET=0x10 / RX=0x08 / TX=0x04`。手册是 `bit2=RCV`、`bit3=TXC`、`bit4=RESET`。
  旧值让 RESET 落在保留位（**软复位从未真正执行**），RX/TX 两位名字互换
  （合写 `0x0C` 时恰好都对，单独用就会错）。

两项已按手册改正（`RCR = RBLEN_8K | APM | AB | AM | AAP | WRAP`，`CR` 位纠正，
复位后加 ~5ms 等待，CAPR 初始化为 `0xFFF0`）。

**仍未闭环（如实记录）**：改完上述四处后，TX 正常（ISR 只有 TOK），但
**RX 一帧都没收到**（ISR 始终没有 ROK），ARP 请求得不到应答 →
`[HTTP64] DNS resolution failed` 依旧。网络栈的接收侧还需要单独端到端排查
（QEMU `info network` 已确认 rtl8139 挂在 user netdev 上，问题在驱动/帧构造侧）。
本版**不宣称网络已打通**；天气组件在取不到数据时显示 `-- / N/A` 占位是预期行为。

---

## 二、图形层增强（新能力）

此前 `hpdesk.c` 中 `grep -n 'shadow|hover'` 只命中 Dock 的 `hover_zone`：
全系统没有窗口投影、没有任何悬停反馈，多窗口重叠时只能靠边框分辨前后。

#### 1.8 数据面板在按需渲染下不会自己刷新

`desktop64.c` 主循环加了低频心跳：每 ~500ms 请求一次 `hpdesk_mark_dirty()`。
按需合成只在"脏"时重绘，而 CPU/内存占用、进程数这类数值是自己变的，
没有任何交互会去标脏 → 面板上的百分比会冻在打开那一刻，看起来就像"占用不显示"。

### 1.9 任务管理器曲线在 0% 时整块空白

空闲系统的 CPU 占用真的是 0%（idle 100%），原实现 `bh=0` 就什么都不画，
三块图板看着全空，容易被当成"图表没接上数据"。改为 0 值也留 1px 采样点
（浅色），表示仍在采样。

### 1.10 IP/UDP/TCP/ICMP 校验和字节序全部写反（网络全线的真正断点）

`ip_checksum` / `udp_checksum` / `tcp_checksum` / `icmp_checksum` 都返回"按大端拼 16 位字
算出的反码和"，即网络序语义的值；但调用处全部直接赋给 `uint16_t` 字段，在 x86 上按小端
存进包 → 实际发出的校验和是字节颠倒的。后果极其隐蔽：**ARP 一直是通的**（ARP 没有 IP
校验和），而 ICMP/UDP/TCP 每个包都被对端按校验失败静默丢弃，于是 DNS 永远解析不出来、
天气组件一直是 `-- / N/A`。

用 QEMU `-object filter-dump` 抓帧验证：修复前 IP 头校验和字段 `0x9b62`，正确值 `0x629b`；
修复后 `csum_ok=True`。共修 5 处（IP 头、UDP、ICMP 两处、TCP），全部改为 `htons16(...)`。

顺带修了 UDP/TCP 的伪头部：`udp_checksum` 内部按 `(ip>>16)&0xFFFF` / `ip&0xFFFF` 拆两个
16 位字，调用处却传了 `htonl32(...)`（已经字节交换过的值），等于交换了两次、算出错校验和
（实测与正确值差 0x5FA 量级）。改为直接传主机序 IP。

### 1.11 `http64_get_body` 传错响应长度 → body 尾部是垃圾

`hpdesk_fetch_weather` 里调 `http64_get_body(resp, sizeof(resp), ...)`，传的是**缓冲区大小
2048** 而不是 `http64_get` 返回的真实长度（约 730）。函数据此算出
`body_len = 2048 - body_start ≈ 1900`，再被 `max_body=1024` 截断，于是拷进 body 的后半截
全是缓冲区旧数据、返回长度也虚高。改为 `int rlen = http64_get(...)` 并把 `rlen` 传下去。

### 1.12 open-meteo 的 units 段抢走 `"temperature"` 键

响应里 `"temperature"` 第一次出现是在 `current_weather_units` 段，值是单位字符串 `"..C"`。
旧实现只找第一个命中点，`hs_parse_int_at` 解析失败就放弃 → 温度永远是初始值，卡片上什么都
不画。改为用 `hs_find_key_from()` 从上一个命中点继续往下找，直到取到真正的数字；
`weathercode` 同理。

### 1.13 天气卡片在"有响应但没解析出字段"时退化成纯色块

`g_weather_ok` 置 1 而 `g_weather_temp` / `g_weather_desc` 还是空串时，卡片只剩一块纯色
圆角方块 —— 看起来跟"没接数据"和"渲染坏了"都分不开。改为按字段回退占位。

### 1.14 QEMU 默认网卡改为 e1000

RTL8139 的接收侧在 QEMU 上始终拿不到 ROK（详见 1.9 的遗留说明），而 e1000 全链路
（ARP → DNS → HTTP → 天气）实测通过，`tools/dev/qemu-common.sh` 的默认网卡切到 e1000。
RTL8139 驱动与 `rtl8139_available()` 判定都保留，真机上仍会优先走 RTL。

### 1.15 侧栏 Info 页百分比是固定三位补零

`CPU 000 / MEM 003 / DSK 009` —— 看着像码表而不是百分比。改为 `hs_pct_str()`
输出 `0% / 3% / 9% / 100%`。

### 1.16 任务管理器进程表只有 idle，看着像"没接数据"

`PROC: 1`，列表只有 PID 0。根因是桌面与驱动都还没注册成调度进程（进程模型补齐是
独立工程，不是本版能顺手做完的）。本版不改动调度，改为在进程表下方增加
**KERNEL SERVICES (drivers loaded)** 区块，用 `drv_get_all()` 列出真正注册进驱动框架的
驱动与其状态（实拍：IDE / RTC / PIT / PS2 Keyboard / PS2 Mouse / PCI / RTL8139 / AHCI
全部 ACTIVE）。面板显示的是真实注册结果，不是凑数的假进程。

### 1.17 进程模型只建了 idle：优化器是空转的，从来没创建过线程

现象：任务管理器 `PROC: 1`，进程表只有 PID 0，看着像"没接数据"。

现场：`process64_init()` 只建 idle(PID0)；`process64_create` / `ready_queue` /
`context_switch_64`(entry_64.S:477) 这套东西**都是齐的**，调度器也确实会真的切 CPU
上下文。缺的只有一件事：**全系统没有任何地方创建过 PCB**。`kernel_main_64.c:418`
那行 `process64_create("init", 0x200000, 0x300000)` 不但被注释掉了，参数本身也是错位的
（`0x200000` 被当函数指针、`0x300000` 被当优先级，进 create 后被夹成 PRIORITY_NORMAL）。

修复：初始化后注册常驻服务线程 `kbeat`（PID 1），入口 `ksvc_beat_entry()` 是
"计数 + `process64_yield()`"的无限循环。串口探针 `[KSVC] beat alive` 每 1000 次打一行，
实测连续运行 25 秒稳定、桌面输入不受影响、任务管理器 `PROC: 2` 且两行都在。

同时修同模块两处隐患：

- `process64_create` 栈分配失败时直接 `return -1`，已 memset、已分配 pid 的槽位既不是
  READY 也不是 UNUSED，反复失败会漏光 32 个槽位 —— 改为失败即置回 `UNUSED` + `pid=-1`。
- `processes[idx].stack = (uint8_t *)stack_phys` 把物理地址直接当虚拟地址用，只在核段
  恒等映射下成立 —— 就地加注释写明这个前提，避免以后引入非恒等映射时踩雷。

遗留（未做，如实记录）：`kbeat` 目前只是心跳，还没有承担真实工作。把网络轮询、指标采样
从桌面主循环挪进线程，要先解决"线程与主循环同时调 `net64_poll` 抢网卡"的并发问题，
属于下一步。

### 1.18 系统图标改为一套统一线性图标

原来的应用图标是十几组各写各的矢量函数（`hs_icon_terminal/task/settings/...`），
线条粗细、圆角、视觉重心都不一致，放在 48×48 底座上像拼凑出来的。

改为统一图标集：从一份 30 枚线性图标总览图裁出每个图标的实际 bbox（按列/行投影定位，
不靠估算坐标），二值化后缩到 32×32、每行一个 `uint32`（bit31=最左）存进
`hs_icons_data.h`（12 枚在用，约 1.5KB）。新增 `hs_icon_bmp(x,y,size,color,bits)`
按目标色填充绘制，所以同一图标在浅/深主题、选中/未选中状态下都能用。

映射按语义贴而不是按名字硬配（注释在 `hpdesk.c` 的 switch 里逐行写明）：
Terminal≡ / Files=home / Browser=search / Settings=齿轮 / Editor=信封 / Calculator=+
/ Notes=点阵 / Task Manager=时钟 / App Store=投影 / About=滑杆 / Device=齿轮 / Disk=下载。

素材另存 `assets/icons-linear/`（30 枚 128×128 PNG），供以后换尺寸或复用到别处。

### 1.19 任务管理器进程表列不对齐

现象：`PID / NAME / CPU / MEM / STATE` 五列在每行都错开，数字位数一变列位置就漂。

根因：表头和整行都是**拼成一个含空格的字符串**再一次性画（`hs_itoa` + 手动补空格），
而 font64 是比例字体 —— 空格宽度和 `0` / `16` / `100%` 的宽度都不一致，补空格补不出
等宽列。这不是"差几个像素调一调"，是方法本身错。

修复：表头与数据行共用一组固定列偏移，**分 5 段分别绘制**（`c_pid/c_name/c_cpu/c_mem/
c_state`）。列距按内容宽度定（NAME 最长 12 字符、CPU 最长 `100%`），不用百分比摊开，
否则 520 宽窗口后两列会顶到右边界、被旁边面板遮住。

### 1.20 多线程把主循环饿死（新增服务线程时踩到的真实限制）

按"增加注册进程"建了 `kbeat` / `khouse` / `kstat` 三个会主动 `yield` 的线程后，
**整机停在启动阶段**：串口只剩心跳，桌面一直不出来。

根因：调度器只在 `ready_queue` 里挑人，而 **idle(PID0) 从来没有被放进就绪队列**
（它只是初始化时把 state 置成 RUNNING）。一个服务线程时，`schedule` 选来选去还是它
自己，无害；一旦有两个以上，它们就互相切换、永不回到 BSP 上跑桌面主循环的那个上下文。

处理：`kbeat` 保留真实运行（心跳 + 每 5 秒一条串口探针）；`khouse`（每秒唤醒桌面合成）
和 `kstat`（维护 uptime 缓存）改为 `process64_create_suspended()` —— **建 PCB、摘出
就绪队列**，登记但不调度。任务管理器里它们的 STATE 显示 `kthr`，不假装在跑。

同时修掉两个由这次改动暴露的问题：

- 心跳探针原来按次数节流（每 1000 次），空转循环一秒跑上万轮 —— 实测刷出 6 万行日志
  并把整机拖住。改为按时间节流（每 5 秒）。
- `UP:` 原本读 `kstat` 的缓存值，而 kstat 处于挂起 → 恒显示 `0s`，这是假数据。改为
  直接读 `pit64_get_ms()`。

遗留（下一步，已写清楚）：要让 `khouse`/`kstat` 真正跑起来，前提是 **idle 能被正常
切入切出** —— 需要把 PID0 的 context 做成可保存/可恢复的形态并加入就绪队列，否则
桌面主循环这个"进程"本身没有可切换的上下文。

### 1.21 状态栏右上角图标风格与 App 图标不统一

`wifi / battery / volume / power` 四个是各自手绘的矢量函数，线宽、圆角、视觉重心都和
新换的线性图标集不一样，并排放着像拼的。

处理：用同风格补画 4 枚（wifi 双弧+点、电池圆角框+正极+电量、喇叭+声波弧、电源开口
环+竖线），和 App 图标一起放在 `hs_icons_data.h`；状态栏改为 `hs_icon_bmp()` 绘制，
尺寸从 16px 提到 20px（32px 源图减半到 16px 时 2.5px 线宽会塌成糊块）。删掉了不再被
调用的 4 个旧矢量函数，全量重链验证警告回到基线。

### 1.22 第二批图标：45 枚入库，修正三处语义错配

用户又给了 4 张图标集（社交线性图标 15 枚、wifi/信号/电池状态图标 40 枚、线性图标集
27 枚、一枚红色关闭按钮）。同样按投影 bbox 裁剪，二值化 32×32 入库（统一加
`__attribute__((unused))`，没接入的不产生未使用告警）。

这次接入的部分：

- **状态栏 wifi** 换成标准造型（之前自绘的三道弧在 20px 下糊成一块）。
- **状态栏电池**：试过图标集里的实心竖版/横版，20px 下都糊成黑块，最后仍用自绘线性版
  （圆角框 + 正极 + 电量填充）。**这批图里没有能直接用的电池造型**，如实记录。
- **Files** → 更规整的房子；**Notes** → 带字文档（原来是"下载箭头"，语义错）；
  **App Store** → 应用网格（原来是"投影"）；**About** → 均衡器滑杆。

其余 41 枚连同上一批 18 枚未接入的一起存 `assets/icons-v2/`（128×128 PNG），
留给媒体播放器、聊天/社交面板、文件管理器工具条用 —— 那些界面还没做，现在接进去
只能算摆设。

### 1.23 状态栏图标清晰度：源数据 32→64px，绘制改覆盖采样 + 4 级混色

20px 显示时那排图标糊成一团, 根子在两处:

1. **源数据只有 32×32**。32→20 的缩放本身就把 2.5px 的线条压到 1.5px;
2. **绘制是"源像素 → 目标矩形块"的直接映射**。非整数倍缩放时会整像素跳过, 一段 2px
   宽的弧线在目标尺寸上直接断掉 —— 这才是"糊"的主因, 不是尺寸不够。

改法:

- 状态栏 5 枚图标(wifi/电池/喇叭/齿轮/电源)在 256×256 画布上重绘后降到 **64×64**,
  每行一个 `uint64`(bit63=最左);
- 新增 `hs_icon_bmp64()`, 按**覆盖采样**算每个目标像素覆盖到的源像素里有多少是墨;
- 再把覆盖比例分 **4 档**(≥75% 实色 / ≥50% 62% 混色 / >0 30% 混色) —— 相当于给 1-bit
  位图补了抗锯齿, 边缘不再是硬台阶;
- 显示尺寸 20px → **24px**(状态栏高 36px, 上下各留 6px)。

素材存 `assets/icons-status64/`(64×64 PNG)。

### 1.24 偶发卡在 "AP trampoline copied to 0x8000" 的根因与修复

现象: QEMU 启动有相当概率停在 `[SMP] AP trampoline copied to 0x8000 (N bytes)`,
桌面永远不出来, 重跑一次常常就好了。之前一直当成"偶发"绕过去。

根因不在 trampoline 拷贝本身, 而在它后面紧接着的那一步: `apic64_init_timer()`
→ `apic64_calibrate_timer()` 用 PIT tick 做窗口计时, 里面有两个**无界自旋**:

```c
uint64_t t = pit64_get_ticks();
while (pit64_get_ticks() == t) { /* spin */ }        // 等下一个 1ms tick
while (pit64_get_ticks() < target) { /* spin */ }    // 等 50ms 窗口
```

而 `smp_init()` 跑在 `irq64_init()` **之前**, 此时已经调用过
`apic64_switch_to_apic_mode()` 切到纯 APIC 模式, 但 IOAPIC 重定向表还没配 ——
PIT 的 IRQ0 有可能压根投递不进来, `pit64_get_ticks()` 永远不前进, 自旋就永久卡死。
是否能投递取决于 ACPI MADT 中断源覆盖(IRQ0→GSI2)的解析结果, 所以表现为**概率性**,
重跑一次撞到另一种时序就好。

修复: 两个等待循环都加有界超时(`CAL_SPIN_LIMIT`), 超时直接 `return -1`, 走调用方
**已经存在**的 `ICR=6250` 回退路径 —— 系统继续启动, PIT 1kHz 仍在投递调度 tick。
把"偶发死机"降级成"偶发降级"。

验证: 连续冷启动 6 次, **6/6 全部进桌面**。其中 2 次日志出现
`[APIC] WARN: timer calibration failed`(即原先会卡死的那两次), 降级后照常启动。

已知残留: 回退值 `ICR=6250` 是"假设 100MHz 总线"的历史魔数, 而 QEMU 实测约
400MHz, 降级时 tick 会偏快约 4 倍(影响 CPU 占用率与 uptime 的精度, 不影响功能)。
彻底解法是把校准挪到 IOAPIC 重定向配置完成之后, 或改用 TSC 校准 —— 本轮未做。

### 1.25 定时器校准的 4 倍偏差：根因定位、三条方案的结论与残留

§1.24 只保证"不永久卡死", 但校准失败时 tick 会快约 4 倍(ICR=6250 假设 100MHz 总线,
QEMU 实测约 31MHz)。本轮想把偏差也消掉, 结论如下。

**根因**: `smp_init()` 运行在 `irq64_init()`(即 "Interrupts enabled")**之前**,
此刻 PIT 的 IRQ0 还没开始投递, `pit64_get_ticks()` 不推进 —— 所以校准必然超时失败。
"重跑一次就好"就是撞到了中断恰好已可用的时序。

**三条方案, 各自为什么不成**:

1. **把校准挪到 `irq64_init()` 之后立刻做** → 该位置访问 APIC 寄存器直接触发页错误
   0xE(CR2=0xF00003E0), 系统崩。
2. **启动阶段不校准(bootstrap), 进桌面后再校准** → bootstrap 只能用 6250 的 4kHz,
   中断负载过重, 桌面卡在壁纸/PNG 解码之后,**根本走不到"进桌面后"那一步**。
3. **改回退值**(6250→62500, 让 tick 落到 ~500Hz) → 实测卡点与 6250 完全相同。
   说明卡住的不是频率快慢, 而是"校准失败这件事本身"。

**本轮保留的增量**(低风险, 正常路径不触发):
- `apic64_timer_needs_recal()` / `apic64_timer_recalibrate()` 两个接口;
- 桌面初始化成功后再检测一次 —— 若启动期确实没校准出来, 此时补做(此刻 tick 已在走)。
  只是它救不了"桌面起不来"的情形, 属于给"能起来但频率不对"兜底。

**残留(未解决)**: 冷启动仍有偶发卡住, 且不限于校准失败路径(校准成功的运行也出现过)。
另一条已记录的现象是"内核线程创建之后主循环停摆"(`idle`/PID0 不在 `ready_queue`,
抢占后 BSP 的主循环上下文回不来)。根治需要把 PID0 的 context 做成可保存/可恢复
并加入就绪队列, 这一项未做。

## 2.1 窗口三层阶梯投影

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

### 1.26 桌面主循环被内核线程饿死（长期"重跑一次就好"的偶发卡住）

**现象**：冷启动偶发停在 `[PROC64] Created process: khouse` 之后，画面停在深蓝
VBE 文字（截图取 (100,300) 为 `(0,0,128)`），重跑一次通常就好。

**取证**：用截图采样判据（浅色面板 ≈ `(250,246,238)`）把卡点分成两类（早卡 / 深卡），
再读 `process64_schedule()`：调度是**严格优先级抢占**（从 `PRIORITY_REALTIME` 往下扫），
同优先级内才按 `scheduler_ticks % total` 加权轮转。桌面主循环跑在 idle(PID0, 优先级 0)
的上下文里，而 `kbeat` 建出来是优先级 **2** —— 它一旦可运行就永远压住 idle，
主循环再也拿不到 CPU。偶发性来自 kbeat 首次可运行的时刻与主循环推进的竞态。

**修复**：`kbeat` 优先级 2 → **0**，与 idle 同级，进同一就绪队列按 tick 轮转。
`context_switch_64` 用 `ret`（不是 `iretq`）且保存 rsp/rbp/rbx/r12-r15，
所以 idle 首次被切出时其上下文被自然保存，同级轮转后能被正确切回。

**验证**：连续 4 次冷启动 4/4 进桌面；此前同镜像 6 次里必现 1~2 次卡住。

### 1.27 内核加载区 640KB → 1MB

64 位内核已 637880 B（≈623KB），stage2 上限 640KB（20 块 × 32KB），余量只有 17KB，
任何第三方库都塞不进去。改 `stage2_64.S` 块数 20 → 32（1MB），32 位内核整体后移
LBA3400 → 4400（`make_single_img64.sh` 的 `KERNEL32_LBA` 同步）。安全性依据：
`pmm64.c` 用链接符号 `&_end` 动态取内核结束地址，多读进 `.bss` 的部分被入口清零覆盖。

### 1.28 B 组第一项：stb_image 接入（壁纸支持 JPG）

- 新增 `third_party/stb/stb_image.h`（v2.30，283010 B）+ `kernel/arch/x86_64/jpeg64.c/.h`。
- 只开 `STBI_ONLY_JPEG`（PNG 已有 png64），其余解码器被预处理器剪掉，代码段仅 18.8KB。
- 分配器：页级 bump（`pmm64_alloc_pages`，物理地址当虚拟地址，前提是核段恒等映射），
  壁纸常驻不回收。
- 壁纸：`hs_wallpaper_png()` 先读 `wallpaper.png`，失败回退 `wallpaper.jpg`，
  渲染统一走 `src_px/sw/sh`。

**三个必须踩过去的坑**（内核环境与用户态不同）：
1. **`__thread` 不可用**：stb 的 `stbi__vertically_flip_on_load_local/_set` 是线程局部，
   内核未设 FS 基址 → `cmpl $0x0,%fs:0xfffffffffffffff8` → `vector=0xE CR2=0xFFFFFFFFFFFFFFF8`。
   修：`#define STBI_NO_THREAD_LOCALS`。
2. **`assert` 无实现**：链接期 `undefined reference to 'assert'`。修：`#define STBI_ASSERT(x) ((void)0)`
   （stb 官方支持覆盖）。
3. **`-mno-sse` 与 SIMD 分支冲突**：`SSE register return with SSE disabled`。修：`#define STBI_NO_SIMD`。

**验证**：内核 637880 → 652472 B（+14.6KB，1MB 上限内）；`[JPEG64] decoded 1024x768`，
壁纸顶部像素 `(26,94,193)` 与测试图渐变一致，桌面正常。


### 1.29 B 组第二项：Lua 5.4 解释器接入

目标：让内核里能真正执行 Lua 脚本（后续给 shell / 配置系统 / 插件用）。

**落地组成**
| 组成 | 文件 | 说明 |
|---|---|---|
| Lua 源码 | `third_party/lua/src/` | 5.4.7，去掉 `lua.c`/`luac.c`/`ltests.c` |
| 兼容头 | `third_party/lua/include/` | 9 个极简 C 头（string/stdlib/stdio/math/locale/time/setjmp/ctype/assert），**不能混内核头** |
| 内核 libc/libm 子集 | `kernel/arch/x86_64/klibm64.c` | malloc/realloc/free、strtod、memchr/strstr/strspn、double 版 sqrt/exp/log/sin/cos/atan 等 |
| setjmp/longjmp | `kernel/arch/x86_64/kjmp64.S` | 64 位内核此前完全没有（32 位内核有 `arch/x86/setjmp.S`） |
| 接入层 | `kernel/arch/x86_64/lua64.c` | kheap 分配器、串口输出重定向、库集合、自检 |

**五个必须踩过去的坑**

1. **浮点 ABI（最隐蔽）**。内核整体 `-mno-sse`，但 x86_64 SysV 规定 `double` 参数/返回值走
   `xmm0`。禁 SSE 后 stb 报 `SSE register return with SSE disabled`，Lua 则是**静默错误**：
   用 `-mno-sse` 编的 `vsnprintf` 从整数寄存器组取 vararg double，拿到的是垃圾。
   → Lua 全部源码 + `klibm64.c` 单独用 `-msse2` 编（Makefile 里 `LUA_CFLAGS`）。
2. **`-msse2` 与内核头冲突**。把 `kernel/include` 混进 include 路径会让 Lua 一片
   `expected ')' before 'c'`。→ 给 Lua 独立一套兼容头，互不相见。
3. **`longjmp` 必须标 `noreturn`**。Lua 的 `luaD_throw` 是 noreturn、以 `abort()` 收尾；
   漏标会让编译器生成"函数返回"路径（`ldo.c: noreturn function does return`），
   错误抛出后可能落到未定义位置。`abort`/`exit` 同理。
4. **`vsnprintf` 要重做**。`kernel/lib/kstdio.c` 只支持 `%d/%u/%x/%c/%s/%p`，Lua 的
   `tostring` 依赖 `%.14g`、`string.format` 还要 `%f/%e`。→ 在 `klibm64.c` 重写一份，
   支持精度、`%g` 的有效数字语义、整数/浮点；`kstdio.c` 不再进 64 位构建。
5. **一堆 libc 桩**。`lauxlib` 即使不用 `loadfile` 也会引用 `fopen/fread/feof/getc/
   strerror/localeconv/__errno_location`，给安全空实现（返回失败而非乱跳）。

**验证**
- 内核 652472 → **780480 B**（Lua 真正被链入，1MB 上限内）。
- 全量干净重建 **0 error / 90 warning**（= 基线，第三方代码的警告已通过补齐
  兼容头声明消掉，未用 `-w` 掩盖）。
- QEMU 串口实测：
  ```
  hepu lua Lua 5.4
  sum(1..100)=5050
  sqrt2=1.414213
  table=1,4,9,16,25
  [LUA64] selftest result: ok
  ```
  覆盖了：整数运算、`%.6f`、`%.14g`、字符串拼接、table 库、math 库。
- 桌面仍正常（截图为证），未影响既有启动路径。

**已知限制**
- `io`/`os`/`package` 库未编（内核没有文件/进程/dlfcn 抽象）。
- 中断处理不保存 xmm 状态：若 Lua 执行中被 tick 抢占、且另一个线程也用 SSE，
  xmm 会串。当前只有 Lua 用 SSE，实际不触发；后续需要的话在 `context_switch_64`
  里补 `fxsave/fxrstor`。
- 精度是多项式实现（约 1e-9 相对误差），不是 libm 的 0.5ulp。


### 1.30 线程切换保存 SSE/FPU 状态（补齐浮点 ABI 技术债）

**背景**：接入 stb 与 Lua 后，二进制里出现两套浮点 ABI —— 内核主体 `-mno-sse`，
Lua / `klibm64` 是 `-msse2`。`context_switch_64` 原本只保存 rbx/rbp/r12-r15/rsp，
**不保存 xmm/MXCSR**。一旦两个线程都用浮点，切换就会互相污染，症状是"偶发数字错误"，
且极难定位（不报错、不崩）。

**改动**
1. `process_context_t` 增加 `fx_area[512]`（FXSAVE 格式）+ `_fxpad` 占位，
   让它在 context 偏移 **208**（= 13×16，满足 FXSAVE 的 16 字节对齐要求）。
2. `context_switch_64` 在保存 GPR 之后 `fxsave 208(%rdi)`，在加载 GPR 之前
   `fxrstor 208(%rsi)`；旧进程指针为 NULL 时跳过保存。
3. `proc_init_fx()` 给新进程的 fx_area 写 Intel 复位标准值（FCW=0x037F、MXCSR=0x1F80）——
   全 0 的 MXCSR 会屏蔽全部 SSE 异常、全 0 的 FCW 让 x87 行为怪异，不能用 memset 凑数。
   idle(PID0) 与 `process64_create` 都走这个初始化。
4. 三处 `_Static_assert` 钉住偏移：`offsetof(process_context_t, fx_area) == 208`、
   该偏移 16 对齐、`offsetof(process_t, context)` 16 对齐。汇编里写死的 208 一旦与
   C 结构脱节，就会静默保存/恢复到错误位置 —— 用编译期断言堵住这条缝。

**验证**
- 全量重建 **0 error / 90 warning**（基线不变）。
- 压力用例：Lua 里 `for i=1,200000 do acc=acc+1.0/3.0 end`，期间被 kbeat 反复抢占，
  输出 `acc=66666.6666`（精确值 66666.66666…，`%.4f` 四舍五入正确）。
- 连续 3 次冷启动：自检通过、浮点值一致、桌面正常。

**说明**：该用例能证明"保存/恢复路径正确且没引入回归"，但它无法反证"不加 fxsave 一定出错"
（kbeat 本身不碰浮点）。这条改动的价值在于给后续 FreeType 这类重度用浮点的库铺地基。


### 1.31 UEFI 引导线接进 64 位主线（第一条不依赖 16 位实模式的启动链）

**背景**：`docs/boot/BOOTLOADER_DECISION_LIMINE.md` 判断"UEFI 线是能编译、没接线"。
本次把它接上线并端到端验证：OVMF → `hepboot_uefi.efi` → 64 位内核 → 桌面 + 首设向导。

**三处断点（都是"看起来通、实际不通"）**

| # | 断点 | 现象 | 根因 |
|---|------|------|------|
| 1 | `hepboot_uefi.c` 64 位分支写 `jump_to_kernel_64(0x10000c, ...)` | 内核已加载到 0x100000，跳过去落在空内存 | 入口地址是从 BIOS 线"32 位壳在 0x10000"抄来的；UEFI 线并不加载那段壳，64 位 flat 镜像的入口就是 0x100000（与 `stage2_64.S` 的 `jmp 0x100000` 一致） |
| 2 | `build_uefi_dual.py` 的 `PROJECT` 写死另一会话的绝对路径；内核被放进 `EFI/BOOT/` | 脚本跑不动；即便跑动，引导器读的是根目录 `\KERNEL64.BIN`，找不到文件 | 路径常量写死 + 文件落位与引导器读取路径不一致 |
| 3 | 自研 GPT/FAT32 生成器 | OVMF 报 `failed to load Boot0002 "UEFI QEMU HARDDISK": Not Found`，随后掉 PXE | 见下 |

**断点 3 的两个硬伤（逐个定位）**

1. **GPT 分区表数组 CRC 一直是 0**：`create_gpt_partition_table()` 先算 header CRC 再写分区数组，
   且偏移 88 的 entries CRC 从没写入。OVMF 校验失配 → **整张 GPT 被丢弃** → 视为无启动盘。
2. **只写主表、缺备份表**：标准 GPT 要求磁盘末尾有一份 header + entries。
3. 补完 1、2 后仍不通，继续定位到 **FAT32 的 FSInfo 结构签名**：
   偏移 484 必须是 `rrAa`（0x61417272），实现写成了 `RRaA`（0x41615252），且尾部签名缺失。
   这类错误 mtools 能容忍（`mdir` 读得出来），但 UEFI FatPkg 不容忍。

**最终方案（构建期用成熟工具，运行时零外部依赖）**

- GPT：`sgdisk --clear` + `-n 1:2048:+128M -t 1:ef00` + `-n 2:... -t 2:8300`（主/备表与 CRC 由工具保证）；
- ESP：`mkfs.vfat -F 32 -s 1 -n HEPU_ESP` + `mmd`/`mcopy` 写 `EFI/BOOT/BOOTX64.EFI`、`KERNEL64.BIN`、`HEPBOOT.CFG`；
- 根分区：仍用 `mkfs.ext2` + `debugfs` 写 `/fonts/DejaVuSans.ttf` 与 `/wallpaper.png`；
- 自研 `create_gpt_partition_table()` / `FAT32Generator` 保留在源码里作研究对照，不再用于出镜像。

**关键取舍**：FAT32 簇数必须 ≥ 65525（128MB 分区若用 8 扇区/簇只有 32768 簇，严格实现直接判非法）。
`mkfs.vfat -s 1` 得到 258016 簇，满足要求。

**验证（端到端）**
- `make -C tools/gnuefi && make -C tools/hepboot/uefi` → 75,835 B PE32+ EFI；
- `python3 tools/build_uefi_dual.py` → 256MB GPT 镜像；
- QEMU + OVMF（`OVMF_CODE_4M.fd`）启动 → 串口出现 `[SMP] BSP APIC timer enabled` … `[Hepu Desktop] Initialized (2048x1536)`、
  `[DESKTOP64] Desktop running with mouse support`、`[OOBE-GUI] first boot`；
- 截图确认状态栏、Info 侧栏、Settings、Quick Controls、首设向导 1/4 全部正常。

**已知限制（本次未处理，非阻塞）**
- UEFI 路径下 `[ACPI] RSDP not found`：内核仍按 BIOS 方式扫内存找 RSDP，UEFI 启动时应从
  配置表（`EFI_CONFIGURATION_TABLE` 的 ACPI GUID）转交——影响 SMP/timer 精度，不影响启动；
- `[PNG64] no sample image`：壁纸文件已写入 ext2，但桌面读取路径与镜像布局尚未对齐；
- 当前主线不产出 `build_v250/hepu-kernel.bin`，脚本已改为"存在则打包"。


### 1.32 UEFI 路径接 ACPI（RSDP 来自 EFI 配置表 + 修页表映射空洞）

**症状**：UEFI 启动时 `[ACPI] RSDP not found`，SMP 退化为 CPUID 探测、APIC timer 校准失败。
真机上前者会直接拖垮中断与多核。

**根因（四层，逐层剥出来的）**

1. **协议层**：UEFI 下 ACPI 表由固件通过 `EFI_CONFIGURATION_TABLE` 传递，地址不在
   EBDA/`0xE0000-0xFFFFF` 内（OVMF 实测 RSDP 在 **0x3FB7D014 ≈ 1019MB**），内核沿用 BIOS 扫描方式必然扫不到。
2. **页表层（空洞）**：内核页表对 `[0, min(total_mem,1GB))` 做恒等映射、对 `[1GB,4GB)` 做高直接映射。
   本机 `total_mem=979MB`，于是 **`[979MB, 1GB)` 两端都不覆盖**，而固件表恰好落在这段空洞里 → `#PF`。
3. **大页对齐**：把高映射起点改成 `low_end` 后仍崩，`#PF error=0x9`（RSVD 保留位）。
   原因是 `low_end` 不是 2MB 对齐，2MB 大页的物理地址低 21 位非零，页表项被判非法。
4. **调用点遗漏**：`acpi_init_irq_overrides()` 仍在用 `vmm64_phys_to_virt()`（阈值写死 1GB），
   对 1019MB 的地址原样返回物理地址，又踩回空洞。

**改动**

- `hepboot_uefi.c`：新增 `find_acpi_rsdp_from_table()`，遍历配置表匹配 ACPI 2.0 GUID
  `8868E871-E4F1-11D3-BC22-0080C73C8881`（回退 1.0 GUID），校验 `"RSD PTR "` 签名；
  在 ExitBootServices **之前**取好地址，之后写入固定槽位 **0x6020**（低内存，与 0x6000 图形信息不冲突）。
- `kernel/arch/x86_64/acpi.c`：`acpi_find_rsdp()` 先读 0x6020，签名/校验通过即直接用；
  全部地址转换统一改为 `vmm64_phys_to_ptr()`。
- `kernel/arch/x86_64/vmm64.c`：
  - 记录 `g_low_identity_end`（恒等映射实际上限），新增 `vmm64_phys_to_ptr()` 按它判断；
  - **高直接映射改为从 0 起覆盖 4GB**（恒等映射区重复映射无副作用，VA 不同），
    既消除 `[total_mem,1GB)` 空洞，又保证 2MB 起点对齐。

**验证（QEMU + OVMF，-smp 4）**
```
[ACPI] RSDP found at 0xFFFF80003FB7D014
[ACPI] RSDT found, length=0x38 / MADT found, length=0x90
[ACPI] CPU: apic_id=0x0..0x3  (4 个)
[ACPI] I/O APIC: id=0x0, addr=0xFEC00000
[ACPI] IRQ override: ISA IRQ0x0 -> GSI0x2
[SMP] Using ACPI MADT, CPU count: 0x4
[SMP] Starting AP 0x1 / 0x2 ...
```
桌面 Info 面板由 `CPU: 1 core` 变为 **`CPU: 4 cores`**；首设向导正常，无异常。

**回归**：BIOS 线（stage2_64）同步复跑，映射改动不影响原路径（见下）。


### 1.33 GPT 盘根分区挂载失败（保护性 MBR 没有 0x83 项）

**症状**：UEFI 路径桌面能起，但根文件系统一直没挂上 —— `[EXT2_64] ERROR: invalid superblock parameters, refuse to mount`。
连带后果是 `/account.conf` 读不到（每次开机都弹首设向导）、壁纸读不到。

**根因**：`ext2_64_init()` 读超级块用 `super_lba = 2 + ext2_partition_offset`，而该值来自 MBR 分区扫描
（`kernel_main_64.c` 只认 `type == 0x83`）。GPT 盘的保护性 MBR 只有一条 `0xEE` 项，扫描一个 0x83 也找不到，
于是回退出厂固定值 **13000**（BIOS 布局），而 UEFI 布局的根分区在 **264192** —— 读到的不是超级块。

**修法（构建期桥接，不改内核分区逻辑）**：`build_uefi_dual.py` 在生成 GPT 后补写一条 `0x83` MBR 项
（LBA 264192，245760 扇区）。UEFI 固件只读 GPT，不受影响；内核 MBR 扫描因此能拿到真实根分区偏移。

**验证**
```
[INIT] Found Linux partition 1: LBA=264192 size=120MB
[INIT] Using partition 1 as root
[EXT2_64] Magic OK, block_size=0x1000 / GDT loaded / Root inode OK
[EXT2_64] Verify read: HepuOS 64-bit ext2 write test OK!
[AUTH] Account loaded: dev (role 2)        ← 首设不再弹
```

**同轮修掉的路径不一致**：桌面读壁纸用 `/usr/share/wallpapers/wallpaper.png`，构建脚本却写 `/wallpaper.png`，
两边对不上。改为两处都写，并在 `hs_wallpaper_png()` 加 `/wallpaper.png` 平铺回退。

**仍未解决（如实记录）**：壁纸 PNG 能读到但 **解码结果是横条纹错乱**（`png64_decode` 对该 4K 壁纸的过滤/扫描线处理有缺陷），
日志仍打印 `[PNG64] no sample image`。此项不影响系统其余功能，列为待修。


### 1.34 壁纸解码成横条纹（不是解码器，是渲染跟不上）

**现象**：壁纸显示为水平色带错乱，日志 `[PNG64] no sample image` 与 `[PNG64] ok 512x288` 同时出现。

**定位过程（先用证据排除解码器）**
1. 把 `png64.c` 用 `-DPNG64_HOST` 在主机编译，直接解 `assets/samples/wallpaper.png`，
   输出 `512x288`、`px0=(11,59,79)`，与 PIL 逐像素一致 —— **解码器无罪**。
2. 在内核里加采样日志，打印解码后的 `w/h` 与首像素/中心像素，结果同样 `512x288 / (11,59,79) / (35,75,85)`
   —— **内核里数据也是对的**。
3. 剩下的只有绘制路径：`gfx64_blit_rgba` 把 512×288 放大到 2048×1536，每像素一次除法 + alpha 混合，
   共 300 万像素。QEMU 上一帧画不完，`screendump` 抓到的就是"只刷了部分行"的横条纹。

**修法**
- `hpdesk`：新增 `hs_wallpaper_build_cache()` —— 首次渲染时按屏幕比例算一次整屏 XRGB 缓存
  （x 映射预先算成表，避免逐像素除法），之后每帧只做整行拷贝。
- `gfx64`：新增 `gfx64_blit_rgba_opaque()`，不透明图跳过 alpha 混合（壁纸/封面这类全屏图适用）。

**验证**：壁纸平滑铺满 2048×1536，无条纹；灵动岛、天气卡、Settings、Quick Controls 全部正常。
内存代价：整屏缓存 2048×1536×4 = 12.6MB（本机 979MB 可接受），分配失败时不画壁纸而不是画错。


### 1.35 左右面板大小不一致 = 两栏用了两套尺寸来源

**现象**：同屏下左侧 System Info 面板明显比右侧 Quick Controls 宽（2048×1536 下 560px vs 360px），
被误读成"分辨率提高了"或"组件变小了"。

**根因**（分辨率始终是 2048×1536，GOP 未变，前几张截图也同样）
- 左侧：`sidebar_width: 280` → `sw = hs_sx(280)`，按基准 1024 缩放 → 560px；
- 右侧：`cc_geometry()` 直接用配置的 `cc_width`，**完全不缩放** → 360px；
- Settings 窗口（460×430）是第三套常量，也是裸像素。

两栏各自内部是自洽的（侧栏内部走 hs_sx，右栏内部走裸像素），所以看不出坏，只是**基准不一致**，
在宽屏上差距被放大。

**修法（先试过「外框统一乘 hs_s」，失败后改方案）**
- 错误尝试：给 `cc_geometry`、`CC_RAIL_W/CC_PAD`、卡片网格、导航栏、Settings 窗口全部乘 `hs_s()`。
  编译通过、缩放确实生效，但**布局被撑坏**：Settings 变成覆盖左侧 1/3 屏的浅紫大块（内容在固定坐标里只画左上角），
  Quick Controls 的 Air/DND 卡与 Volume/Brightness 滑块互相压盖。
  原因：面板内部有上百处固定像素坐标，还有一套**与渲染镜像的命中测试**（`hs_control_center_click`），
  外框放大而内部坐标不放大，必然错位。
- 最终方案：**宽度随 `hs_sx()` 缩放，高度与内部纵向坐标保持基准像素**。
  - `cc_geometry`：`w = hs_sx(cc_width)`，`cx = screen_width - w - hs_sx(24)`，`cy = hs_sy(cc_y)`，另加屏内钳制（宽 ≤ 3/5 屏、高 ≤ 19/20 屏）；
  - 基准值 `cc_width` 由 360 改为 **280（与 sidebar_width 相同）**，两栏因此落在同一基准、同一缩放函数上；
  - 高度、`CC_RAIL_W/CC_PAD`、卡片高度与行距、导航条目全部保持基准像素 —— 不缩放纵向，卡片不会被压扁重叠；
  - Settings 窗口尺寸撤回原值（其内部同样是一套固定坐标 + 命中测试，需单独改造，不在本次范围）。

**验证**：2048×1536 下两栏均约 560px 宽，Quick Controls 的 6 张卡片（WiFi/BT、Air/DND、Loc/Torch）
与 Volume/Brightness 滑块排布正常、无压盖；左侧 System Info 不受影响。

**遗留**：Settings 窗口内部仍是 1024 基准的固定坐标，若要跟随缩放，需要连同 `hs_settings_window_render`
的绘制与命中一起改造（工作量约等于重写一个面板）。


### 1.36 配置解析器 256 token 静默截断（加一行配置就卡死桌面的根因）

**现象**：在内置默认配置里加一行 `ui_scale: 100`，编译通过，但整机停在启动画面
（`HepuOS 64-bit Graphics / VBE Linear Framebuffer`），串口日志止于 `config: no file, using built-in defaults`，
既没有 parse failed 也没有成功日志。移除该行后立即恢复正常。

**定位**
1. 先怀疑是新增键不被支持 → 不是，`hsqml_get_number` 对缺失键返回默认值；
2. 注意到成功日志 `config: N sections` **从来没打印过**，说明解析一直不算真正成功；
3. 查 `hsqml.c` 词法阶段：`while (parser.token_count < HSQML_MAX_TOKENS - 1)`，
   `HSQML_MAX_TOKENS` 只有 **256**，循环到上限就**静默停止**，不报错；
4. 内置默认配置约 9700 字节、300+ 个 token → 尾部一直被丢弃。
   现有配置的截断点正好落在完整节点边界，语法侥幸可解析；
   新增一行把截断点挪进半个节点中间 → 语法分析进入异常路径并卡死。

**修法**
- `hsqml.h`：`MAX_TOKENS 256→1024`、`MAX_PROPS_TOTAL 128→640`、`MAX_NODES 16→32`、`PROPS_PER_NODE 32→96`
  （`parser` 本就是 `static`，不占内核线程栈；增大部分落在 BSS，内核 bin 仍 781968 B < 1MB）；
- `hsqml.c`：token 超限与属性池超限都改为**显式置 error + error_msg**，不再静默丢弃。

**验证**：`ui_scale: 100` 保留在配置中，桌面正常启动，日志首次打印
`[Hepu Desktop] config: 3 sections, background=0x1c1b1f` —— 三个 section 全部生效，桌面外观与修复前一致。

**影响面说明**：此前默认配置中位于第 256 个 token 之后的项（主题色板后半段、字体、面板几何等）
从未被应用，系统实际一直在用硬编码默认值运行。此修复后这些配置真正生效，
后续显示设置（`ui_scale`、`resolution`）也才可能通过配置下发。


### 1.37 显示设置页（Settings/Display）：缩放即时生效、分辨率与刷新率如实标注

**Windows 对照**：Windows 分三层——显示驱动按 EDID 枚举模式并编程 CRTC 切分辨率/刷新率；
系统级 DPI（每显示器 DPI + 用户百分比）让应用按 logical DPI 重画；DPI 变化发 `WM_DPICHANGED` 触发重排。
关键点是**缩放永远是"用更大单位重画"，不是放大位图**。

**赫普的现状（代码级）**
- 模式枚举/设置：引导器已具备（`hepboot_uefi.c:311-441`，`QueryMode` + `SetMode`）；
- 内核侧：`ExitBootServices` 之后无 Boot Services，**运行时无法 SetMode**；
- 缩放：桌面 `hs_sx/hs_sy/hs_s` 已按 1024×768 基准缩放，本次再叠加显式因子 `g_ui_scale_pct`；
- 字体：自研 TTF 光栅化（矢量），放大是重新栅格化，不是位图拉伸；
- EDID/CRTC：**没有**，因此刷新率不可调。

**本次实现（P2）**
- Settings 左侧菜单由 4 项扩到 5 项，新增 `Display`（放在末尾，索引 0–3 的既有语义完全不动）；
- Display 页内容：当前分辨率（`2048 x 1536` + "Set by firmware at boot"）、
  四个缩放档位按钮（100/125/150/200，点击即时生效）、刷新率（"firmware-controlled / no EDID/CRTC driver yet"）；
- 缩放按钮的渲染位置存为 `g_disp_scale_btn_x/y`，命中测试直接复用，**不另算一套坐标**；
- 持久化：写 `/display.conf`（`scale=<pct>` + `resolution=WxH`），启动时在 `hpdesk_load_config` 末尾由
  `hs_display_load()` 覆盖 `g_ui_scale_pct`（优先级高于 `config.hsqml`）。

**验证**
- 100%（默认）：桌面与改动前一致，Settings 菜单出现 Display 项；
- 150%：整体按 1.5 倍放大，Quick Controls 的 6 张卡片与两条滑块排布仍成立、无压盖；
  但左侧 Info 面板内容溢出出现滚动条、底部 dock 被挤 —— 见「遗留」。

**遗留（P1 未完成部分）**
面板内部仍有部分固定像素坐标（侧栏内容行距、天气卡、Settings 内部、命中测试镜像），
在 >100% 时会溢出。真正统一需要把绘制与命中共用一套"逻辑坐标 → 物理像素"transform，
工作量约等于逐面板重排。当前 100% 完全可用，125/150/200 属"可放大但不完美"。


### 1.38 P1 首轮：面板外框钳制，高倍缩放不再把面板推出屏幕

**现象（150% 实测）**：左侧 Info 面板整体放大后**底部被屏幕裁掉**，内容被迫出现滚动条、并挤压底部 dock。
初看像"内部坐标没缩放"，实际不是。

**定位**：`hs_sidebar_render()` 内部坐标已全面 `hs_sx/hs_sy` 化（标签栏、CPU/MEM/DSK 行、行距、条宽都在缩放），
真正的问题是**外框没有屏内钳制**：
- `sy = hs_sy(50) = 150`
- `sh = hs_sy(500) = 1500`
- `150 + 1500 = 1650 > 1536`（屏高）→ 面板下缘跑到屏外。

**修法**：`hs_sidebar_render()` 加外框钳制
- 高度：`sh = min(sh, screen_h - sy - hs_sy(16))`
- 宽度：`sw = min(sw, screen_w * 2/5)`（高倍缩放下左右两栏会互相压盖）

**验证（150%）**：侧栏完整落在屏内，无滚动条；底部 dock 不再被挤；Quick Controls 六卡两滑块正常；
天气卡正常显示 `12 Cloudy`。

**遗留**：顶部灵动岛在高倍缩放下与状态栏胶囊区域重叠（时间上半被裁），
属层叠/位置细节，需单独处理；个别窗口内部（Settings）仍是基准像素。


### 1.39 P1 收尾：灵动岛屏内约束 + 基线居中，高倍缩放顶部不再被裁

**现象（150%）**：顶栏时间上半截被裁，展开态的日期/天气/状态挤成一团。

**两个独立根因**：
1. **位置没有屏内约束**：`island_y = hs_sy(12) = 36`，展开高度 `hs_sy(120) = 360`，
   与状态栏胶囊（`bar_top = hs_sy(8)`、`bar_h = hs_sy(40)`）重叠。
2. **基线语义搞错**：`font64_draw_string()` 的 `y` 是**基线**（`y + font_size` 才是块底），
   旧实现按"文本顶部"做垂直居中，整块向上顶出一个字高，时间上半正好被裁掉。

**修法**：
- 位置：`island_y = max(island_y, bar_top)`，展开高度不超过 `screen_h - island_y - 8`。
- 排版：文本块先算总高 `blk = f_time + 8 + f_date + 8 + f_stat`，
  首行基线取 `块顶 + f_time`，下行按 `cy += 字号 + 间距` 递增。

**验证（150%）**：时间完整居中于胶囊内，日期/天气/状态分行对齐，不再顶出岛外。

**顺带确认**：之前"时间消失"的假象是 Settings 窗口盖住了岛内左侧文本（岛内文本从
`island_x + hs_sx(20)` 起，正好落在窗口范围内），不是渲染缺失。

**P1 结论**：150% 下侧栏、Quick Controls、天气卡、dock、顶栏、灵动岛均可用；
Settings 窗口保持 460×430 基准尺寸（内部坐标同基准，内容不会错位，只是相对偏小）。

---

## 四、回归基线

- 全量干净重建：**0 error / 90 warning**（`-Wunused-parameter` 基线不变；接入 Lua 后仍保持）
- 主机测试：**8/8**
- 文档检查：`python3 tools/check_docs.py` **0 warn 0 error**
- QEMU 冒烟：`tools/dev/smoke-test.sh` 通过（颜色数 912，无致命错误关键字）
- 端到端点击闭环：光标定位 → 点击 Apps 标签 → 页面切换 → 图标悬停底座，逐项截图确认

## 五、遗留

- 光标图形的视觉箭头尖端与逻辑指针点存在约 7~12px 固定偏移（hotspot 语义），
  不影响点击准确性，但视觉上略有偏差，待后续按 xcur hotspot 校准
- 真机（物理鼠标）尚未验证；以上均在 QEMU + XHCI 下取证
- 进程表仍只有 idle，任务管理器的进程列表内容待补
