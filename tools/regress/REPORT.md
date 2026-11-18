# HepuOS 回归测试报告（test/regress-2026-11-17）

- 分支：`test/regress-2026-11-17`
- 基线提交：`61c53fa` fix(desktop): bar_height 只留一处真源（36），去掉 hpdesk_init 里的第二个默认值 40
- 回归脚本/报告快照随分支 `test/regress-2026-11-17` 最新提交入库（不 push）；基线为 `61c53fa`
- 证据目录：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17`
- 运行环境：2 核 / 无 /dev/kvm / 全 TCG；QEMU 全机串行（/tmp/hepu-qemu.lock），编译串行（/tmp/hepu-build.lock）
- 三态口径：**PASS** 取证通过；**FAIL** 真实失败；**UNVERIFIED** 未能取证，不计通过

## 三态结果表

| 项目 | 架构 | 状态 | 证据路径 | 说明 |
|---|---|---|---|---|
| 64-菜单 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/menu_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/menu_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/menu_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/menu_4.png | UEFI 引导菜单 3 项可选，截图 4 张 |
| 64-加载屏（连续视频） | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/loading.mp4 | 加载过程连续录屏；TCG + 外部抢核下阶段推进很快，见抽帧明细。 |
| 64-加载屏静态帧 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/frames/p0051.png、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/frames/p0055.png、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/frames/p0059.png、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/frames/p0069.png | 2026-11-18 复核：120 ms 高频 screendump 轮询 81 帧，按 font8x16 离线渲染候选文案做点阵模板匹配（score≥0.50）并目视复核，Loading/Verifying/Preheating/handoff 各阶段独立静态帧均命中；汇总见 `loading/evidence_summary.md`。旧 loading_1..4 晚于 splash 的结论由本轮新帧取代。 |
| 64-加载阶段帧覆盖 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/frames/p0051.png（Loading 64-bit kernel image）、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/frames/p0055.png（Verifying kernel image）、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/frames/p0059.png（Preheating kernel pages）、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/frames/p0069.png（Preparing to hand off） | 2026-11-18 复核：三阶段独立帧全部命中——Loading `p0051-0054`（score 0.595）、Verifying `p0055-0058`（0.587）、Preheating `p0059-0068`（0.605）、handoff `p0069-0078`（0.584）；候选原文取自 `tools/hepboot/uefi/hepboot_uefi.c:1709/1740/1757/1771`，明细 `loading/evidence_summary.md`。 |
| 64-锁屏 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_4.png | 串口 `booting to lock screen` 在场；本树锁屏画面即米白底解锁页（头像 U、用户名 hepu、密码框、Unlock 按钮、VKB/PWR），未观察到独立于密码页的纯时钟锁屏；NONBLACK min=2073597 |
| 64-登录页 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/login_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/login_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/login_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/login_4.png | 按键后串口 `lock -> login (key)`，登录页为同一解锁/密码 UI；登录页帧 4 张，另空口令拒绝提示帧 1 张（红条 Wrong password） |
| 64-空 Enter 登录探测 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/login_reject_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/ser.log | 只按 Enter（空口令）不能进桌面：已配置账户分支（configured），空口令被拒并留在登录页；串口 `login: auth failed` 在场；账户加载标记=1。 |
| 64-密码登录→桌面 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_4.png | 输入夹具口令 `hepu123` 后提交进入桌面（串口 login -> desktop）；该口令为本回归 fixture 选择，仓库无内置默认口令；桌面 NONBLACK min=2073597 / 阈值 2073597 |
| 64-鼠标解锁路径 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/mouse/mouse_lock.png、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/mouse/mouse_login.png、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/mouse/mouse_desktop.png、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/mouse/ser.log、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/mouse/mouse_report.txt | QMP `input-send-event` 绝对鼠标（usb-tablet/UHCI）两段点击：锁屏 (960,540) → 登录页；登录帧实测 Unlock 按钮 bbox x885-1034/y588-635、中心 (959,611) → 桌面。串口 L608 `lock -> login (click)`、L621 `login -> desktop (auth ok)`、L622 `login -> desktop (button click)`；三帧像素统计与判定见 mouse_report.txt。口令 `hepu123` 走键盘夹具，鼠标只负责点击。 |
| 32-菜单 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/menu_4.png | 菜单数字键 `2` 即时选中 32 位项（串口 `[BOOT] Mode: 32-bit i386` 在场）；截图 4 张 |
| 32-加载屏 | 32 | **UNVERIFIED** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_4.png | 4 帧中仅 loading_1 为有效 Preheating 96% 快照，其余 3 帧为切换黑屏/过渡帧，未凑齐 4 张有效加载屏；串口 32-bit 引导标记在场。 |
| 32-锁屏 | 32 | **N/A（现状限制）** | /home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/ser32.txt、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/ser.log、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/frames32-uefi/ser.log | 设计上 32 位兜底关闭锁屏（非未取证）：`kernel/arch/x86_64/hpdesk.c:4634-4637`（`#else` 分支 `lock_cfg_disabled()` 返回 1，`:4636`）；调用点 `:7652-7657`，禁用时 `g_locked=0`，`booting to lock screen` 标记仅在 `:7656` else 分支打印。本轮三份 32 位串口均无 `booting to lock` / `booting to lock screen` 标记。 |
| 32-桌面（multiboot 直启补充取证） | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/desktop_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/desktop_2.png | 直启 kernel.elf 图形桌面到达（串口 elastic free desktop init done + first frame）；NONBLACK min=2073598 / 2,073,600（≈100.00%），32 位桌面不套 1080p 全量非黑阈值。旧基线 786,399（≈38%）为部分首帧稀疏画面，本基线上直启桌面已整帧上色，旧数值不成立。 |
| 32-UEFI 链路桌面帧 | 32 | **不可取证（依赖 fix/linuxhello32）** | /home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/frames32-uefi/measure.csv、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/frames32-uefi/ser.log、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/frames32-uefi/state.txt、帧 `f003.png`–`f006.png`（崩溃前过渡帧）、`f008.png`（重启早期 GOP 文本帧）、`f009.png`–`f011.png`（回灌菜单残留） | 本轮 UEFI 链路 12 帧采帧（0→17.05s；TCG 全帧 dump 阻塞，实测间隔约 1.6–2.1s）：崩溃前窗口仅见菜单、OBS-06 稀疏过渡帧（strict≈786,385–786,387）与固件重启早期 GOP 文本帧；回灌菜单帧中心色 `(232,234,236)`，与旧档菜单残留同签名。串口 `desk_start`（ser.log:525）、`first frame via registry`（:550）在场，但崩溃前未抓到可度量的真实桌面帧；不以菜单残留凑数。该链路桌面取证依赖 `fix/linuxhello32` 修复（已分派，本仓不修）。直启桌面见上一行。 |
| 32-linuxhello 用户态崩溃 | 32 | **FAIL** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/ser.log | 直启与 UEFI 两条路径均在 [AUTO] linuxhello start 后退出/重启：直启 -no-reboot 下 QEMU 退出，UEFI 路径重启回灌菜单。对照上一基线 a787a01：同位置 linuxhello 故障被回收（rc=3 继续跑后续应用），本轮 escalate 为重启；61c53fa 链路上 53e53cb 仅修 32 位文案空串兜底、61c53fa 仅统一顶栏 bar_height，未改动 32 位内核执行路径；根因未隔离编译验证，暂记为既有不稳定项存疑，不直接判为本次合并回归。 |
| 32-登录/认证路径 | 32 | **不可取证：无调用方** | /home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/ser32.txt、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/ser.log、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/frames32-uefi/ser.log | 全仓 grep 核实：`login_show` 定义 `kernel/arch/x86/login.c:47`、声明 `kernel/include/login.h:34`，**无外部调用方**；`login_authenticate`（`login.c:85`）仅被内部状态机 `login.c:305` 调用，`login_unlock_screen`（`login.c:164`）仅被 `login.c:319` 调用，`mu_private_unlock`（`kernel/arch/x86/multi_user.c:232`）零调用方。内核 shell 分发 `kernel/sched/shell_execute.c:102-160`、用户态 shell `user/shell.c:219-245` 均无 login/unlock 命令，autostart 无登录应用。三处桩路径不可达，本轮三份 32 位串口无 LOGIN WARNING。认证行为不可取证，不是已通过；改动意图是把假校验改成显式未实现并保留无密码放行，现有证据既未触发路径，也不能据此判定为回归。 |
| 保护模式 64 | 64 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/protect64/protect64_text.png | 文本终端进入，提示符 hepu@os:~ 在场，help/ls 回显可见；注：64 位 shell 无 ktest 命令 |
| 保护模式 32 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/protect32/protect32_text.png | 文本终端进入，提示符 + help 命令列表回显在场；取证以串口为准（VGA 文本缓冲未刷入图形帧缓冲，截图为深色残留） |
| 64 位 KUnit | 64 | **不可取证** | /home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/kunit/kunit_audit.md | 不可取证原因：64 位内核没有 KUnit 运行入口——`kernel/arch/x86_64/Makefile` 源清单不含 `kernel/kunit/` 与 `kernel/sched/`，`shell64.c` 命令表无 `ktest`；`ktest` 仅是 32 位命令（`shell_cmd_system.c:75` → `shell_execute.c:115`，框架初始化 `arch/x86/kernel.c:638-651`）。`Command not found: ktest` 是路径必然结果，不是"跑过但没验到"。 |
| 安全启动验证桩 | host | **不可取证** | /home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/secboot/secboot_audit.md、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/secboot/harness.log | 不可取证原因：全仓 grep 确认 `sb_verify_stage/signature/hash` 三桩在 kernel/user/tools 零调用方，唯一外部引用是 `secure_boot_init()`（`init_sequence.c:370`），x86_64 不引用 secure_boot；启动链路不会进入 verify 路径。另做主机级单元调用 harness（非启动链路实测）：signature/hash 返回 -2、严格模式 stage 返回 -2，harness PASS。桩在 `kernel/arch/x86/secure_boot.c`、头 `kernel/include/secure_boot.h`。 |
| 主机端 ASan/UBSan | host | **已修复（PASS 8/0）** | /home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/host-tests/host-tests.log | 2026-11-18 复核：改动前 7 过 1 败，elf 报 `undefined reference to compat_gate_detect_elf`；定位为测试链接清单只编 elf64.c、漏编 compat_gate.c。最小改动两处：`tools/host-tests/run_host_tests.sh` elf 行补 `$ARCH/compat_gate.c`；`tools/host-tests/elf_test.c` 补进程表访问器桩 `user_process_current_env_type()` 返回 0（ENV_NATIVE）。未把 user_process.c 编入（会级联 pmm/vmm/ext2 等一大批未定义符号）。全套实跑 === 结果: 通过 8 / 失败 0 ===，elf `ALL ELF TESTS PASSED`。 |
| 图形栈边界自检 | host | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/boundary.log | check_gfx_boundary.sh 退出码 0 |
| 32 位冒烟六项 | 32 | **PASS** | /home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/smoke32.txt | serial=1 initrd=1 net=1 desk=1 fault=0 |
| 32 位 KUnit 套件 | 32 | **FAIL** | /home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/kunit32-ledger.txt、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/ser32.txt | 本轮实跑套件自判=FAIL；失败套件=4 / 用例=6。与 `old-a787a01/tests/baseline/a787a01-kunit32.txt`、`tests/baseline/61c53fa-kunit32.txt` 两份基线逐条一致（same_as_baseline），属**既有基线项，非新增回归**。根因定位：5 例时间/中断类——KUnit 在 `kernel/arch/x86/kernel.c:638-651` 早于 `sti`（`kernel/sched/shell.c:180`）运行，PIT tick 未累加（`kernel/arch/x86/pit.c:48-50` 仅 IRQ0 自增）、IF=0，`sys_time`/`pit_get_ticks` 返回 0；1 例超大分配——`kmalloc(0xFFFFFFFF)` 未拒收，`kernel/mm/kheap.c:260` 仅判 size==0，`:261` ALIGN 回绕，`kf_pool_alloc`（`kheap.c:48-55`）`need=HEADER_SIZE+size` 32 位回绕后落池。失败用例明细见 kunit32-ledger.txt。 |
| 32-ICMP echo / TCP 对外路径 | 32 | **FAIL** | /home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/ser.log、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/host-listener.log、/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/evidence-summary.md | 保护模式 + `qemu-system-i386 -nic user,model=e1000` 实跑：交互 DHCP 完成后 `ping 10.0.2.2` 4 次 ARP miss、无 Echo Reply；`tcp connect 10 0 2 2 18080` SYN_SENT 后 ARP miss，宿主 listener 0 连接。阻塞于 ARP 解析：`kernel/net/ip.c:128-133` ARP miss 即 `return -1`，ICMP/TCP 帧未出 guest；ARP 请求 `sender_ip=0`（`kernel/net/arp.c:52`）。未用 DHCP/冒烟 `net=1` 冒充 ICMP/TCP 已通。 |

