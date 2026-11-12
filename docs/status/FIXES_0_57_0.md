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


### 1.40 缩放默认跟随分辨率自动（含 200% 不可用的实测结论）

**需求**：界面缩放不要写死 100%，默认跟随显示器分辨率自动决定，用户仍可在 Display 页手改。

**实现**：
- `hs_display_auto_scale()`：以 1024×768 为 100% 基准，按屏高推算 → `by = screen_h * 100 / 768`，
  再吸附到四档 `{100,125,150,200}`。
- `hs_display_load()` 改为返回 `1/0`（是否有用户显式选择）。`hpdesk_load_config` 末尾：
  没有显式配置就走自动，并打印 `[DISPLAY] auto scale=N% (screen WxH)`。
- `g_disp_scale_auto` 标志：Display 页标题显示 `UI Scale: Auto (from resolution)` 或 `Manual`；
  用户点任一档位按钮即置为手动，并写入 `/display.conf`。
- 优先级：`/display.conf` 手选 > 自动 > `config.hsqml` 的 `ui_scale`。

**关键实测：200% 当前布局不成立，自动档上限收到 150%**
2048×1536 按算法得 `by = 200%`，实测截图：
- 侧栏内容宽度需求 `280 * 4 = 1120px`，而外框被钳到屏宽 2/5 = `819px` → CPU/MEM/DSK 进度条冲出面板，
- 天气卡放大到 720×720 溢出右下，Quick Controls 覆盖近半个屏幕，顶栏图标互相压叠。

结论：**150% 是 2048×1536 上当前布局能完整成立的最大档**，因此自动算法上限 clamp 到 150。
200% 档仍保留在 Display 页（手动可选），但需要面板内部布局支持才会好用。

**验证**：启动日志 `[DISPLAY] auto scale=150% (screen 2048x1536)`，桌面与手工设 150% 表现一致：
侧栏完整、Quick Controls 六卡两滑块、天气卡、dock、顶栏图标均正常。


### 1.41 P3 第一步：分辨率可由配置决定（含引导器评分溢出的真 bug）

**目标**：Display 页的分辨率从"只读展示"变成可配置、重启生效。

**能力现状核对**：
- 引导器 `hepboot_uefi.c` 已有 `QueryMode` 枚举、EDID 读刷新率、`SetMode` 切换 —— 通路齐全。
- 内核 `ExitBootServices` 之后没有 Boot Services，**运行时无法 SetMode**，所以分辨率只能由引导阶段决定。
- 内核 `kernel/fs/vfat.c` 是空壳（`vfat_mount` 只填了一段假引导扇区，不读盘不写文件），
  所以内核目前**无法**写 ESP 去改 `HEPBOOT.CFG` —— 这是"Display 页直接点选切换"的最后一道门槛。

**实现**：
1. 引导器支持 `HEPBOOT.CFG` 新增键 `resolution=WxH`，解析后对完全匹配的模式给 50,000,000 分。
2. 引导器把枚举到的模式表导出到内存槽 `0x6100`：`count(4) + N*8 字节 (w,h)`，上限 16 项。
3. 内核 Display 页读取该槽，显示 `Available: 1024x768, 640x480, ...` 与
   `Change: resolution=WxH in HEPBOOT.CFG / then reboot (no runtime SetMode)`。

**顺带发现的真 bug：引导器评分 UINT32 溢出**
`score` 原为 `UINT32`，而"限制最大分辨率"给 `w>2560 || h>1440` 扣 2,000,000 分。
2048×1536 的其它加分合计约 133 万，减去 200 万后为负，在 `UINT32` 下**回绕成约 42 亿**，
反而稳坐第一 —— 也就是说任何高度 >1440 的模式都会赢，`resolution=WxH` 的加分也被它压过。

修法：`score` 与 `best_score` 改为 `INT64`，初始 `best_score = -999999999`。

**验证**：
- `resolution=1024x768` → 日志 `[CFG] resolution=0x400x0x300`，
  `[GFX64] Mode set: 0x400x0x300 pitch=0x1000`，界面按 1024×768 完整重排，自动缩放降为 100%。
- 无配置项（默认）→ 修复溢出后正确选中 **1920×1080**（此前一直是 2048×1536），
  日志 `[DISPLAY] auto scale=150% (screen 1920x1080)`，桌面各面板排布协调。

**行为变化（需知悉）**：默认分辨率从 2048×1536 变为 1920×1080（这正是修复溢出后的正确结果）。
若想回到 2048×1536，在 `HEPBOOT.CFG` 写 `resolution=2048x1536` 即可。

**尚未打通**：Display 页直接点选分辨率（需要内核具备 FAT32 写 ESP 的能力，`vfat.c` 目前是空壳）。


### 1.42 让 200% 真正可用（侧栏内容按可用宽度自适应 + 渲染侧外框钳制补齐）

**背景**：1.40 把自动档上限压到 150%，因为 200% 下侧栏内容冲出面板。1.42 修掉这个根因，恢复 200% 档。

**两个根因**：
1. **进度条宽度写死**：Info 页 CPU/MEM/DSK 三条进度条是 `hs_sx(180)`，200% 下需要 720px，
   而外框被钳到屏宽 2/5 = 819px，扣掉标签列（280）与百分比列之后已无 720 可用 → 条子冲出面板。
2. **渲染侧外框没钳制**：1.38 的钳制只加在了命中测试函数里，`hs_sidebar_render()` 漏了。
   150% 时面板仍是 `150 + 1500 = 1650 > 1536`，底部其实一直被屏裁，只是没滚动条所以看不出来。

**修法**：
- 渲染与命中两处使用同一套外框钳制公式（`sh = min(sh, screen_h - sy - 16)`、`sw = min(sw, screen_w * 2/5)`）。
- 进度条几何改为按面板宽度推导：
  `s_bar_x = sx + hs_sx(70)`、`s_bar_w = sw - hs_sx(70) - hs_sx(20) - hs_sx(52)`（下限 `hs_sx(30)`），
  百分比列右对齐到 `sx + sw - hs_sx(20) - hs_sx(26)`。
- 自动档上限由 150% 恢复为 200%。

**验证（2048×1536，`resolution=2048x1536` → 自动算到 200%）**：
侧栏完整落在屏内（底边圆角可见）、三条进度条与 4%/6% 百分比都在面板内；
Quick Controls 六卡两滑块、天气卡、顶栏时间、dock 把手全部正常。

