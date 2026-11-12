---
title: FIX_SECURITY_BUNDLE_A_R4
status: active
sys_version: 0.58.0
updated: 2026-11-10
owner: security
source_of_truth: null
supersedes: null
---

# 第四轮修复 · 组合 A「安全止血包」（6 P0 + 12 P1，共 18 项）

## 范围与边界

本轮只处理 R4 审计（131 条）中风险最高、改动最小的 18 项：6 个 P0 与 12 个
一行至十行级 P1。遵循 `docs/security/BUG_FIX_GUIDELINES.md`：证据优先、最小修改、
每项独立可 revert。

**明确不做（留待用户拍板）**：组合 B（SMP 硬化）、组合 C（真机 AHCI）、
组合 D（文档对齐）；32 位遗留树归档、window64 处置、镜像两路径统一、USB 事件
驱动重构等删除/架构类项。

**硬约束执行情况**：版本号保持 **0.54.11 未 bump**；仅本地 commit，**未 push**；
未在任何输出回显凭据；权限闸本轮一律用 **PID==0 临时闸**，未搭 cred/capability 大体系。

基线：HEAD=`4fbafb5`，修复前工作区干净，主机测试 6/6。

---

## 一、P0 六项

### INT-01　LAPIC 定时器校准到真实 1kHz

- **位置**：`kernel/arch/x86_64/apic64.c`
  - 校准函数 `apic64_calibrate_timer()`：520–552
  - 窗口常量 `CAL_WINDOW_MS`：507；结果变量 `g_apic_timer_hz`：511
  - 初始化 `apic64_init_timer()`：555–584；周期 LVT 写入：582；ICR 写入：583
- **证据/根因**：
  - 历史硬编码 `ICR=6250` 假设 100MHz 总线；QEMU 实测总线约 400MHz，致 tick 漂移
    到约 4kHz，KVM/真机可漂移至 40kHz。
  - 第一版校准用 PIT channel-2 的 GATE2/OUT2 门控。实测该门控窗口与 1193182Hz
    假设不一致：门内 LAPIC CCR 递减 625296（标称 10ms 应对应 10 个 ICR 周期），
    但 LAPIC/PIT 投递率稳定在 **0.62**（多窗口 0.634/0.617/0.623，非随机丢中断），
    即 GATE2 实际窗口约 1.6 倍标称值，校准高估约 1.6 倍。
  - 另发现并修复一个子缺陷：校准函数内部把 LVT 改写为一次性模式，若周期 LVT
    在校准前写入会被覆盖，定时器只触发一次（`g_kernel_tick64` 卡在 1）。
- **修复**：
  - 改用 **PIT channel-0 软件 tick（1ms/tick，晶体 1193182Hz 分频，已确认精确）**
    作为基准，对齐 tick 边界后测量 50ms 窗口内 LAPIC CCR 递减量，64 位运算求
    实际计数频率（`hz = ticks * 1000 / actual_ms`）。中断保持使能——channel-0
    中断不影响 LAPIC 自由计数。
  - 校准期 LVT 置屏蔽位（bit16），CCR 照常递减但不投递；校准返回后再写周期
    LVT（`(1<<17)|0x40`，582 行），最后写 ICR（583 行），彻底避开 LVT 覆盖问题。
  - 校准失败保留保守回退（ICR=6250，PIT 仍投递调度 tick，系统不停摆）。
- **验证**：见文末「验证总览」。多窗口 LAPIC/PIT 比值 994.6–1014Hz，客户机时间
  内落到 1kHz。
- **回退**：`git checkout` 本文件即可恢复硬编码 ICR=6250 路径；校准失败路径已内置。

### SC-01　writev 任意用户指针/无界长度

- **位置**：`kernel/arch/x86_64/syscall64_full.c:1214–1264`；常量 1210–1213
- **证据/根因**：旧实现在 ring0 直接解用户 `iov`/`iov_base`，`iovcnt` 与总长度
  无任何上限，可越界解内核地址并可无界拷贝。