## 像素计数明细

判定口径：桌面 1920×1080 = 2,073,600 像素，64 位桌面 NONBLACK ≥ 2,073,597 判真桌面；锁屏米白底，非黑占比 ≥ 50% 记在场；32 位旧桌面不套 1080p 阈值，仅记录计数。

- **64 位**：桌面帧 4，NONBLACK 最低=2073597（PASS，阈值 2073597）；锁屏帧 4，NONBLACK 最低=2073597（PASS）；登录页/拒绝提示帧 5，NONBLACK 最低=2073597
- **32 位**：UEFI 链路桌面帧不可取证（依赖 `fix/linuxhello32`；旧 4 帧 NONBLACK=2073600 为崩溃重启残留菜单，不计入桌面计数）；锁屏帧 0，NONBLACK 最低=None（N/A 现状限制：设计上 32 位关闭锁屏）；登录页/拒绝提示帧 0，NONBLACK 最低=None（不可取证：登录页无调用方）。2026-11-18 复核明细见文末「遗留项复核」节。
- **32 位直启补充**：桌面帧 2，NONBLACK 最低=2073598（PASS），占比 100.00%；UEFI 链路残留菜单帧不计入桌面计数。

## OBS-05：32 位桌面（multiboot 直启） 测量口径与九宫格