**遗留**：Settings 窗口仍是 460×430 基准尺寸（内部坐标同基准，不会错位，只是相对偏小）。


### 1.43 撤销 1.42 的"恢复 200% 自动档"：200% 按逻辑画布算结构上装不下双栏

**背景**：1.42 把自动档上限恢复到 200%，交付后用户上传 200% 实机截图反问"这对吗"。
逐条核对确认四类问题：QC 压进侧栏、CPU 行整行空白、`System Info` 只显示 `Syst`、
日期 `October 15, Thursday` 被天气卡压住只露 `…ay`、面板底部与推算不符。

**根因（算术，不是调参）**：逻辑画布 = `1024 × 100 / pct`。200% 时逻辑宽只有 512，
而双栏布局需要 `侧栏 280 + QC 280 + 间距 24 = 584`，**差 72 逻辑像素**，任何排布都装不下。

**修法**：
- `cc_geometry()` 增加"不得压进侧栏"约束：`cx` 不得小于 `侧栏右缘 + hs_sx(24)`，
  宽度按剩余空间收缩（下限 `hs_sx(200)`）。
- 自动档上限由 200% 收回 150%，并把上述算术理由写进注释；200% 保留为手动试验档。

**验证**：150% 下 `gap_x=260`，两栏不重叠；200% 手选时侧栏与 QC 也不再互相覆盖（净空 96）。

**行为变化**：2048×1536 屏幕不再自动进 200%，而是走 150%。


### 1.44 桌面时钟撞进天气卡（高倍缩放下的真实碰撞）

**现象**：200% 下天气卡右侧露出一个孤立的 `ay`，日期整段被卡片盖住。

**根因**：`clock_x: 380`（1024 基准逻辑值）在 200% 下放大成 1520，
而天气卡左缘是 `屏幕宽 - hs_sx(140) = 1488`，时钟区域整体已压进卡片内。

**修法**：`clock_x` 钳在"天气卡左缘 − 时钟区宽"以内；日期字号再按"时钟区域可用宽"反推
（`len × 0.55 × 字号 > 可用宽` 时缩小，下限 `hs_s(10)`）。

**验证**：改后 200% 截图上该 `ay` 消失，日期完整显示在时钟区内。


### 1.45 三组几何数字对齐，并纠正一次读图误差

**做法**：在 `hpdesk_render()` 首帧加 `[GEOM]` 探针（缩放百分比 / 屏幕 / 逻辑画布 /
侧栏原始几何 / QC 几何 / 两栏间距），以及钳制后的 `[GEOM] sidebar clamped`；
另在 `hs_sidebar_render()` 第 60 帧加 `[GEOM-RENDER]`，打印渲染函数**自己算出来**的
`sx/sy/sw/sh/bottom`。

**200% 实测（2048×1536）**：
```
pct=200 logical=512x384 | sidebar x=48 y=200 w=1120 h=2000 | cc x=963 y=432 w=989 h=440 | gap_x=-205
sidebar clamped: w=819 h=1272 right=867 bottom=1472
```

**价值（记一笔教训）**：此前曾据截图量出"侧栏底边 1105，与探针 1472 差 367px"并据此怀疑渲染层
另有逻辑。加了渲染侧探针后，`sx=48 sy=200 sw=819 sh=1272 bottom=1472` 与钳制值完全一致，
证明**那是读图误差，不是代码分叉**。

结论：缩放后的截图必须用 PIL 取像素值，不要靠肉眼在缩放图里量边界。


### 1.46 窗口默认定位统一到真实屏尺寸（A 方案第一步）

**背景**：把 Settings 窗口纳入缩放时，先在 `hs_settings_window_render()` 内引入
`g_set_sc = w->width / 460`，把 23 处几何、31 处 `ry +=`、35 处字号统一乘它，并把窗口尺寸改成
`hs_sx(460)` 再 cap 到屏宽 62% / 屏高 66%。

**第一次失败**：整屏浅紫色块。根因是 `hs_window_auto_pos()` 硬编码 `1024/768`：
窗口宽 1270 时 `nx = 1024 - 1270 = -246`，负坐标把窗口甩出屏外。
修好 auto_pos 后窗口回来了，但实际只有约 590px 宽而非设定的 1270，
说明窗口管理器还有别处的 1024 假设/尺寸上限。

**决策**：判断这已不是"改 60 个常量"，而是要先统一整个窗口系统几何基准
（auto_pos、尺寸上限、背景绘制与裁剪、命中测试四处），于是整体回滚到 `5007530`，
改走 A 方案——先只统一定位层。

**A 方案实施**：重写 `hs_window_auto_pos()`：读真实 `g_shell.screen_width/height`
（为 0 时回落 1024/768），显式坐标分支只做屏内兜底，默认位置改为"居中偏上"
（`nx = (屏宽 − 窗宽)/2 + (idx % 6) * hs_sx(24)`、`ny = 屏高/6 + (idx % 6) * hs_sy(20)`），并 clamp 非负。

**验证**：150% / 200% 双档 Settings 窗口均居中（150% x≈733–1075，200% x≈780–1255），
左面板彻底干净：`System Info`、`CPU 0%`、`MEM 4%`、`DSK 6%` 全部完整可见。

**顺带澄清一个假问题**：Quick Controls 画在窗口之上**是有意设计**——
`hs_window_render_all()` 之后单独调 `hs_control_center_render()`，源码注释即"控制中心绘制在窗口之上"。

**仍待处理**：窗口尺寸上限（设 1270 实测约 590）、窗口背景绘制与裁剪、命中测试，
三处均含 1024/768 类假设。


### 1.47 通知卡锚点改用真实顶栏高度；tab 选中态的对比保底

**通知卡**：`hs_notification_render()` 里 `w=288 h=64 gap=10` 与锚点
`HS_PANEL_HEIGHT + 8 = 48` 全硬编码，200% 下顶栏实际 160px，通知卡正好盖住右上角
wifi / 电池 / 音量图标。改为从 `hs_sy(bar_height) + hs_sy(8)` 起算（内部尺寸仍未随缩放）。

**验证**：150% 实机截图确认通知卡落在顶栏下方（y≈90），图标不再被遮。**200% 档未取证**。