- **修复**：
  - 新增 `WRITEV_MAX_IOV=1024`、`WRITEV_MAX_TOTAL=1MB`、`WRITEV_IOV_CHUNK=64`、
    `WRITEV_BUF_CHUNK=512`（1210–1213）。
  - 入口 `hepu_is_user_address(iov, iovcnt*16)` 校验整窗（乘法用 64 位，含溢出防护）。
  - 分批 `copy_from_user` 取 iovec（栈 `k_iov` 1KB）；窗口外 `iov_base` 保持原
    `continue` 语义（1235）；每段数据分块 `copy_from_user` 到 512B 栈缓冲再输出，
    总字节封顶 1MB（1238–1259）。
- **验证**：主机 uaccess 测试通过；QEMU 运行写路径不崩。
- **回退**：revert 本函数至 `4fbafb5` 版本。

### SC-02　clone 子线程 TID 裸写 / TLS 裸写

- **位置**：`syscall64_full.c:915–953`；child_tid 938–942；tls 946–950
- **证据/根因**：旧实现对 `child_tid` 做 ring0 裸写，用户传内核地址即构成 4 字节
  任意内核写；`tls` 直接 wrmsr 到 FS_BASE，可指向内核/MMIO。
- **修复**：
  - child_tid 写前 `hepu_is_user_address(child_tid,4)` 校验，改走 `copy_to_user`
    （939–941），删除裸写。
  - tls 加 `hepu_is_user_address(tls,1)` 后保留 wrmsr（947–949），统一记账留后续
    架构轮。
- **验证**：主机 uaccess 测试通过；QEMU 不崩。
- **回退**：revert 本函数。

### SC-03　reboot 无权限闸

- **位置**：`syscall64_full.c:2608–2620`；闸 2613–2614
- **证据/根因**：reboot 系统调用无任何调用方判定，任意用户进程可触发整机复位。
- **修复**：函数入口加 `user_process_get_current_pid() != 0 return -1`（2614）。
- **验证**：见文末 PID 语义说明；内核内文本 shell/安装路径不经本分发，不受影响。
- **回退**：删除 2613–2614 两行。

### SC-04　mkfs 仅靠源码内魔数鉴权

- **位置**：`syscall64_full.c:480–490`；闸 484–485；魔数 486
- **证据/根因**：魔数 `0x4D4B4653` 是源码内可见常量，单独不足以鉴权。
- **修复**：在魔数校验之外加 PID==0 临时闸（485），魔数保留为第二道（486）。
- **验证**：见 PID 语义说明。
- **回退**：删除 484–485 两行。

### SC-05　sigaction 把 act 指针当 handler 存储

- **位置**：`syscall64_full.c:1347–1377`；结构 1347–1352；函数 1353–1377
- **证据/根因**：旧实现把 `act` 结构指针本身当作 handler 保存，信号投递时跳到错误
  地址。投递路径 `process64_full.c:791` 本就正确使用存储的 handler，修复存储即修复投递。
- **修复**：
  - 新增 `struct k_sigaction`（handler 8B + mask uint64[16]=128B + flags int +
    restorer 8B，共 152B），对齐 glibc x86_64 布局（handler@0/mask@8/flags@136/restorer@144）。
  - `copy_from_user` 取真实 `sa_handler`（1362）；特殊值 0=SIG_DFL、1=SIG_IGN 放行，
    其余 `hepu_is_user_address` 校验后 `process64_set_signal_handler`（1365–1366）。
  - oldact 用 `process64_get_handler` 回填后 `copy_to_user`（1368–1374）。
- **验证**：QEMU 信号路径不崩；主机 uaccess 测试通过。
- **回退**：revert 本结构与函数。

---

## 二、P1 十二项

### MM-01　COW 缺页丢失 NX 位
- **位置**：`kernel/arch/x86_64/pmm64.c:116`
- **证据**：旧 `pt[si]=(old_pte&0xFFF)|new_page|0x2` 丢弃 bit63 NX；与
  `vmm64_try_cow`（vmm64.c:457）口径不一致。
- **修复**：额外 `|(old_pte&(1ULL<<63))` 保留 NX。
- **回退**：删去该掩码项。

### MM-02　用户栈字符串区越界
- **位置**：`kernel/arch/x86_64/user_stack64.c:228`（strbuf 定义 224）
- **证据**：`strbuf[512]` 填充前无上限校验，`strings_len` 已在上方按真实写入量计算。
- **修复**：填充前加 `if (strings_len > sizeof(strbuf)) return 0`。
- **回退**：删该判断。