### 1) 测量截图（绝对路径，共 2 帧）
- `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/desktop_1.png`
- `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/desktop_2.png`
- 九宫格叠加对照图：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/obs05/32-desktop_desktop_1_grid.png`

### 2) 采集参数
- 分辨率：截图实测 1920×1080（目标桌面 1920×1080）；帧由 QEMU monitor `screendump` 出 PPM，再用 PIL `convert("RGB")` 转 PNG，未缩放、未裁剪。
- 实例：`qemu-system-i386 -kernel build_v250/kernel.elf`，multiboot 直启；`-m 256 -smp 1`；**不挂磁盘**（无 ext2 镜像）；`-no-reboot`；VNC 127.0.0.1:5902。
- 等待：串口 `elastic free desktop init done` 命中后取帧（本轮 2 帧，帧间隔 1.5s）；直启无菜单阶段，最早桌面帧在首帧注册后约 1s 内抓到。
- 抓图方式：Unix socket monitor 下发 `screendump <ppm>`（非 -vnc 截屏、非外部录屏抽帧）；QEMU 退出则跳过该帧并在状态文件标记 CRASHED。

### 3) 分子/分母定义
- 分子（主口径 strict）：`pixel != (0,0,0)`，即 RGB 三通道不全为黑才计 1；**未排除背景色、未按主题色剔点**。
- 近黑参考口径（near，仅附列不参与判定）：任一通道 ≥16 判非黑。
- 分母：整帧 W×H = 2073600；九宫格每格分母 = (W/3)×(H/3)，格内占比 = 格内非黑 / 格内像素。
- 整帧 NONBLACK：strict 最低 2073598 / 2073600（100.00%）；near 最低 2073584。

### 4) 九宫格分区占比（代表帧：desktop_1.png，strict 口径）

| 分区 | 非黑像素 | 占比 | 分区 | 非黑像素 | 占比 | 分区 | 非黑像素 | 占比 |
|---|---|---|---|---|---|---|---|---|
| 上 | 上左 230400 / 100.00% | 上中 230400 / 100.00% | 上右 230398 / 100.00% |
| 中 | 中左 230400 / 100.00% | 正中 230400 / 100.00% | 中右 230400 / 100.00% |
| 下 | 下左 230400 / 100.00% | 下中 230400 / 100.00% | 下右 230400 / 100.00% |
- 九宫格全部 ≥50%，无明显黑块分区。

### 5) 与旧基线对比
旧基线 3d0bd23 台账值 786,399 / 2,073,600 ≈ 38%（部分首帧稀疏画面）。本轮 61c53fa 实测见上：整帧 strict NONBLACK 最低 2073598，占比 100.00%；若与 38% 不一致，差异来自抓图时机（旧值取到首帧尚未整帧上色的稀疏画面，本轮取桌面稳定后 2 帧），而非同一时点的回退。旧值仅存于 old-3d0bd23/ 对照，不混入本轮结论。

## OBS-05：64 位锁屏 测量口径与九宫格

### 1) 测量截图（绝对路径，共 4 帧）
- `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_1.png`
- `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_2.png`
- `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_3.png`
- `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/lock_4.png`
- 九宫格叠加对照图：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/obs05/64-lock_lock_1_grid.png`

### 2) 采集参数
- 分辨率：截图实测 1920×1080（目标桌面 1920×1080）；帧由 QEMU monitor `screendump` 出 PPM，再用 PIL `convert("RGB")` 转 PNG，未缩放、未裁剪。
- 实例：`qemu-system-x86_64`，`-m 1024 -smp 2`；挂回归镜像 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/build/hepu.img`（256MB raw，`snapshot=on` 只读共享）；OVMF CODE/VARS 私有副本；`-vnc 127.0.0.1:5902`，USB 键盘/鼠标。
- 等待：菜单 7s 后取帧；状态标记串口等待上限 60–70s；每状态 4 帧，帧间隔 1.5–2s。
- 抓图方式：Unix socket monitor 下发 `screendump <ppm>`（非 -vnc 截屏、非外部录屏抽帧）；QEMU 退出则跳过该帧并在状态文件标记 CRASHED。

### 3) 分子/分母定义
- 分子（主口径 strict）：`pixel != (0,0,0)`，即 RGB 三通道不全为黑才计 1；**未排除背景色、未按主题色剔点**。
- 近黑参考口径（near，仅附列不参与判定）：任一通道 ≥16 判非黑。
- 分母：整帧 W×H = 2073600；九宫格每格分母 = (W/3)×(H/3)，格内占比 = 格内非黑 / 格内像素。
- 整帧 NONBLACK：strict 最低 2073597 / 2073600（100.00%）；near 最低 2073595。

### 4) 九宫格分区占比（代表帧：lock_1.png，strict 口径）

| 分区 | 非黑像素 | 占比 | 分区 | 非黑像素 | 占比 | 分区 | 非黑像素 | 占比 |
|---|---|---|---|---|---|---|---|---|
| 上 | 上左 230400 / 100.00% | 上中 230400 / 100.00% | 上右 230400 / 100.00% |
| 中 | 中左 230400 / 100.00% | 正中 230397 / 100.00% | 中右 230400 / 100.00% |
| 下 | 下左 230400 / 100.00% | 下中 230400 / 100.00% | 下右 230400 / 100.00% |
- 九宫格全部 ≥50%，无明显黑块分区。

## OBS-05：64 位桌面 测量口径与九宫格

### 1) 测量截图（绝对路径，共 4 帧）
- `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_1.png`
- `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_2.png`
- `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_3.png`
- `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames64/desktop_4.png`
- 九宫格叠加对照图：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/obs05/64-desktop_desktop_1_grid.png`