**tab 选中态**：150% 下 Info 选中块不可见。先加"accent 过暗时亮化 + 字色按最终亮度选"，
再补 `0xFF000000`（色板色值是 6 位，alpha 字节为 0）。串口与像素两级证据：
```
theme accent=0xd0bcff panel_bg=0x211f26 tab=0
[TAB] i=0 tx=66 ty=130 tw=222 th=59 bg=0xd0bcff
```
参数全对，但像素恒为 `window_bg`；补 alpha 后仍不变 —— **alpha 假设被否**，当轮以"未解决"如实提交。


### 1.48 tab 高亮定案：贝塞尔细分 `cross²` 溢出 → 圆角填充被截断

**定位方法的转折**：不再靠 screendump 反复重启，改为
① QEMU monitor 直读显存原始像素（`xp /1wx`，`addr = fb + y*pitch + x*4`）；
② 同位置同颜色换成 `gfx64_fill_rect` 做直角对照；③ 在 `vg_fill` 里统计边数与 y 跨度。

**证据链**：
```
显存 (200,160) = 0x001c1b1f（面板底色）        -> 确实没落像素，不是显示/截图丢帧
改直角填充后显存 = 0xffd0bcff                  -> 颜色管线与位置都正确
[VG] #4 nedge=2045 y=130..140                  -> 边数顶到 VG_MAX_EDGE/SEG 上限 2048，
                                                  而 y 跨度只有 11 行（该块应为 222×59）
```

**根因**：`vg64.c` 的 `vg_flat_quad()` 用 `cross * cross <= len2 / 16` 判断"曲线是否够平直"。
`cross` 是坐标差的乘积（量纲 `fixed²`），平方后可达 1e26，**int64 溢出成负数**，
判据恒为"不平直"，曲线一路细分到 4096 段后被 `VG_MAX_SEG(2048)` 截断，
圆弧只剩顶部十几行 → 形状不闭合 → 扫描线奇偶配对失败 → 整块不落像素。

这也解释了此前"同一段代码一次显示一次不显示"：细分是否被截断受内存边界状态影响，不稳定。

**本次处置（保守）**：tab 选中态先用 `fill_rect` 直角填充，保证高亮可见，
显存读出 `0xffd0bcff`、截图目视确认。**视觉代价：选中块四角变方**。

**尝试过的根因修复（未采纳）**：把判据改为等价但不开平方的 `|cross| <= sqrt(len2)/4`，
隔离验证（只改 `vg64.c`、tab 不动）后 **所有圆角填充回归为"只剩描边、内部透明"**
（面板底 `0x1c1b1f` 变壁纸色、QC 按钮变壁纸色）。面积远大于原症状，故回滚。

**最终修复：把细分深度由 12 压到 6**。溢出会让判据恒为"不平直"，此时细分跑到上限；
深度 12 时每曲线 4096 段、4 条边共 16k 段，必然被 `VG_MAX_SEG(2048)` 截断；
深度 6 时满细分也只有 64 段/曲线，总段数落在容量内，形状闭合，圆角完整落像素。
对 ≤40px 的圆角而言 64 段/曲线在视觉上已无差别。

**验证（150%，1920×1080，显存直读）**：
```
tab 选中 (200,160) = 0xffd0bcff   （accent 原色，圆角块）
面板底  (200,600) = 0x001c1b1f    （window_bg，未回归）
QC 按钮 (1300,290) = 0x00d0bcff   （圆角填充正常）
```
截图目视：Info 选中块为圆角亮紫块，面板 / Settings / Quick Controls / 天气卡圆角全部正常。

**遗留**：`cross²` 溢出的判据本身仍未修（现在靠限制深度规避），若将来需要更大半径
或更高精度曲线，应再把判据换成不开平方的形式并单独验证。

**另记一个间歇性异常（未查）**：个别开机帧里，屏幕左上会出现一个约 1015×610 的空心圆角色框
（遮住顶栏中部），同版本另一次开机没有。与本次圆角修复无关，怀疑某个窗口创建后
在完成布局前用默认尺寸画了背景，列为待查。

---

### 1.49 控制中心与通知卡右缘统一到顶栏 margin

**现象**（用户圈图）：150% 下 Quick Controls 明显"浮"在页面中间，右缘 1853，
而顶栏右缘 1892，两者差 39px；右上两条通知还冲出屏幕，时间文本 `06:2…` 被切。

**根因**：三个元素的右侧 margin 各自为政。

| 元素 | 原右边距（逻辑） | 150% 物理 | 右缘 |
|---|---|---|---|
| 顶栏 | `hs_sx(10)` | 28 | 1892 |
| Quick Controls | `hs_sx(24)` | 67 | 1853 |
| 通知卡 | 硬编码 `8` | 8 | 1912（超屏） |

通知卡那个 `8` 根本不是缩量，`x = screen_w - 288 - 8`，卡身右缘 1912 > 1920 从而被切。

**修法**：三处统一到顶栏的 `hs_sx(10)`。
- `cc_geometry()`：`cx = screen_w - w - hs_sx(10)`，收缩分支同步
- `hs_notification_render()`：两处 `x = screen_w - w - hs_sx(10)`

**验证**（150%，1920×1080）：`[GEOM] cc x=1105 w=787` → 右缘 1892，
与顶栏右缘 1892 一致；通知卡完整落在屏内，不再被切。

**说明**：`cc_geometry()` 的 `h` 仍保持基准像素 440（未缩放），
所以 QC 在 150% 下呈"宽扁条"（787×440）。这是内部卡片布局全硬编码、
未整体缩放的直接后果，属窗口/组件纳入缩放那批工作量，本轮未动。

### 1.50 缩放公式语义修正：150% 实际放大 2.81 倍（版式挤不下真正的根因）

**起因**：用户圈出控制中心"跟右上状态栏脱离"，修完右缘对齐（1.49）后，
QC 高度接入缩放立刻变高 837，把天气卡整个盖住。到此确认不是摆放问题。

**根因**：`hs_sx/hs_sy` 把基准分辨率重复乘了一次。

```c
/* 旧 */ 物理 = v * (screen_w / 1024) * (pct / 100)
/* 正确(Windows DIP 语义) */ 物理 = v * (pct / 100)
```

1920×1080 下 pct=150：