### NET-01　TCP 发送成功判据恒假，序列号不推进
- **位置**：`kernel/arch/x86_64/net64_tcp.c:159`
- **证据**：已确证 `ip64_send→eth64_send` 成功返回正字节数（非 0），旧 `if(sret==0)`
  恒假，致序列号永不推进。
- **修复**：改 `if (sret > 0)`。
- **回退**：改回 `==0`。

### NET-02　纯 ACK 静默丢弃，拥塞/RTT 不更新
- **位置**：helper `tcp_process_ack` 275；调用 370、374
- **证据**：旧代码 ESTABLISHED 下纯 ACK 分支缺失，ACK 被静默丢弃。
- **修复**：新增 static helper（RTT 指数平滑、清 unacked_seq/retries、慢启动 cwnd），
  data 路径与新增的纯 ACK 分支（`else if(flags&TCP_ACK)`）均调用。
- **回退**：revert helper 与两处调用。

### NET-05　TCP 接收缓冲填满后新数据静默丢弃
- **位置**：`net64_tcp.c:243–250`
- **证据**：读取后只推进 `rx_read`，`rx_len` 永不回落，缓冲填满后新数据无法进入。
- **修复**：读毕若 `rx_read>=rx_len` 两游标归零；否则把剩余未读数据 `tcp_memcpy`
  压缩到缓冲头部并复位游标。
- **回退**：revert 243–250。

### FS-03　块设备部分写失败后脏读
- **位置**：`kernel/arch/x86_64/blk_cache64.c:115`（前向声明）、127（调用）
- **证据**：raw write 失败直接返回，缓存仍保留旧/脏数据，后续读得到错误内容。
- **修复**：失败时先 `blk_cache_invalidate(lba,count)` 再 return -1。
- **回退**：删该调用与前向声明。

### FS-04　free_inode inode 表块 32 位截断
- **位置**：`kernel/shared/fs/ext2_algo.c:625–626、647–648`（free_inode 603–667）
- **证据**：旧 `uint32_t it_block=itb+(inode_byte/bs)` 先截断后使用，大盘可越界；
  与 alloc/read/write_inode（115–117、144–146）口径不一致。
- **修复**：先算 `uint64_t itb64`，用其做 `<s_blocks_count` 校验后再截断。
- **回退**：revert 两处。

### FS-05　写文件末尾偏移 32 位溢出
- **位置**：`kernel/arch/x86_64/ext2_64.c:1319`；引用 1456、1465–1466、1478
- **证据**：旧 `uint32_t end_offset=offset+size` 可溢出，影响合并判定与 i_size 更新。
- **修复**：改 `uint64_t end_offset64=(uint64_t)offset+size`，三处引用全部改用，
  按需加 uint32 截断。
- **回退**：revert 本函数相关行。

### BLD-14　CSPRNG 熵源 extern 宽度不匹配
- **位置**：`kernel/crypto/csprng.c:64–69`
- **证据**：旧 `extern uint32_t irq_count[16]` 与真实定义 `irq64_simple.c:32` 的
  `uint64_t irq_count[256]` 宽度/元素数双错，熵源按错宽度读。已确认 csprng.c 仅
  64 位构建引用，32 位不复用，无需条件编译。
- **修复**：extern 改 `uint64_t irq_count[256]`，total 改 uint64 累加 256 项。
- **回退**：revert 本函数。

### GFX-01　Tab 补全候选列表栈溢出
- **位置**：`kernel/arch/x86_64/hs_apps.c:461`（t_complete）；有界写 495、504、507
- **证据**：`out[80]` 无界 strcpy 拼接，≥5 个候选时内核栈越界写，用户可触发。
- **修复**：改逐字符有界（仅当 `olen < sizeof(out)-1` 才写），缓冲将满安全截断并
  强制 NUL。
- **回退**：revert 本函数。

### DRV-02　内核模块 PLT 静态索引跨模块累计
- **位置**：`kernel/arch/x86_64/kmod64.c:287`（got_idx 局部 218）
- **证据**：旧 `static uint64_t plt_slot_idx` 跨模块累计，第二个 .o 加载时 PLT 槽越界写。
- **修复**：删 static 改模块级局部（与同级 got_idx 口径一致）。
- **回退**：恢复 static 关键字。

### WM-02　鼠标/平板坐标按硬编码 1024 钳制
- **位置**：`kernel/arch/x86_64/usb64.c:1357–1362`；相对钳制 1399–1401、1426–1428；
  平板归一 1456–1457、1482–1483