### 2) 采集参数
- 分辨率：截图实测 1920×1080（目标桌面 1920×1080）；帧由 QEMU monitor `screendump` 出 PPM，再用 PIL `convert("RGB")` 转 PNG，未缩放、未裁剪。
- 实例：`qemu-system-x86_64`，`-m 1024 -smp 2`；挂回归镜像 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/build/hepu.img`（256MB raw，`snapshot=on` 只读共享）；OVMF CODE/VARS 私有副本；`-vnc 127.0.0.1:5902`，USB 键盘/鼠标。
- 等待：菜单 7s 后取帧；状态标记串口等待上限 60–70s；每状态 4 帧，帧间隔 1.5–2s。
- 抓图方式：Unix socket monitor 下发 `screendump <ppm>`（非 -vnc 截屏、非外部录屏抽帧）；QEMU 退出则跳过该帧并在状态文件标记 CRASHED。

### 3) 分子/分母定义
- 分子（主口径 strict）：`pixel != (0,0,0)`，即 RGB 三通道不全为黑才计 1；**未排除背景色、未按主题色剔点**。
- 近黑参考口径（near，仅附列不参与判定）：任一通道 ≥16 判非黑。
- 分母：整帧 W×H = 2073600；九宫格每格分母 = (W/3)×(H/3)，格内占比 = 格内非黑 / 格内像素。
- 整帧 NONBLACK：strict 最低 2073597 / 2073600（100.00%）；near 最低 2073547。

### 4) 九宫格分区占比（代表帧：desktop_1.png，strict 口径）

| 分区 | 非黑像素 | 占比 | 分区 | 非黑像素 | 占比 | 分区 | 非黑像素 | 占比 |
|---|---|---|---|---|---|---|---|---|
| 上 | 上左 230400 / 100.00% | 上中 230400 / 100.00% | 上右 230400 / 100.00% |
| 中 | 中左 230400 / 100.00% | 正中 230397 / 100.00% | 中右 230400 / 100.00% |
| 下 | 下左 230400 / 100.00% | 下中 230400 / 100.00% | 下右 230400 / 100.00% |
- 九宫格全部 ≥50%，无明显黑块分区。

## 加载屏推进证据

- 视频：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/loading.mp4`
- 抽帧：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_1_menu.png`
- 抽帧：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/loading/stage_3_handoff.png`
- 旧档（2026-11-17）：Loading/Verifying/Preheating 三阶段未采到独立静态帧；该结论已被 2026-11-18 高频轮询复核取代（见上表 64-加载屏静态帧/阶段帧覆盖两行与文末「遗留项复核」段），旧录像与菜单/handoff 抽帧保留备查。

## 失败 / 未验证项与原因

- **64-加载屏静态帧（64 / PASS，2026-11-18 复核）**：120 ms 高频 screendump 轮询 81 帧，font8x16 点阵模板匹配 + 目视复核，Loading/Verifying/Preheating/handoff 静态帧命中；证据 `/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/frames/p0051.png、p0055.png、p0059.png、p0069.png`，汇总 `loading/evidence_summary.md`。旧 loading_1..4 晚于 splash 的结论作废。
- **64-加载阶段帧覆盖（64 / PASS，2026-11-18 复核）**：Loading `p0051-0054`、Verifying `p0055-0058`、Preheating `p0059-0068`、handoff `p0069-0078` 四阶段独立帧齐备；候选原文 `tools/hepboot/uefi/hepboot_uefi.c:1709/1740/1757/1771`；证据 `/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/frames/`。
- **64-鼠标解锁路径（64 / PASS，2026-11-18 复核）**：QMP 绝对鼠标两段点击（锁屏 → 登录页 → Unlock 按钮 → 桌面），按钮坐标从登录帧色带实测 (959,611)；串口 L608/L621/L622 三条标记在场，三帧像素统计见报告；证据 `/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/mouse/`。
- **32-加载屏（32 / UNVERIFIED）**：4 帧中仅 loading_1 为有效 Preheating 96% 快照，其余 3 帧为切换黑屏/过渡帧，未凑齐 4 张有效加载屏；串口 32-bit 引导标记在场；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_1.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_2.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_3.png、/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32/loading_4.png`
- **32-锁屏（32 / N/A 现状限制）**：设计上 32 位兜底关闭锁屏，非未取证。`kernel/arch/x86_64/hpdesk.c:4634-4637`（`#else` 分支 `lock_cfg_disabled()` 返回 1，`:4636`）；调用点 `:7652-7657`，禁用时 `g_locked=0`，`booting to lock screen` 标记仅在 `:7656` else 分支打印。本轮三份 32 位串口（直启 KUnit、保护模式网络、UEFI 链路）均无 `booting to lock` 标记；证据 `/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/ser32.txt`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/ser.log`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/frames32-uefi/ser.log`
- **32-UEFI 链路桌面帧（32 / 不可取证，依赖 fix/linuxhello32）**：本轮 UEFI 链路 12 帧采帧（0→17.05s，TCG 全帧 dump 阻塞致实测间隔约 1.6–2.1s），崩溃前窗口仅见菜单、OBS-06 稀疏过渡帧（strict≈786,385–786,387）与固件重启早期 GOP 文本帧；回灌菜单帧 `f009–f011` 中心色 `(232,234,236)`，与旧档菜单残留同签名。串口 `desk_start`/`first frame via registry` 在场，但崩溃前未抓到可度量的真实桌面帧，不以菜单残留凑数；该链路桌面取证依赖 `fix/linuxhello32` 修复（已分派）。直启桌面见上一行；证据 `/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/frames32-uefi/measure.csv`、`ser.log`、`state.txt` 及帧目录
- **32-linuxhello 用户态崩溃（32 / FAIL）**：直启与 UEFI 两条路径均在 [AUTO] linuxhello start 后退出/重启：直启 -no-reboot 下 QEMU 退出，UEFI 路径重启回灌菜单。对照上一基线 a787a01：同位置 linuxhello 故障被回收（rc=3 继续跑后续应用），本轮 escalate 为重启；61c53fa 链路上 53e53cb 仅修 32 位文案空串兜底、61c53fa 仅统一顶栏 bar_height，未改动 32 位内核执行路径；根因未隔离编译验证，暂记为既有不稳定项存疑，不直接判为本次合并回归；证据 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/frames32-direct/ser.log`
- **32-登录/认证路径（32 / 不可取证：无调用方）**：全仓 grep 核实 `login_show`（`kernel/arch/x86/login.c:47`，声明 `kernel/include/login.h:34`）无外部调用方；`login_authenticate`（`login.c:85`）仅内部 `login.c:305` 调用，`login_unlock_screen`（`login.c:164`）仅内部 `login.c:319` 调用，`mu_private_unlock`（`kernel/arch/x86/multi_user.c:232`）零调用方；内核 shell 分发 `kernel/sched/shell_execute.c:102-160`、用户态 shell `user/shell.c:219-245` 无 login/unlock 命令，autostart 无登录应用。三处桩路径不可达，本轮三份 32 位串口无 LOGIN WARNING。认证行为不可取证，不是已通过；改动意图是把假校验改成显式未实现并保留无密码放行，现有证据既未触发路径，也不能据此判定为回归；证据 `/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/ser32.txt`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/ser.log`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/frames32-uefi/ser.log`
- **64 位 KUnit（64 / 不可取证，2026-11-18 复核）**：缺的是运行入口，不是"没跑到"——64 位 Makefile 源清单不含 `kernel/kunit/`、`kernel/sched/`，`shell64.c` 命令表无 `ktest`；`ktest` 仅 32 位命令（`shell_cmd_system.c:75`、`shell_execute.c:115`、`arch/x86/kernel.c:638-651`）。证据 `/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/kunit/kunit_audit.md`。
- **安全启动验证桩（host / 不可取证，2026-11-18 复核）**：三 verify 桩全仓零调用方，启动链路不进入该路径；唯一外部引用 `secure_boot_init()`（`init_sequence.c:370`）。主机级单元调用佐证桩返回 -2（非启动链路实测）。证据 `/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/secboot/secboot_audit.md、harness.log`。
- **主机端 ASan/UBSan（host / 已修复，8/0，2026-11-18 复核）**：改动前 7 过 1 败（elf `undefined reference to compat_gate_detect_elf`）；补链接清单 `compat_gate.c` + 测试桩进程表访问器后全套通过 8 / 失败 0，elf `ALL ELF TESTS PASSED`。证据 `/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/host-tests/host-tests.log`。
- **32 位 KUnit 套件（32 / FAIL）**：本轮实跑套件自判=FAIL；失败套件=4 / 用例=6。与 `old-a787a01/tests/baseline/a787a01-kunit32.txt`、`tests/baseline/61c53fa-kunit32.txt` 两份基线逐条一致（same_as_baseline），属**既有基线项，非新增回归**。根因：5 例时间/中断类（KUnit 早于 `sti` 运行，PIT tick 未累加、IF=0，`sys_time`/`pit_get_ticks` 返回 0）；1 例超大分配（`kmalloc(0xFFFFFFFF)` 未拒收，大小校验缺失 + 32 位回绕落池）。失败用例明细见 kunit32-ledger.txt；证据 `/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/kunit32-ledger.txt`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/ser32.txt`
- **32-ICMP echo / TCP 对外路径（32 / FAIL）**：保护模式 + `qemu-system-i386 -nic user,model=e1000` 实跑，宿主 listener 127.0.0.1:18080。交互 DHCP 完成后 `ping 10.0.2.2` 4 次 `[IP] ARP miss, sending request`，无 Echo Reply；`tcp connect 10 0 2 2 18080` 仅 `[TCP] connect SYN_SENT` + 1 次 ARP miss，无 established，宿主 listener 0 连接。阻塞于 ARP 解析（`kernel/net/ip.c:128-133` miss 即 `return -1`，帧未出 guest；`kernel/net/arp.c:52` 请求 `sender_ip=0`）；ARP 应答未进缓存的根因未完全定位（缺 guest 侧 ARP 抓包 / slirp 报文计数；DHCP 分配 IP 仅打 VGA 未镜像串口）。未用 DHCP/冒烟 `net=1` 冒充已通；证据 `/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/ser.log`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/host-listener.log`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/evidence-summary.md`