| | 旧公式 | 正确 |
|---|---|---|
| 放大倍数 | 1920/1024*1.5 = **2.81 倍** | 1.5 倍 |
| 逻辑画布 | 1024*100/150 = 682×512 | 1920/1.5 = **1280×720** |
| 侧栏宽 | 787 | 420 |

所以"150%"实际是 2.81 倍放大，逻辑画布被压到 682×512 —— 侧栏(500 逻辑)+
天气卡、QC(440 逻辑) 纵向必然装不下。此前 §1.43 得出的"200% 逻辑宽 512 < 双栏 584、
结构上装不下"也是同一个错：200% 正常逻辑画布应是 960×540，双栏 584 装得下。

**修法**：
1. `hs_sx/hs_sy/hs_s` 改为 `v * pct / 100`，与分辨率无关
2. 自动档不再"按最接近档吸附"，改为**只从装得下的档位里取最大**：
   `lim = min(screen_h*100/768, screen_w*100/1024)`，取 `<= lim` 的最大档。
   1920×1080 → lim=140 → **125%**（旧逻辑会选 150%，但纵向只允许 140%，必然挤压）

**连带修正**：
- `cc_geometry()` 高度改用 `hs_sy(cc_height)`（原来保持基准像素，导致 787×440 宽扁条）
- QC 内部导航栏/卡片/滑块/字号全部走 `hs_sx/hs_sy/hs_s`，命中测试同步
- QC 起点避让常驻通知区（否则面板标题被通知卡压住）
- 通知卡 `288×64/gap10` 与内部字号、图标、进度条全部接入缩放

**验证**（1920×1080，自动档 125%）：
```
[DISPLAY] auto scale=125% (screen 1920x1080)
[GEOM] logical=819x614 | sidebar 15,62 350x625 | cc 1558,135 350x550 | gap_x=1193
```
截图：侧栏 / QC / 天气卡(右下 175 宽) / 通知卡四者互不重叠，QC 与顶栏右缘对齐，
tab 选中块圆角可见。

**影响与取舍**：整版 UI 比修复前小（这是正确语义 —— 125% 就是放大 1.25 倍）。
手动选 150%/200% 仍可用，但在 16:9 屏上纵向会超出基准 768，QC 与天气卡会挤压；
4:3 屏（2048×1536）自动档算得 200%，逻辑画布正好 1024×768，不受此限。

### 1.51 撤回 §1.43 的"200% 结构上装不下"：200% 实测可用

§1.43 曾据"逻辑宽 = 1024*100/200 = 512 < 双栏 584"判定 200% 结构上装不下，
据此把自动档上限压到 150%。那组数字是 §1.50 那个错误公式算出来的 ——
正确的 200% 逻辑画布是 1024×768（2048×1536/2），双栏 584 绰绰有余。

**实测**（2048×1536，自动档 200%）：
```
[DISPLAY] auto scale=200% (screen 2048x1536)
[GEOM] sidebar 24,100 560x1000 | cc 1468,216 560x880 | gap_x=884
```
截图：侧栏与 QC 并排不重叠，tab 选中块圆角、QC 与顶栏右缘对齐、
通知卡落位正确（首帧在屏外是滑入动画的起始位移，非缺陷）。

**残余问题（未修）**：QC 底边 1096，天气卡自 ~1090 起，两者仍有约 100px 重叠。
4:3 屏 1536 高对应逻辑 768，而原设计纵向正好占满 768（顶栏36 + QC440 + 天气卡 若干），
再叠上通知区让位就没有余量了。彻底解决需要 16:9/4:3 各一套纵向布局，
或把天气卡也做成可被 QC 顶开的流式布局，属下一批工作量。

### 1.52 Settings 窗口接入缩放（渲染与命中测试共用一套公式）

**背景**：Settings 是最后一个完全不受缩放影响的组件 —— `hs_settings_window_render()`
的 278 行里所有几何与字号都是按 460×430 硬编码的常量，窗口本身却按
`hs_sx(460)/hs_sy(430)` 创建。125% 下窗口 575×537，内容仍按 460×430 画，
右下角必然留一大片空白；这也正是早前那次整体改 `g_set_sc` 的回滚点之一。

**做法**：
```c
static int hs_set_k(hs_window_t *w, int v) { return (v * w->width + 230) / 460; }
```
- 渲染函数内 113 处几何/字号全部改为 `SETK(v)`（宏 = `hs_set_k(w, v)`）
- 保留不缩放的：循环边界（`i < 5`、`i < HS_THEME_PRESET_COUNT`）、
  比较与逻辑量（`shown < 3`、`q < 100`）、数组下标、0/1
- 窗口创建尺寸改为 `hs_sx(460), hs_sy(430)`（两处：4805 / 5948 行）
- **命中测试同步**：菜单区、System 页的 language/driver/notify/storage/registry
  各行、Display 页档位按钮的 `bi / HS_DISP_BTN_GAP` 全部改用 `hs_set_k()`

**验证**（1920×1080，125%）：窗口 575×537，内容铺满，左侧菜单五个条目、
8 个色板圆点、壁纸行、Desktop 3×3 色板位置与间距全部正确，无右下留白。

**说明**：`SETK` 宏只在本函数内有效（函数末 `#undef`），命中测试处用
`hs_set_k(w, v)` 显式调用 —— 就是为了避免"渲染一套坐标、命中另一套"的老问题。

### 1.53 QC 与天气卡的"重叠"定性为浮层遮挡, 加阶梯投影拉开层次

**先纠正 1.51 的表述**：那里说"4:3 屏纵向正好占满 768、再给通知区让位就没余量"，
把重叠归因于缩放。按 1024×768 基准重算：

| 元素 | 位置(逻辑) | 纵向区间 |
|---|---|---|
| 顶栏 | h=36 | 0–36 |
| QC | `cc_y=108` `cc_height=440` | 108–548 |
| 天气卡 | y = 768−160−120 = 488, h=120 | 488–608 |

`weather_margin_bottom=160` `weather_height=120` 也是 QML 默认值，全是基准像素。
**在设计稿本身上这两块就叠 60 逻辑像素（横向叠 120）**，100% 分辨率下同样叠。
缩放只是把既有重叠等比放大，并没有新增冲突。

**结论**：这是浮层语义（下开面板压住桌面组件），不是布局算错；与 Windows 一致。
真正的问题是两块同色圆角卡片叠在一起"看起来像画错了"，属视觉层次问题。