- **证据**：旧代码用固定 1024 尺寸，非 1024x768 模式下鼠标无法到达屏幕右/下方，
  平板绝对归一也按 1024 缩放。
- **修复**：`usb64_poll` 开头取 `gfx64_get_width/height` 得真实 scr_w/scr_h、max_x/y；
  两处相对钳制、两处 tablet 绝对归一（`*scr_w/32768`）与钳制全部改用真实尺寸。
- **回退**：revert 本函数。

---

## 三、PID 语义关键说明（决定权限闸行为）

用户进程 PID 一律为 `idx+100`（或同函数内 `200+idx`），**全部 ≥100**，故 PID==0
闸会拒绝所有真实用户进程——这与既有 `sys_disk_read/write` 闸（同样 pid!=0 拒绝）
口径一致。用户态 install64（调 disk 26/27、mkfs 31、reboot 28）本就被既有 disk 闸
阻断，且 OPEN-04「安装器不拷贝系统文件」标「待解决」，属已知不完整遗留路径；
mbrtool 调 reboot 同理。内核内文本 shell 的 reboot（shell64.c:2513）仅 hlt、不走
syscall。**长期授权安装流需随未来 capability 模型解决，本轮不搭大体系。**

---

## 四、验证总览

### 1. 编译（无新增警告）
- 全量干净重建（`rm -rf build_x86_64` 后 make），编译通过。
- 警告数：基线 **210** → 修复版 **209**。归一化行号后逐类型对比：
  - **修复版零新增警告类型**；
  - 唯一消失的是基线 `syscall64_full.c: unused parameter 'oldact'`（新 sigaction
    实际消费了该参数）；其余差异均为同一预存警告因插行导致的行号漂移。

### 2. 主机测试（保持全过）
- `bash tools/host-tests/run_host_tests.sh`：**6/6 通过**
  （uaccess / aspace / heap / elf / winslot / ext2）。
- 未新增主机测试：可修复逻辑均深耦合内核大函数（extern/静态表），隔离测试需大改
  或复制逻辑，违背最小修改；ext2 主机测试已编译通过本轮改动的 ext2_algo.c。

### 3. QEMU 实测
- **LAPIC 1kHz**：以晶体精确的 PIT 为基准，多窗口 LAPIC/PIT 比值
  994.6 / 998.3 / 998.5 / 1014.0 Hz（目标 1000，偏差 ±1.4% 内）。
- **高 CPU 缓解**：中断率由旧硬编码下约 4kHz 降到 1kHz，中断负载降至 1/4。
- **信号/写路径不崩**：全程无真实 panic / page fault / 保护异常；OOBE 桌面作为
  用户进程持续运行，写/帧路径正常。
- **桌面与输入**：1024x768 桌面与首设向导正常渲染（版本显示 v0.54.11）；PS/2 与
  USB tablet 均能枚举，输入事件可投递并触发界面响应，无崩溃。
- 镜像经 `tools/make_single_img64.sh` 重建成功（256MB，EXIT=0）。

### 4. 环境性现象（非缺陷，已区分）
- 本机无 KVM，QEMU 走 TCG，客户机为忙等主循环时单核跑满（约 106%），客户机虚拟
  时间慢于墙钟，故按墙钟测两时钟均偏低；以客户机内部 PIT 为基准，LAPIC 精确为 1kHz。

---

## 五、需真机/后续确认点

1. **NET-01/02/05 真实流量**：本机 QEMU 二进制未编译 user/SLIRP 网络后端，无法在
   本机做完整联网实测；代码逻辑与主机编译已验证，需真机或带网络后端环境确认
   序列号推进、纯 ACK 拥塞更新、长连接缓冲回收。
2. **LAPIC 在真机/KVM 的总线频率**：校准为运行期实测，理论上自适应；真机不同 APIC
   总线频率下的 1kHz 落点需真机回归确认。
3. **USB HID 事件细节**：合成 monitor 事件与 guest USB 轮询交互存在已知怪癖（属
   待拍板的「USB 事件驱动重构」范围），精确指针/按钮行为建议真机确认。
4. **长期授权安装流**：PID==0 为临时闸，reboot/mkfs/disk 的用户态授权路径需随
   capability 模型落地（与 OPEN-04 一并解决）。