## 新提交意图对照（基线 61c53fa）

- **9e8f2a3 登录页接入账户密码三分支认证**：未配置账户放行 / 已配置 sha256 比对 / 失败留登录页；本轮按该三分支实测空 Enter 与夹具密码两条路径，结果见上表。
- **dec60c9 锁屏/登录文案接入多语言表**：登录页为中文化后渲染；自动化按键不受文案影响。
- **10e42fa 向导与锁屏状态优先级修复**：锁屏/登录态优先于 OOBE 模态，按键不被向导吞掉。
- **261a027 / a787a01 合并**：并入首设向导优先级修复；64 桌面仍正常到达。
- **53e53cb i18n**：日期跟随语言；修 32 位“文案全空”空串兜底（本轮 32 直启桌面日期/设置文案在场）。
- **61c53fa 顶栏高度双默认统一**：bar_height 只留 36 一处真源，去掉 hpdesk_init 里第二个默认值 40；本轮 64/32 桌面顶栏布局未塌。


## 基线对比

- 本次基线提交：`61c53fa`；紧邻上一基线 a787a01 证据归档至 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/old-a787a01/`，更早 3d0bd23 归档至 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/old-3d0bd23/`，均未与本次结果混比。
- 32 位 KUnit 基线快照：`/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/tests/baseline/61c53fa-kunit32.txt`（4 套件失败 / 6 用例失败；比对结论：same_as_baseline）。
- 主机端 ASan/UBSan：2026-11-18 复核已修复，8 过 0 败；改动前 7 过 1 败（elf 缺 compat_gate.c），详见上表与「遗留项复核」段。
- 图形栈边界：本次 boundary.log 退出码 0（PASS）。
- 32 位冒烟六项：serial/initrd/net/desk=1/1/1/1，fault=0。

## 一条命令重跑

```bash
cd <wt-regress worktree>
bash tools/regress/run_all.sh
```
跳过构建：`SKIP_BUILD=1 bash tools/regress/run_all.sh`；自定义证据目录：`HEPU_EV=<dir> bash tools/regress/run_all.sh`；单步：`bash tools/regress/<boot64|boot32|desktop32_direct|protect64|protect32|smoke32|record_loading>.sh`。

## 遗留项复核（2026-11-18，32 位）