**修法**（只改观感，不动任何几何）：
```c
static uint32_t hs_dim(uint32_t c, int k);   /* 颜色按比例压暗, 保留 k% */
```
在面板本体之前画两层阶梯暗边：

| 层 | 偏移/扩边 | 压暗到 |
|---|---|---|
| 外 | `(-1,+6)` `+5×+8` | 22% |
| 内 | `(0,+3)` `+2×+4` | 48% |

用"面板色 × 系数"而非纯黑，因为 `window_bg` 本身接近纯黑，
纯黑投影在深色壁纸上根本看不见。

**验证**（1920×1080 @125%）：QC 右下出现明显暗边，与天气卡/壁纸分层清楚，
被压住的组件自然退到后面。

### 1.54 窗口几何统一入口 + 修正 [GEOM] logical 误导性打印

**一、hs_window_create 成为窗口几何的唯一入口**

14 个创建点（Settings 2 处、hs_apps.c 6 处、hs_systools64.c 3 处、其余在 hpdesk.c）
原本各传各的裸像素。现在统一在 create 里：

```c
int max_w = screen_width  - hs_sx(40);
int max_h = screen_height - hs_sy(80);
if (max_w > 0 && w > max_w) w = max_w;
if (max_h > 0 && h > max_h) h = max_h;
if (w < hs_sx(200)) w = hs_sx(200);
if (h < hs_sy(120)) h = hs_sy(120);
/* 并打印 [WM] create <title> WxH */
```

**为什么不在这里顺手 w = hs_sx(w)**：各 app 的内容渲染仍是按基准像素画的
（Terminal/Files/Task Manager/Editor/Image/Viewer/Device Manager/Disk Manager/Log Viewer
都还没接缩放）。只放大窗口不放大内容，会重现 §1.5 那次的"窗口变大、内容只占左上角、
大片留白"。Settings 之所以能放大，是因为它的**内容渲染和命中测试一起改了**（§1.52）。

所以本轮只做三件事：单点钳制、单点可观测、语义写进注释。

**二、[GEOM] 的 logical 一直是错的**

```c
/* 旧 */ int lw = 1024 * 100 / pct;   /* 1920x1080@125% 打印 819x614 */
/* 新 */ int lw = screen_width  * 100 / pct;   /* 1536x864 */
```

1024 是设计基准，不是物理宽。这个打印正是 §1.43/§1.51 两次错误结论的数字来源，
一直没被纠正，现在顺手修掉。

**验证**：`[WM] create Settings 575x537`、
`[GEOM] pct=125 screen=1920x1080 logical=1536x864`，桌面无残留空框。

### 1.55 光标包终于真的加载了（打包缺失 + 静默回退）

**症状**："用户给的鼠标包没加载"，指针一直是最初的内置位图。

**根因**：`cursor64.c` 解析器、`assets/cursors/*.xcur`（17 个，含 24/32/48 三档名义尺寸）
都早就存在，`hs_cursor_init_once()` 也调 `cursor64_load_file("/usr/share/cursors/default.xcur")`。
但 **`tools/build_uefi_dual.py` 从未把 `assets/cursors` 写进根分区** —— 目标路径根本不存在，
`ext2_64_read_file()` 返回负值，`cursor64_load_blob()` 失败，`cursor64_draw()` 的
`if (!g_ready)` 分支**静默回退内置位图**，全程没有任何日志。

**修法**：
1. 打包：把 `assets/cursors/*.xcur` 写入 `/usr/share/cursors/`
2. 留痕：`[CURSOR] load rc=%d want=%d ready=%d nominal=%d hot=(%d,%d)`
3. 尺寸随 DPI：`want = cursor64_pick_size(...) * pct / 100`，指针是 DIP 尺寸

**验证**：
```
[CURSOR] load rc=0 want=30 ready=1 nominal=32 hot=(1,0)
```
125% 下 want=30，选中最近的 32 档；截图放大后是标准 Xcursor 白色箭头带深色描边。

### 1.56 Terminal 窗口与内容一起接入缩放

**为什么只做一个 app**：`hs_window_create` 里若统一 `w = hs_sx(w)`，而某 app 的内容仍按
基准像素画，就会重现"窗口变大、内容只占左上角"的留白（§1.5 那次）。所以必须
**窗口尺寸和内容渲染同时改**，只能一个一个来。

**做法**：Terminal 内容按**窗口自身尺寸**缩放，不依赖全局缩放因子：

```c
#define TX(v) ((v) * w->width  / 520)
#define TY(v) ((v) * w->height / 340)
```

基准 520×340 时两式恒等于原值，行为向后兼容；窗口被放大时内容同比例放大。
行高、可见行数、输入行位置、光标块位置全部走这两个宏。

跨文件拿不到 `hs_sx`（它是 hpdesk.c 的 static），所以在 hpdesk.c 暴露：

```c
int hs_ui_sx(int v) { return hs_sx(v); }
int hs_ui_sy(int v) { return hs_sy(v); }
```

窗口创建改为 `hs_ui_sx(520), hs_ui_sy(340)`。

**验证**（1920×1080 @125%）：
```
[WM] create Terminal 650x425
```
截图：黑底铺满整窗，绿字与字号同步放大，底部输入行贴底，光标块跟在提示符后，无留白。

### 1.57 Files 窗口与内容接入缩放（行高这一项是关键）

Files 的 `f_render()` 有 203 行、`FS_ROW_H 18` 被渲染和命中测试共用，所以不能只改渲染：

```c
static int f_sc(int v) { return v * (f_win ? f_win->width : 460) / 460; }
#define FS(v) f_sc(v)
static int f_row_h(void) { return f_sc(FS_ROW_H); }
```

三个地方必须共用同一个行高，否则就会出现"看得见点不到 / 点错行"：

| 位置 | 改动 |
|---|---|
| `f_render()` | 111 处几何/字号 → `FS(v)`；`row_h = f_row_h()` |
| `f_visible_rows()` | `h - FS(26) - FS(40)`、`(ch - FS(52)) / f_row_h()` |
| 命中测试 | `cy = y + FS(26) + FS(26)`、`rel = (y - cy) / f_row_h()`、路径栏 `FS(26)/FS(20)/FS(6)` |

**批量替换踩到的坑**（这类函数里数字语义混杂）：
- `const char *ext = nm + len - 4` 中的 4 是**字符串偏移**不是像素，误改成 `FS(4)` 会让扩展名判断整体错位（截图中文件图标会全变成未知类型）
- `if (maxn < 4) maxn = FS(4)` 的 4 是**字符个数**，同上
- `(cw - FS(100)) / FS(7)` 的 7 是**字符宽**，这个必须缩放（字号变了字符宽也变）

规律：位置/尺寸/字号/字符宽要缩放；字符串容量、数组下标、循环边界、比较值、字符个数不缩放。

**验证**（1920×1080 @125%）：`[WM] create Files 575x450`，
路径栏、USB 状态按钮、列表行高、图标、右侧大小列、底部状态栏全部同比放大，无留白。

### 1.58 Task Manager 接入缩放（列偏移必须同一比例）

`m_render()` 192 行，列位置是硬编码的 `c_name=cx+38`、`c_cpu=cx+150`、
`c_mem=cx+202`、`c_state=cx+250`（基准 520 宽窗口）。

**缩放的关键不是字号，而是列偏移**：如果只放大字号、列偏移留在原值，
NAME 列文字会变宽而 CPU 列起点不动，两列直接叠在一起。

```c
#define MC(v) ((v) * w->width / 520)
int c_name  = cx + MC(38);
int c_cpu   = cx + MC(150);
int c_mem   = cx + MC(202);
int c_state = cx + MC(250);
```

表头与数据行共用同一组 `c_*`，所以只要这组偏移同比缩放，对齐关系自动保持。

顺带说明：这张表之前那次"PID/NAME/CPU/MEM/STATE 不对齐"，根因是**把整行拼成一个含空格的
字符串一次性画**——`font64` 是比例字体，空格的宽度和 `0`/`16`/`100%` 的宽度不同，
于是每行的列位置随数值位数漂移。V0.57.1 已改为分列绘制，本轮缩放后依然是分列，
对齐没有回退。

**验证**（1920×1080 @125%）：窗口 650×475，五列表头与其下数据完全共线；
曲线区、分隔线、KERNEL SERVICES 段、底部提示条位置均同比。

### 1.59 三个系统工具窗口接入缩放（语义误替换集中爆发点）

`hs_systools64.c` 的 Device Manager / Disk Manager / Log Viewer 各自一套：

```c
static int st_dev_sc(int v) { return v * (st_dev_win ? st_dev_win->width : 560) / 560; }
#define SD(v) st_dev_sc(v)
```

行高必须和 `st_dev_rows()` 共用同一个公式（`(ch - SD(34) - SD(24)) / SD(15)`），
否则会出现"看得见点不到"。Log Viewer 基准 620、行距 14，同理走 `SL`。

**这一轮批量替换踩到三个语义误替身，全都编译通过、只有跑起来才看得出错**：

| 误改 | 这个数字是什么 | 后果 |
|---|---|---|
| `st_hex(..., SD(2), ...)` | PCI 地址的**十六进制位数** | 125% 下位数变 2.5→整型截断，地址打印错位 |
| `if (ssize == 0) ssize = SK(512)` | **扇区字节数** | 容量计算全错，磁盘大小显示成 256MB 之外的值 |
| `pct = used_mb * SK(100) / tot_mb` | **百分比分母** | 占用率算错，进度条长度不对 |

这三处都满足"数字看起来像像素、实际是语义常量"。**结论：凡是用 `_sc` 包裹前要先问一句
"这个数是被绘制的长度，还是被计算的量"，后者一律不包。** 位数、扇区大小、百分比、超时毫秒、
容量上限都属于后者。

**验证**（1920×1080 @125%）：`[WM] create Device Manager 700x500`、
`Disk Manager 700x525`、`Log Viewer 775x525`；三个窗口的标签栏、行高、字号、
磁盘进度条、分区表、日志行距全部同比放大。

### 1.60 Editor / Viewer / Image 接入缩放（共用 render 的基准问题）

Editor：基准 640，`EC(v)` + `ed_line_h()`。行高必须和 `vis` 计算共用：
`vis = (w->height - EC(82)) / ed_line_h()`。行号槽 `EC(ED_GUTTER)` 与光标块
`col * EC(7)` 也走同一套，否则光标会跑到文字左边。

**Viewer 与 Image 共用同一个 `hs_viewer_render`，但基准宽不同**（460 / 520）。
如果只按一个基准写 `v * w->width / 460`，520 宽的 Image 窗口在 100% 缩放下会被
放大 1.13 倍，破坏"基准尺寸恒等于原值"。解法是打开时记录基准：

```c
static int v_base_w = 460;
#define VC(v) (v * (v_win ? v_win->width : v_base_w) / (v_base_w ? v_base_w : 460))
```

`v_base_w` 在打开 Viewer 前置 460、打开 Image 前置 520。

**本轮的语义误替身**（和前两轮同型）：`maxc` 的 `8`（字符列下限）、`110`（字符列上限，
受 `lb[128]` 容量约束）、`7`（半角字符宽）。前两个不能缩放，第三个必须缩放。

**验证**：`[WM] create Editor 800x550`，标题/路径/消息/行号槽/光标位置均同比；
Viewer 与 Image 共用同一套缩放，本轮仅做编译与代码核对，未截图（缺自动打开路径，
需从 Files 手动点开文件才能触发）。

### 1.61 缩放规则收成单点 + 标题栏接入缩放（9 窗口改造收尾）

**一、七个 DS 宏收成一个实现。** 9 个窗口接完回头看，实际只有三种需求：

| 需求 | 覆盖 | 做法 |
|---|---|---|
| 单基准等比 | 7 个窗口 | `hs_win_k(w, base_w, v)` |
| 横纵分别 | Terminal | 保留 `TX/TY`（TY 按高度） |
| 双基准共用 render | Viewer/Image | 保留 `v_base_w` |

所以只在 `hpdesk.c` 留一个实现，其余全部改为调用它：

```c
int hs_win_k(hs_window_t *w, int base_w, int v)
{
    int bw = (w && w->width > 0) ? w->width : base_w;
    if (base_w <= 0) return v;
    return v * bw / base_w;
}
```

`f_sc / e_sc / st_dev_sc / st_disk_sc / st_log_sc / hs_set_k / MC / TX` 全部转发到这里，
调用点一行未改 —— 规则改了只改这一处。