> 工作树 `wt-ledger32-2026-11-18`（分支 `test/ledger-32-2026-11-18`，基于 main HEAD `391e1db`）；
> 构建 `bash tools/regress/build_all.sh`，32/64 位均 0 error（`evidence-ledger-2026-11-18/32/build/build32.log`、`build64.log`）。
> 证据根：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/`。
> 三态口径：PASS=本机实测通过；FAIL=有实测失败证据；不可取证=写明具体原因与代码位置；N/A=设计/现状不适用。未改产品源码；未 push。

### 1. 32-锁屏 —— 判定 N/A（现状限制，非未取证）

- **原文引用**：三态表原记 `32-锁屏 / UNVERIFIED`；`docs/status/OPEN_BUGS.md:716-722` 记「32 位本树兜底关闭锁屏，串口无 `booting to lock` 标记」。
- **动作与命令**：源码核实 `lock_cfg_disabled()` 的 32 位分支；对本轮三份 32 位串口（直启 KUnit、保护模式网络、UEFI 链路）grep `booting to lock` / `booting to lock screen`。
- **观察**：`kernel/arch/x86_64/hpdesk.c:4634-4637` 为 `#else`（32 位）分支，`lock_cfg_disabled()` 直接 `return 1`（`:4636`）；调用点 `:7652-7657`，禁用时 `g_locked=0`，`[Hepu Desktop] booting to lock screen` 仅在 `:7656` else 分支打印。三份串口均无该标记。
- **判定**：32 位在设计上就关闭锁屏，属产品现状限制；台账三态由 UNVERIFIED 改为 **N/A（现状限制）**，不是证据缺失。
- **证据**：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/ser32.txt`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/ser.log`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/frames32-uefi/ser.log`。
- **代码位置**：`kernel/arch/x86_64/hpdesk.c:4619-4637`（32 位兜底 `:4634-4637`）、`:7652-7657`（置锁/打印分支）。

### 2. 32-登录/认证路径 —— 判定 不可取证：无调用方

- **原文引用**：三态表原记 `32-登录/认证路径 / UNVERIFIED`；`OPEN_BUGS.md:719-720` 记三处桩路径未触发、串口无 LOGIN WARNING。
- **动作与命令**：全仓 grep `login_show`、`login_init/login_hide/login_is_active/login_select_user`、`login_authenticate`、`login_unlock_screen`、`mu_private_unlock`；核对内核 shell 命令分发与用户态 shell 命令表；核对 autostart 应用清单。
- **观察**：`login_show` 仅定义（`kernel/arch/x86/login.c:47`）与声明（`kernel/include/login.h:34`），**无外部调用方**；`login_authenticate`（`login.c:85`）仅被内部状态机 `login.c:305` 调用，`login_unlock_screen`（`login.c:164`）仅被 `login.c:319` 调用，`mu_private_unlock`（`kernel/arch/x86/multi_user.c:232`）零调用方。内核 shell 分发 `kernel/sched/shell_execute.c:102-160`、用户态 shell `user/shell.c:219-245` 均无 login/unlock 命令，autostart 无登录应用。三处桩路径不可达；三份本轮 32 位串口无 `[LOGIN] WARNING` / `[MULTI_USER] WARNING`。
- **判定**：**不可取证：无调用方（登录页入口未接线）**。存在低成本入口假设（shell 命令拉起登录页）经核对不成立：两条 shell 命令表均无对应命令，故未做实跑注入。
- **证据**：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/ser32.txt`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/ser.log`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/frames32-uefi/ser.log`。
- **代码位置**：`kernel/arch/x86/login.c:47`（`login_show`）、`:85`（`login_authenticate`）、`:164`（`login_unlock_screen`）、`:305`/`:319`（内部调用点）；`kernel/include/login.h:34`；`kernel/arch/x86/multi_user.c:232`（`mu_private_unlock`）；`kernel/sched/shell_execute.c:102-160`；`user/shell.c:219-245`。

### 3. 32-UEFI 链路桌面帧 —— 判定 不可取证（依赖 fix/linuxhello32）