**二、标题栏高度是"版式正确性"最后一块。** 标题栏一直固定 24 物理像素，
内容放大后相对变窄，125% 下标题与会挤按钮。改为：

```c
int th = hs_sy(24);
```

同时按钮几何 `-68/-44/-20`、图标 `5/4/16`、标题 `26/6/12`、截断字符宽 `150/7`
全部按 DPI 缩放。

**三、命中测试必须同步**（老坑）。`hpdesk_mouse_down()` 里标题栏判定
`y <= w->y + 24`、按钮区 `-68/-44/-20 + 24` 若不同步，就是"按钮画得大、点得小"。
本轮已改为 `hs_sy(24)` / `hs_sx(68..)` / `hs_sx(24)`。

**验证**（1920×1080 @125%）：Files 575×450、Task Manager 650×475、Editor 800×550，
标题栏 30px 高，三按钮与标题文字同比，内容起点 32.5 仍留 2.5 间隙。

### 1.62 全窗口缩放回归（100% / 125% / 200% 三档）

9 个窗口接完缩放后做的一次横向回归。判定标准有两条：
**① 基准档尺寸必须等于原始值**（证明没改坏旧版式）；**② 放大档内容与窗口同比**（证明没留白）。

| 档位 | 屏幕 | logical | 窗口实测 | 结论 |
|---|---|---|---|---|
| 100% | 1024×768 | 1024×768 | Settings 460×430、Files 460×360、Device 560×400、TaskMgr 520×380、Editor 640×440 | **全部等于基准原值** ✅ |
| 125% | 1920×1080 | 1536×864 | Settings 575×537、Terminal 650×425、Files 575×450、TaskMgr 650×475、Editor 800×550 | 同比放大 ✅ |
| 200% | 2048×1536 | **1024×768** | Settings 920×860、Files 920×720、Device 1120×800、TaskMgr 1040×760、Editor 1280×880 | 同比 2×，版式成立 ✅ |

200% 档的三条关键探针：

```
[DISPLAY] auto scale=200% (screen 2048x1536)
[GEOM] pct=200 logical=1024x768 | sidebar 280x500*2 | cc x=1468 w=560
```

`logical=1024x768` 是缩放公式修对之后的值 —— 早前错公式（`v * (screen_w/1024) * (pct/100)`）
在这里会算出 512×384，那是"200% 装不下"这个错误结论的唯一来源。

**这一轮没发现新的版式缺陷。** 100% 档的窗口重叠是摆位拥挤（1024×768 塞 4 个窗口），
200% 档 Editor 底部被裁同理，都不是缩放问题。

### 1.63 vg_flat_quad 判据根治（不再是"靠深度 6 规避"）

§1.47 那次只是把细分深度从 12 压到 6，绕开了 `cross²` 溢出，判据本身是错的。
本轮按量纲把它算清楚。

**问题**：`fixed_t` 是 `1<<16` 缩放，长度 L 存成 `L·F`。

```
cross = (cx-ax)·dy - (cy-ay)·dx   →  L²·F²
len2  = dx² + dy²                 →  L²·F²
```

旧判据 `cross*cross <= len2/16` 左边是 `L⁴·F⁴`，右边是 `L²·F²` —— **量纲差 F²**，
而且 `L⁴·F⁴` 在 40px 圆角上就够到 1e26，int64 直接溢出成负数 → 判据恒假 →
每条曲线细分到 4096 段 → 被 `VG_MAX_SEG` 截断 → 圆弧只剩顶部十几行 →
扫描线奇偶配对失败 → 整块不落像素。

**修法**：不要平方，用一次除法把叉积归一化成真实距离。

```c
len = vg_isqrt64(len2);             /* = |AB|, 即 L·F */
cross = |cross|;
if (cross / len <= FIX1 / 4)        /* 0.25 px */
    → 平直
```

`cross / len` = `L²F² / (L·F)` = `L·F` —— 正好就是"偏离距离的 fixed 表示"。
全程只有一次除法，没有平方，所以不可能溢出。`vg_isqrt64` 用牛顿法。

`vg_flat_cubic` 的 `(c1c²+c2c²)*64 <= len2` 是同型错误，一并改成
`c1c/len` 与 `c2c/len` 分别比阈值。

**深度恢复 12**：判据正确后，40px 圆角只需 2~4 层就收敛，深度 12 只是安全上限，
不再会成为"必须压低"的约束。

**验证**（1920×1080 @125%）：侧栏面板圆角、Info tab 选中块（此前整块不落像素）、
QC 六卡与两滑块圆角全部实心且边缘平滑，没有出现"只剩描边"。

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

## 1.64 Display 页"点选分辨率"：ESP FAT32 最小读写 + 重启生效

**需求**：Display 页能把分辨率改掉。但内核已过 `ExitBootServices`，运行时没有
`SetMode` 能力（`hpdesk.c:2082` 注释与页面提示都写着这一点），所以只能把
`resolution=WxH` 写进 ESP 上的 `/HEPBOOT.CFG`，**重启生效**。

**为什么新建 `hs_fat32.c` 而不是复用**：`ext2_64` 只能写根分区，ESP 是 FAT32，
`vfat.c` 是空壳。存储层有 `storage_hal_write(lba, count, buf)`，所以写入能力本身是有的，
缺的只是 FAT32 这一层。

**模块刻意做得窄**（`kernel/arch/x86_64/hs_fat32.c`）：

- 只认 GPT 里的 EFI System Partition（类型 GUID `C12A7328-F81F-11D2-BA4B-00A0C93EC93B`，
  字节序 `28 73 2A C1 1F F8 D2 11 BA 4B 00 A0 C9 3E C9 3B`），扫前 16 项；
- BPB 逐项校验（`bs` 512–4096 且为 2 的幂、`spc` 1–128、`rsvd`、`nfats`、`fatsz`、`rootc >= 2`），
  任一项不合法整体放弃，不做"尽力而为"的写；目前只支持 512B 逻辑扇区；
- 只遍历根目录 FAT 链（上限 64 簇）找 8.3 短名，跳过 LFN / 已删除 / 卷标；
- **只在"新内容 ≤ 原簇容量"时覆盖写**，不分配新簇、不改 FAT 链 ——
  分配簇要动 FSInfo 与备份 FAT，那是能写坏引导盘的路径，宁可返回错误；
- 写回后**必须更新目录项 off 28 的文件大小**，否则 bootloader 读到旧长度会截断。

**API**：`hs_fat32_read(name83,out,max)` / `hs_fat32_write(name83,data,len)` / `hs_fat32_selftest()`。
Makefile 第 37 行加入 `hs_fat32.c`（显式源文件列表，非 wildcard）。

**实测（读）**：
```
[FAT32] esp=2048 data=6114 root=2 clus=512
[FAT32] HEPBOOT.CFG(82): # HepuBoot UEFI Configuration v0.57.0|timeout=5|default=0|kernel_arch=64|cmdline=|
```

**实测（写）**：读 82B → 追加 `resolution=1920x1080` → 写回 103B → 内核读回 103B 一致；
关机后外部 `mtools` 读回内容一致 → FAT 结构对 bootloader 与第三方工具都成立。

**Display 页 UI**：

- 原来那两行说明文字（"Change: resolution=WxH in HEPBOOT.CFG" / "then reboot"）
  替换为 **2×2 分辨率按钮组**（`1024x768 / 1280x720 / 1920x1080 / 2048x1536`），
  按钮宽 `SETK(132)`、横向 gap `SETK(140)`、纵向 gap `SETK(32)`；
  内容区宽约 290，4 个完整标签一行放不下，所以是 2×2 而不是 1×4。
- 选中态按**当前实际屏分辨率**匹配（不是按配置里的字符串），所以刚开机就能看到当前档高亮。
- 点击命中另设 `g_disp_res_btn_x/y`，用 `hs_set_k(w, ...)` 做行/列整除定位，
  与渲染共用同一组常量 —— 避免"画得大、点得小"。
- 写入后页面显示 `Saved. Reboot to apply.`，串口打印 `[DISPLAY] resolution -> WxH written`。

**关键实现细节：逐行改写而不是整文件覆盖**

`HEPBOOT.CFG` 里还有 `timeout/default/kernel_arch/cmdline`，整文件覆盖会把引导配置清掉。
`hs_display_set_resolution()` 逐行扫描：命中 `resolution=` 前缀就**整行替换**，其余行原样拷贝，
没有该键才在末尾追加。测例（原文件已含 `resolution=1920x1080` 且 `cmdline=` 在其后）：

```
写入前: # ...v0.57.0 / timeout=5 / default=0 / resolution=1920x1080 / cmdline=
写入后: # ...v0.57.0 / timeout=5 / default=0 / resolution=2048x1536 / cmdline=
resolution 行数 = 1        （没有重复追加，其它键顺序与内容不变）
```

**端到端验证（重启链路）**：把 cfg 改成 `resolution=2048x1536` 后重启，
串口输出 `[DISPLAY] auto scale=200% (screen 2048x1536)`、
`[GEOM] pct=200 screen=2048x1536 logical=1024x768`，桌面版式正常 ——
"点选分辨率 → 重启生效"这条链路是通的，不是只写了文件。

**风险与边界**：这是写引导分区的操作。当前实现只覆盖**已存在**的小文件、
不扩簇、不改 FAT 链，失败时返回错误码而不重试；`HEPBOOT.CFG` 只有几十字节，
远小于一个 512B 簇，正常路径不会触发任何簇分配。若将来要写超过一簇的文件，
必须先补 FSInfo 与备份 FAT 的维护，那应当另开一轮并单独验证。

## 1.65 纵向设计预算 768 → 720：16:9 屏拿回 150%

**矛盾**：设计基准一直是 1024×768（4:3）。自动档公式
`lim = min(screen_h*100/768, screen_w*100/1024)` 在 1920×1080 上算出 140，
吸附到 125% —— 1080p 屏**永远拿不到 150%**。150% 下 logical 是 1280×720，
纵向比 768 预算少 48，被判"装不下"。

**为什么不是两套布局**：9 个窗口的缩放刚收成 `hs_win_k(w, base_w, v)` 单点，
按比例分叉出两套纵向排布，等于把刚统一的东西又拆开；而且每个窗口、每块面板
都要维护两份。改预算只需要动一个数，且对 4:3 屏不产生溢出。

**为什么 720 是安全的**（三块屏的账）：

| 屏 | 档位 | logical | vs 720 | 结果 |
|---|---|---|---|---|
| 1024×768 | 100% | 1024×768 | +48 | 纵向多 48，留白不溢出 |
| 2048×1536 | 200% | 1024×768 | +48 | 同上 |
| 1920×1080 | 150% | 1280×720 | 0 | 刚好卡住，首次可用 |

200% 在 1080p 上是 960×540，改预算也救不回来 —— 那是物理限制，
1080p 本来就不该给 200%；若真要，得让面板高度可弹性压缩、内容区可滚动，
那是另一轮工程量，不在本次范围。

**改动**：`hs_display_auto_scale()` 的 `by_h` 分母 768 → 720，
并把理由写进函数注释（避免下次有人"顺手改回 768"）；
`g_shell.scale_y` 的注释同源更新。

**三档实测（QEMU，串口 + 截图）**：

```
100%  [GEOM] pct=100 screen=1024x768  logical=1024x768 | sidebar 12,50 280x500 | cc 734,108 280x440
150%  [GEOM] pct=150 screen=1920x1080 logical=1280x720 | sidebar 18,75 420x750 | cc 1485,162 420x660
200%  [GEOM] pct=200 screen=2048x1536 logical=1024x768 | sidebar 24,100 560x1000 | cc 1468,216 560x880
```

150% 截图逐项核对：侧栏、Settings、QC 六卡 + 两滑块、右下天气卡（bbox 到 y=1019，
屏高 1080）全部在屏内，纵向没有溢出，字号/行距同比放大。

**150% 首帧的两个非缺陷现象**（记录以免下次误判为回归）：

- 右上通知卡看起来"冲出右缘"：那是滑入动效的前 10 帧（`x += slide`，
  slide 最大 `w+16`），动画完成后右缘留在 `hs_sx(10)` 处；
- Settings 左外侧出现一小段紫色圆弧：与 §1.48 那个偶发空心圆角框同类，
  本次 200%/100% 未复现，按偶发处理，未在本轮改代码。

**风险**：720 预算下 150% 是全屏最紧的一档（纵向余量为 0）。
若后续新增纵向元素（更高的通知栈、更多 QC 卡），150%@1080p 会先溢出。
届时优先做法是让该元素按可用高度压缩，而不是把预算再调小。