- **原文引用**：三态表原记 `32-UEFI 链路桌面帧 / UNVERIFIED`；`OPEN_BUGS.md:690-691` 记旧 4 帧为 linuxhello 崩溃后回灌菜单残留。
- **时序核实**：串口顺序为 `elastic free desktop init done` → `desk_start on 32-bit` → `wallpaper/panels/windows done` → `first frame via registry` → `[AUTO] linuxhello start` → 二次 `[BOOT] Mode: 32-bit i386`（崩溃回灌）。桌面注册先于 linuxhello，但可见表面是否被桌面帧覆盖需采帧判定。
- **动作与命令**：`run_uefi_frames_v2.sh`（OVMF + 256MB 双内核镜像，菜单 7s 后 `sendkey 2`，monitor `screendump` 采帧至二次引导/QEMU 退出，上限 40s）；`analyze_uefi_frames.py` 离线 PPM→PNG、strict/near NONBLACK 度量并对照旧档菜单/直启桌面签名。
- **观察**：共 12 帧（0→17.05s；TCG 全帧 dump 阻塞，实测间隔约 1.6–2.1s，未达 100–300ms 目标间隔，已在判定中注明）。崩溃前窗口 `f003–f006` 为 OBS-06 稀疏过渡帧（strict≈786,385–786,387，上半深蓝/下半纯黑）；`f007` 为 1024×768 黑屏，`f008` 为重启早期 GOP 文本帧（`[GDP] 0x780x438 bpp=20 pitch=0000 fb=0`）；`f009–f011` 为回灌引导菜单（中心色 `(232,234,236)`、`previous boot failures: 2`、`Auto-boot in 4s`），与旧档菜单残留同签名。**崩溃前未出现可度量的真实桌面帧**；无任一帧匹配直启桌面签名（teal 系主色）。
- **判定**：**不可取证：崩溃前未抓到真实桌面帧，该链路桌面取证依赖 `fix/linuxhello32` 修复**（已分派，本仓不修、不重复诊断）；未拿菜单残留帧凑数。
- **证据**：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/frames32-uefi/measure.csv`、`frames.csv`、`ser.log`、`state.txt`、帧 `f000.png`–`f011.png`；脚本 `run_uefi_frames_v2.sh`、度量 `analyze_uefi_frames.py`。
- **代码位置**：桌面启动路径 `kernel/arch/x86_64/hpdesk.c`（`desk_start` 调用链）；崩溃项 OBS-07 `linuxhello` 用户态（修复分支 `fix/linuxhello32`，不在本仓）。

### 4. 32 位 KUnit 4 套件 / 6 用例失败 —— 判定 FAIL（既有基线项，非新增回归）

- **原文引用**：三态表原记 `32 位 KUnit 套件 / FAIL`；`OPEN_BUGS.md:759-766` 记 4 套件 6 用例失败、与 a787a01 快照一致。
- **动作与命令**：`run_kunit32.sh`（`qemu-system-i386 -kernel build_v250/kernel.elf -m 256 -smp 1 -no-reboot`，等 `[KTEST] RESULT:` 后收串口，`strings` 转文本）；抽取 `KUNIT-FAIL` / `KUNIT-FAIL-SUITE` 标签，与 `old-a787a01/tests/baseline/a787a01-kunit32.txt`、`tests/baseline/61c53fa-kunit32.txt` 逐条比对；回读断言源码定位原因。
- **观察**：`[KTEST] RESULT: FAIL`，失败套件 4 / 用例 6：`syscall deep / syscall table complete`、`user scenario / boot complete`、`user scenario / interrupts active`、`user scenario / datetime`、`integration / flow syscall seq`、`security E2E / boundary max size`。两份基线比对均为 **same_as_baseline**（new_fails 空、fixed_fails 空）。
- **根因定位**：
  - 5 例时间/中断类：KUnit 在 `kernel/arch/x86/kernel.c:638-651` 早于 `sti`（`kernel/sched/shell.c:180`）运行；PIT tick 仅在 IRQ0 自增（`kernel/arch/x86/pit.c:48-50`），`sys_time` 取 `pit_get_seconds`（`kernel/arch/x86/sys_time.c:39-47`），此时 tick=0、IF=0，故 `t>0` / `ticks>0` / `eflags&0x200` 断言失败。对应断言：`kunit_syscall_deep.c:68`、`kunit_user_scenario.c:17`、`:27`、`:140`、`kunit_integration.c:149`。
  - 1 例超大分配：`kunit_security_e2e.c:29-30` `kmalloc(0xFFFFFFFF)` 期望 NULL；`kernel/mm/kheap.c:260` 仅拒收 `size==0`，`:261` `ALIGN(size,4)` 回绕，`kf_pool_alloc`（`kheap.c:48-55`）`need=HEADER_SIZE+size` 32 位回绕后按小块落池，返回非 NULL。
- **判定**：**FAIL**；与两份基线逐条一致，属既有基线项，非本轮新增回归。
- **证据**：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/kunit32-ledger.txt`（失败标签 + 断言 + 基线比对）、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/tests/ser32.txt`、`ser32.log`；脚本 `run_kunit32.sh`。

### 5. 32 位 ICMP echo reply 与 TCP 路径 —— 判定 FAIL（阻塞于 ARP，部分根因未定位）

- **原文引用**：`OPEN_BUGS.md:578-582`、`:635` 两处标「ICMP echo reply 与 TCP 路径本轮未复测」；不得以冒烟 `net=1` / DHCP 冒充已通。
- **入口核实**：命令行入口存在——`ping`（`kernel/sched/shell.c:375-410` → `ping_once`）、`tcp connect/listen/accept/send/recv/close`（`kernel/sched/shell_cmd_adv.c:283` 起，分发 `kernel/sched/shell_execute.c:152-160,465-466`）；启动自检 `net_selftest`（`kernel/net/net_selftest.c:46`）。注：自检用 `icmp_send_echo_request`（`kernel/net/icmp.c:51-80`）只组包/算校验和，未调 `ip_send`，帧不上线；真正发送路径只有 `ping_once`（`icmp.c:136`）。
- **动作与命令**：`run_net32.sh`；`qemu-system-i386 -kernel build_v250/kernel.elf -append "hepu.graphics=0" -nic user,model=e1000 -no-reboot`；宿主在 `127.0.0.1:18080` 起 listener（slirp 下 guest 访问 10.0.2.2 即宿主）；shell 内依次 `dhcp` → `ifconfig` → `ping 10.0.2.2` → `tcp connect 10 0 2 2 18080`，串口与宿主双侧留证。
- **观察**：e1000 link up、协议栈初始化在场；交互重试 DHCP 完成（`DHCP configuration complete`、`[IP] config updated`）。`ping 10.0.2.2`：4 次 `[IP] ARP miss, sending request`，**无** Echo Reply、无 `64 bytes from`；`tcp connect 10 0 2 2 18080`：`[TCP] connect SYN_SENT` 后 1 次 ARP miss，**无** `connection established`；宿主 `host-listener.log` 0 字节，**未接受任何连接**。
- **判定**：**FAIL**——ICMP echo reply 未通、TCP 对外连接未建立。阻塞点在 ARP 解析：`kernel/net/ip.c:128-133` ARP 未命中即 `return -1`，ICMP/TCP 帧未出 guest；`kernel/net/arp.c:52` ARP 请求 `sender_ip=0` 未填本机 IP。
- **未定位部分**：ARP 应答为何未进缓存（slirp 未应答 vs e1000 RX 未上交 ARP 帧）未完全定位；缺 guest 侧 ARP 抓包 / 宿主 slirp 报文计数；DHCP Offer/ACK 分配到的 IP 仅打 VGA、未镜像串口，串口读不到分配地址。
- **证据**：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/ser.log`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/host-listener.log`、`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/32/net/evidence-summary.md`；脚本 `run_net32.sh`。
- **代码位置**：`kernel/net/ip.c:107-133`（发送/ARP miss 分支）、`kernel/net/arp.c:28-62`（请求组包，`:52` sender_ip=0）、`kernel/net/icmp.c:51-80`（自检组包不上线）、`:112-163`（`ping_once`）、`kernel/net/tcp.c:185-230`（connect/握手）、`kernel/net/ip.c:209-252`（协议/以太网类型分发）。

## 遗留项复核（2026-11-18，64 位与主机侧）

复核基线 HEAD `391e1db`，分支 `test/ledger-64-2026-11-18`；新证据根
`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/`。
三态口径：PASS=本机实测证据；FAIL=实测失败证据；不可取证=写明缺什么 + 代码位置/证据文件。

### 1) 64-加载屏静态帧 —— PASS
- 动作/命令：`bash tools/regress/build_all.sh` 构建镜像；`loading/poll_loading.sh` 经 monitor
  `screendump` 以 120 ms 间隔轮询从引导菜单到 handoff 的整段窗口，共 81 帧（`frames/p0000..p0080.png`，
  `frames/times.txt`）；`loading/classify.py` 按 `tools/hepboot/uefi/hepboot_gop.c` 的 font8x16
  离线渲染候选文案，在状态条区域做点阵模板匹配，阈值 score≥0.50 并目视复核。
- 观察：Loading `p0051-0054`（0.595）、Verifying `p0055-0058`（0.587）、Preheating `p0059-0068`
  （0.605）、Preparing to hand off `p0069-0078`（0.584）；代表帧 p0051/p0055/p0059/p0069。
  帧 1920×1080=2,073,600 px，背景非纯黑，NONBLACK=2,073,600。
- 判定：PASS（三阶段独立静态帧均命中）。
- 证据：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/evidence_summary.md`、
  `frames/`、`ser.log`。
- 代码位置：候选原文 `tools/hepboot/uefi/hepboot_uefi.c:1709/1740/1757/1771`；字体点阵 `tools/hepboot/uefi/hepboot_gop.c`。

### 2) 64-加载阶段帧覆盖 —— PASS
- 动作/命令：同上轮询与匹配；阶段文案去 `tools/hepboot/uefi/hepboot_uefi.c` 取原文。
- 观察：四个阶段（Loading 64-bit kernel image / Verifying kernel image / Preheating kernel pages /
  Preparing to hand off）独立帧齐备，无"采样间隔内闪过未命中"的阶段。
- 判定：PASS。
- 证据：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/loading/frames/`、
  `evidence_summary.md`（53 帧未命中候选的单列为过渡/菜单帧，不计入阶段帧）。
- 代码位置：同 1)。

### 3) 64-鼠标解锁路径 —— PASS
- 动作/命令：`mouse/mouse_unlock.py`，QMP `input-send-event` 绝对事件（usb-tablet，UHCI `-usb -device usb-tablet`）；
  归一 `vx=x*32767//1920`、`vy=y*32767//1080`。锁屏点 (960,540)；登录帧按填充色 (201,169,110)
  在 y560-660/x700-1220 条带内度量 Unlock 按钮，bbox x885-1034/y588-635、中心 (959,611)，左键 down/up。
  口令 `hepu123` 走 HMP `sendkey` 夹具（鼠标只负责两段点击）。
- 观察：三帧——锁屏（时钟 18:09、"Click any action to unlock"，cream 99.7735%、10 色）、
  登录页（U/hepu/空密码框/Unlock，cream 97.3368%、121 色）、桌面（Settings 窗口、顶栏 18:10、
  天气卡 13 多云，cream 427 px=0.0206%、6,323 色）。串口 L465 tablet/abs-HID ready、
  L608 `lock -> login (click)`、L621 `login -> desktop (auth ok)`、L622 `login -> desktop (button click)`。
- 判定：PASS。
- 证据：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/mouse/mouse_lock.png`、
  `mouse_login.png`、`mouse_desktop.png`、`ser.log`、`mouse_report.txt`、`mouse_unlock.py`。
- 代码位置：`kernel/arch/x86_64/hpdesk.c:5777`（lock→login）、`:5891-5896`（按钮提交→桌面）、
  `kernel/arch/x86_64/usb64.c:1456-1457`（平板坐标归一）。
- 过程备注：早期挂 qemu-xhci 时 proto=0 的 tablet 未被注册为绝对指针（XHCI 只分类 proto 1/2），
  事件退回 PS/2 落空；改 UHCI 后命中，详见 mouse_report.txt。

### 4) 64 位 KUnit —— 不可取证（缺运行入口）
- 动作/命令：`grep -rn "ktest\|kunit" kernel/arch/x86_64/`；`grep -n "kunit\|sched" kernel/arch/x86_64/Makefile`；
  对照 32 位链路 `kernel/sched/shell_cmd_system.c:75`、`kernel/sched/shell_execute.c:115`、
  `kernel/arch/x86/kernel.c:638-651`。
- 观察：`kernel/kunit/` 框架存在，但 64 位 Makefile 源清单不含 `kunit/` 与 `sched/`；
  `shell64.c` 命令表无 `ktest`。64 位 shell 输入 `ktest` 返回 `Command not found: ktest` 是路径必然结果。
- 判定：不可取证。缺的是 64 位 KUnit 编译入口与命令分发，不是"跑过未验到"。
- 证据：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/kunit/kunit_audit.md`；
  旧串口 `/home/user/Doubao/chats/38452875115179010/evidence-regress-2026-11-17/protect64/ser.log`。
- 代码位置：`kernel/kunit/`；`kernel/arch/x86_64/Makefile`；`kernel/arch/x86_64/shell64.c`；
  32 位接线 `kernel/sched/shell_cmd_system.c:75-80`、`kernel/sched/shell_execute.c:115`、`kernel/arch/x86/kernel.c:638-651`。

### 5) 安全启动验证桩 —— 不可取证（无调用方）+ 主机级单元调用佐证
- 动作/命令：`grep -rn "sb_verify_stage\|sb_verify_signature\|sb_verify_hash\|secure_boot_init\|SB_ERR_NOT_IMPLEMENTED" kernel/ user/ tools/`（排除桩自身与头文件）；
  主机 harness `secboot/harness.c` 直接编 `kernel/arch/x86/secure_boot.c` 调三接口（桩内输出转 stdout）。
- 观察：三 verify 桩在 kernel/user/tools 零调用方；唯一外部引用 `secure_boot_init()`
  （`kernel/arch/x86/init_sequence.c:101` 声明、`:370` 调用）；x86_64 不引用 secure_boot。
  harness：signature=-2、hash=-2、非严格 stage 返回 0 并 WARNING、严格模式 stage=-2，`HARNESS RESULT: PASS`。
- 判定：不可取证（启动链路不进入该路径）；harness 为**主机级单元调用，不是启动链路实测**。
- 证据：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/secboot/secboot_audit.md`、
  `harness.c`、`harness.log`。
- 代码位置：`kernel/arch/x86/secure_boot.c`、`kernel/include/secure_boot.h`、
  `kernel/arch/x86/init_sequence.c:101,370`。

### 6) OPEN-04 真实安装 —— 旧基线证据复核有支撑；HEAD 391e1db 未独立复跑
- 动作：对 `/home/user/Doubao/chats/38452875115179010/evidence-installer/` 逐项审计，并从留存原始磁盘
  `$CHAT/.toolchain/run/inst/installer/{src.img,target.img,standalone.img}` 独立复核扇区/ESP/关键文件/像素。
- 观察：P1（LBA 264192、245,760 扇区）`cmp -l` 差异 0；ESP 四文件 mcopy 提取后比对 SAME；
  8 个关键文件 size/blocks 两侧一致；standalone 帧 cream=704（0.000340）、distinct=4,369，与参考桌面帧同值；
  串口安装五步、独立启动 Desktop loaded/Initialized 1920x1080 在场。HEAD 代码在位
  （`install64.c` 335 行；白名单 HEAD 实际 `user_process.c:2285` 宏、`:2288-2298` 匹配函数，
  旧台账行号 2544/2547-2557 已漂移）。
- 判定：旧基线 `61c53fa` 产物范围内主张有支撑；HEAD `391e1db` 未独立复跑双盘安装（交互式菜单分支
  无自动化点击方案；双盘 + 独立启动两轮 TCG 受多 agent 锁争用；空白第二盘预置本轮未做），
  不外推旧结论到 HEAD。
- 证据：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/installer/OPEN-04-audit.md`；
  旧证据 `/home/user/Doubao/chats/38452875115179010/evidence-installer/`。
- 代码位置：`user/apps/install64/install64.c:165-187/281-294/299-308/312-321`；
  `kernel/arch/x86_64/user_process.c:2285,2288-2298`。

### 7) 主机 ASan/UBSan elf 用例 —— 已修复，8/0 PASS
- 动作/命令：定位 `tools/host-tests/run_host_tests.sh` elf `run_one` 行（原只编 elf64.c）；
  最小改动后跑全套主机测试。
- 观察：改动前 7 过 1 败，elf 链接 `undefined reference to compat_gate_detect_elf`；
  改动后 `=== 结果: 通过 8 / 失败 0 ===`，elf `ALL ELF TESTS PASSED`。
- 判定：已修复（测试脚手架链接清单问题，非内核运行时缺陷）。
- 改动文件/行号：`tools/host-tests/run_host_tests.sh:64`（elf 行补 `$ARCH/compat_gate.c`）；
  `tools/host-tests/elf_test.c:9-10`（补桩 `user_process_current_env_type()` 返回 0=ENV_NATIVE）。
  未把 `user_process.c` 编入（会级联 pmm/vmm/ext2/domain/uproc 一大批未定义符号）。
- 证据：`/home/user/Doubao/chats/38452875115179010/evidence-ledger-2026-11-18/64/host-tests/host-tests.log`。
