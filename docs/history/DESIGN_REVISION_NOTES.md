---
title: DESIGN_REVISION_NOTES
status: frozen
sys_version: 0.13.65
updated: 2026-10-24
owner: history
source_of_truth: null
supersedes: null
---

> 本节为 DESIGN.md 修订摘要(历史只读)。现行设计见 architecture/DESIGN.md。

## 文档说明

本文档为赫普OS自研移动端下一代操作系统的完整官方设计规范，涵盖系统定位、命名释义、底层架构、内核设计、应用格式、安全机制、AI架构、硬件适配、开源策略、厂商分发规则、现存劣势与修复方案。所有迭代开发均以此文档为准。

**V4.64 修订（E-109）**：**U盘引导链修复——IMG镜像真机/QEMU成功启动**——历经多轮定位修复了IMG镜像刷入后只光标闪、三重故障循环的问题。关键修复：① 第二次进保护模式ljmp加0x66操作数大小前缀（CR0.PE=0后CS缓存D位未刷新，CPU仍按32位解码→EA被读成7字节→selector读错→CS=0x0002(LDT)→#GP→三重故障）；② pm_read_seg2/rm_read_seg2里VGA写从实模式段地址0xB800改为物理地址0xB8000（保护模式下ES是selector不是段地址，movw $0xB800,%es会#GP）；③ MBR读stage2块数3→4（多读覆盖内核首扇区）；④ P0/P1延时0x7FFFFF→0xFFFF（QEMU下太慢）；⑤ 回实模式加jmp刷新+pushw/iret。QEMU实测：IMG镜像从MBR→stage2→保护模式→内核→shell全链路启动成功，用户进程init/procb调度正常。

**V4.56 修订（E101）**：**音频增强——shell audio 命令**——在既有 ALSA 式 PCM/mixer 框架基础上，新增 shell `audio` 命令（info/tone/vol/mute）。QEMU 实测：audio info 显示框架信息、audio tone 440 200 正确解析——详见文末 E-101 章节。
**GB2312 字库覆盖率统计**——在既有 2151 常用字 16x16 点阵字库基础上，cjk stats 增加 GB2312 覆盖率显示（2151/6763 = 31%）。QEMU 实测：glyphs=3775 slots=47/64 + GB2312 coverage 31%——详见文末 E-100 章节。
**man 手册补全——tcp/pm level/mac audit + 语言显示**——在既有 60+ 命令手册基础上补三本新手册（tcp 状态机全命令、pm level 省电等级、mac audit 审计日志），man 无参时显示当前语言。QEMU 实测：man tcp 完整页、man 列表 64 页含 (language: English)——详见文末 E-99 章节。
**MAC 安全深化——审计日志**——在既有 SELinux 式 MAC（规则引擎/enforcing 模式/持久化/策略文件保护）基础上，新增环形审计缓冲（最近 16 条决策：subject/object/perm/result），mac_check 每次决策记录 allow/deny/permissive。shell `mac audit` 命令 dump 审计日志。QEMU 实测命令正常（kernel 域操作不记录，符合设计）——详见文末 E-98 章节。
**设备树 DTB 深化——reg/irq 摘要**——在既有 DTB 框架（fdt 风格 header/prop/node、builtin 13 节点、compatible 驱动匹配、shell dtb show/get/find）基础上，dtb_show() 每个节点输出后追加 `reg=<基址> irq=<中断号>` 摘要。QEMU 实测：13 节点树完整 dump（/cpus/cpu@0/memory/soc 下 timer/keyboard/mouse/vga/ide/rtc/pm/touch 8 个子节点，compatible + reg/irq 全显示）——详见文末 E-97 章节。
**性能指标采集——CPU idle/busy 占比**——在既有 perf 诊断（ctx_switches/ticks/kheap碎片/slab/spin/softirq延迟直方图）基础上，process.c 的 sched_tick 增加 idle_ticks 计数（无运行进程的 tick），`perf` 命令 sched 行输出 idle%/busy%。QEMU 实测空闲态 idle=99% busy=1%——为后续"埋指标→压测→小步优化"流程提供数据基准——详见文末 E-96 章节。
**TCP 协议栈深化——真实状态机 + 缓冲 + 退避 + shell tcp 命令**——① 从"立即成功"模拟升级为符合 TCP 语义的状态机：connect 走 SYN_SENT→(模拟 SYN-ACK)→ESTABLISHED；accept 走 SYN_RECEIVED→(ACK)→ESTABLISHED；close 走 FIN_WAIT_1→(FIN+ACK)→TIME_WAIT→CLOSED；② 发送/接收缓冲（kmalloc 2KB/连接，close 释放）；③ 滑动窗口检查（未确认字节 + 新数据 ≤ 窗口）；④ 指数退避重传（RTO 起步每次×2，5 次放弃）；⑤ shell `tcp` 命令族（connect/listen/accept/send/recv/close/tick/list）；⑥ 修两个 shell bug（args 偏移按"tcp <子命令> "实际长度、close 与 connect 按第二字母区分）。QEMU 实测：完整状态流转 SYN_SENT→ESTABLISHED→send→ACK→FIN_WAIT_1→TIME_WAIT→CLOSED 全链路验证；ktest/schtest/ps 回归全绿——详见文末 E-95 章节。
**电源管理增强——省电等级 + 唤醒原因 + 电池续航估算**——① 新增 `pm level <0|1|2>` 省电等级（0=性能 10min / 1=平衡 5min / 2=省电 1min，实时调整自动休眠超时）；② `pm_wakeup_reason()` 带原因唤醒（键盘/定时器/外部），键盘底半部改走 PM_WAKE_KEYBOARD，`pm` 状态页可查最近唤醒原因；③ `pm_battery_minutes()` 续航估算（剩余容量/放电电流→剩余分钟）；④ `pm` 无参输出增加 level + 续航估算。QEMU 实测：level 2 → tmo 60s、battery est 259min、sleep→wake 统计 sleeps=1/wakes=1/total=2.49s；ktest/schtest/ps 回归全绿——详见文末 E-94 章节。
**USB 驱动栈真实化——UHCI 控制器初始化 + 根集线器端口扫描**——① 修 `pci_find_by_class(0x0C,0x03)` 子类号错误（USB 子类 UHCI=0x00，原 0x03 永远找不到控制器）→ 改为精确匹配 UHCI 子类，兜底任意 USB；② 无 BIOS 分配环境下驱动自分配 UHCI I/O BAR（Intel 规范 BAR4，QEMU 实测挂 BAR4；BAR0/4 双写兼容），写 PCI_COMMAND 使能 I/O 空间；③ 真实 UHCI 初始化：先清 RUN 停止（QEMU 上电 USBCMD=0xC1 已在运行，不停止无法复位）→ HCRESET → 4K 页对齐帧列表（FRBASEADD）→ RUN+CF → 等待退出 Halted；④ `usb_scan_ports()` 根集线器端口复位 + 连接检测（PORTSC CCS/CSS），每次扫描重建设备表（修重复登记）；⑤ shell `usb scan`/`usb` 增强。QEMU `-usb -device usb-mouse` 实测：UHCI 探测、端口1 检测到设备、端口2 空、设备表 1 项；ktest/schtest/ps 回归全绿——详见文末 E-93 章节。
**fs cp 子目录路径参数 + KHEAP bad magic 防御**——① `fs cp <src> <dst>` 的 src 支持子目录路径（`data/cpdocs` → 自动拆分父目录+名字，root 直接路径不变）；dst 子目录路径（write_raw 多级）验证可用。② KHEAP bad block magic 专项：caller 定位确认在 fs_cp_recursive 文件分支的 kfree(buf)；加固 read_file（读取失败 rd<0 或读数异常时不越界写 buf[rd]）+ 文件复制缓冲 +16 字节余量；kfree 增加对齐分配 slab 释放路径（aligned-4 存 raw，raw 为 slab 对象时用 raw 释放，消除误报与 slab 泄漏）。5 轮 cp→rm 压力测试 KHEAP 报错消失；cp src/dst 路径、rm 递归全绿；ktest/schtest/ps 回归全绿——详见文末 E-92 章节。
**rm 递归 + rename 幽灵目录项双修复**——① `fs_rm_recursive` 的 static 路径缓冲被递归子调用覆盖（unlink 误用子项路径导致父目录删除必然失败），unlink 前重建父目录路径；② `ext2_path_rename` 弃用 prev rec_len 合并删除（多次 rename 累积使目录项链 rec_len 虚大错位，add/remove 遍历跳项 → 幽灵目录项），改为与 unlink_v2 一致的置 inode=0 复用。3 轮 cp→mv→rm 压力测试全绿：root 目录项全部删除干净、ls 无任何幽灵项；debugfs 复核磁盘仅有标准 inode=0 空目录项（list 跳过）；rm 递归完整释放子项与本体 inode。ktest/schtest/ps 回归全绿——详见文末 E-91 章节。
**fs rm 目录递归删除 + fs mv 目录移动**——`fs rm <name>` 升级为自动识别：文件直接删除、目录先递归删子项再删自身（先 sync 落盘 + unlink 重试防块缓存时序）；`fs mv <src> <dst>` 验证支持目录重命名/移动（ext2 rename 的 file_type 区分 DIR/REG）。修 rm 递归两个 bug：① 递归后父目录 unlink 因路径无前导 `/` 失败；② 输出 static 缓冲被递归覆盖误导。QEMU 实测：rm 单文件、rm 目录递归（子项+父目录）、mv 目录闭环全绿，ktest/schtest/ps 回归全绿——详见文末 E-90 章节。**已知问题**：ext2 rename 后 root 可能残留指向已释放 inode 的幽灵目录项（ls 显示重复条目），列入 E-91 专项修复。
**fs cp 目录递归/批量复制**——`fs cp <src> <dst>` 升级为自动识别：文件复制（按目标 size 整量读+写，支持大文件）、目录递归复制（mkdir 副本根 + 遍历子项递归，深度上限 4）；新增 `fs cpall <dst>` 批量复制根目录全部条目。修复三个 bug：① 递归时源子目录路径未切换（目录复制走文件分支报 read FAIL）；② write_raw 拼路径缺前导 `/`（ext2_resolve_path 要求绝对路径，导致批量复制大面积 FAIL）——write_raw 内部兼容无前导斜杠；③ 单文件复制目标无目录分隔时报 bad dst（目标直落根目录）+ cpall 自我嵌套防护（跳过目标自身）。QEMU 实测：单文件复制内容校验（abc→abccopy2=hello）、目录递归（docs→docbak2，note.txt 24 字节）、批量 cpall 16 items 全绿，ktest/schtest/ps 回归全绿——详见文末 E-89 章节。
**MAC 规则删除 + 策略文件保护 + 截断修复**——新增 `mac_rule_del(sub,obj)` 与 `mac del <sub> <obj>` 命令（精确匹配删除）；策略文件权限保护 `mac_protect_policy_file`（用户态进程即使规则命中也不得访问 selinux.rules，只允许内核域读写）；**修复短写覆盖残留 bug**：ext2_write_data 不截断 i_size（只增不减），del 后 save 短内容覆盖长文件会残留旧规则尾部、重启后"删掉的规则又回来"——mac_file_write 改为**先 unlink 旧文件再重建**。QEMU 实测：add a1/a2 → del a2 → save → 重启 → autoload 加载 a1（a2 不回来），删除持久化闭环——详见文末 E-88 章节。
**SELinux 规则持久化**——MAC 安全模块新增规则落盘/读取/启动自动加载：`mac save [path]`（默认 /selinux.rules，写盘后强制 `ide_ext2_sync` 落盘，防脏缓存重启丢失）、`mac load [path]`（先清空再填入，含 enforce 状态行）、`mac_autoload`（挂 shell_main 入口必经路径，内部先 `ide_ext2_mount` 确保数据盘就绪）；修复三处真 bug：① MAC 默认拒绝拦死内核自身写配置——内核管理域（subject=kernel）特权放行（SELinux kernel_t 语义）；② save 未 sync 脏缓存重启丢失——写后强制回写；③ autoload 挂 kernel.c 非必经路径且磁盘未 mount——移到 shell_main 入口并加 mount guard。QEMU 实测：add 两规则 → save → 重启 → 自动加载 → mac show 恢复 app1/init 两条规则，全绿回归——详见文末 E-87 章节。
**WM 窗口缩放（PINCH/ZOOM 真实手势驱动）**——新增 `wm_resize_window(win, dw, dh)`：边界夹取（最小 80x40、最大 640x480、不越右/下边界）、WM_EVENT_RESIZE 事件回调、重绘；wm_handle_gesture 的 PINCH→缩小焦点窗口、ZOOM→放大焦点窗口（无焦点窗口时输出保护提示）；wm.h 事件枚举新增 WM_EVENT_RESIZE。QEMU 实测 pinch/zoom 分支正确（文本模式无焦点→保护分支），全绿回归——详见文末 E-86 章节。
**WM 手势轮询联动**——wm_main_loop 新增手势事件轮询块（`input_gesture_poll` 每帧取事件）；手势处理抽为独立函数 `wm_handle_gesture`（SWIPE_LEFT/RIGHT→wm_focus_next+重绘、DOUBLE_TAP→wm_activate_focus、PINCH/ZOOM/LONG_PRESS→缩放/上下文菜单槽位日志），wm 主循环与 shell 文本验证共用；新增 `gesture ui <type>` 命令（文本模式喂事件给 wm_handle_gesture 验证 UI 逻辑）；wm 键盘块新增调试注入键 '1'(右滑)/'2'(双击)/'3'(双指缩放)。QEMU 文本模式实测四类手势处理全绿；图形模式 sendkey 键盘不可达属 QEMU 环境限制（真机触屏中断路径生效）——详见文末 E-85 章节。
**手势事件环形队列**——input.c 新增 16 槽环形队列，detect_gesture 每类手势（TAP/DOUBLE_TAP/LONG_PRESS/SWIPE×4/PINCH/ZOOM）判定后入队（满则丢弃计数）；新增 `input_gesture_poll`（UI 主循环轮询取事件）/`input_gesture_queue_clear`/`input_gesture_queue_stats`；`gesture q [drain|clear]` 命令驱动验证（修复 args+1 为空格导致子命令不匹配的解析 bug）；实测 clear → 注入 double+pinch+swipe → pending=5 → drain 输出完整 5 事件（TAP×2+DOUBLE_TAP+PINCH+SWIPE_RIGHT）——详见文末 E-84 章节。
**手势阈值可配置**——detect_gesture 的 LONG_PRESS 时长/双击窗口/滑动最小位移从常量改为可配置变量（默认 50/30/50），新增 `input_gesture_config`/`input_gesture_cfg_get` 接口与 `gesture threshold [lp] [dt] [sw]` 命令（只传部分参数时其余保留）；实测默认 50/30/50、设 80/25/70 后 60ms 长按判 TAP、调低 40 后恢复 LONG_PRESS、仅传 lp 其余保留——详见文末 E-83 章节。
**LONG_PRESS 长按手势**——detect_gesture 按下时记录起始 tick（pit_get_ticks），抬起时位移小（dist<100）且按住 ≥50 ticks（500ms）判 LONG_PRESS（计数+事件回调），否则按 TAP 走双击判定；长按打断双击窗口（last_tap_ticks=0）；`touchtest long` 忙等 600ms 模拟长按——实测 `[UI] LONG_PRESS @(240,160)` + 计数 1，全手势（TAP/DOUBLE/PINCH/SWIPE/LONG_PRESS）齐备——详见文末 E-82 章节。
**手势事件驱动回调管道**——新增 `gesture on|off|test` 命令注册/注销 UI 层手势回调（`input_set_gesture_callback` + `hepu_ui_gesture_cb` 事件即时打印：TAP/DOUBLE_TAP/PINCH/SWIPE 带坐标位移）；双击判定补发 DOUBLE_TAP 事件（此前只计数不回调）；实测 `gesture test` 与 `touchtest double/pinch/swipe` 事件流完整（TAP×2 + DOUBLE_TAP + PINCH + SWIPE_RIGHT）——详见文末 E-81 章节。
**触屏手势识别修复**——三个根因：① input_init/touch_hw_init 位于用户态进程创建之后，proc1 经 SYS_EXIT→sys_exit 直达 shell 导致输入子系统从未初始化、触屏 dev_id=-1、手势静默失效（移至进程创建前无条件初始化）；② 手势起点坐标 bug（down 时 TRACKING_ID 先于 POS 到达、touch_start 用旧坐标致首次 tap 误判，改 X/Y 独立 valid 首个 POS 初始化）；③ touch_sim_inject 无移动模式（down=1 重复注入 TRACKING_ID 误判双指，加 down=2 移动模式）。touchtest 补 swipe 分支、子命令前缀消歧（sw/st）；实测 TAP 2 + SWIPE_RIGHT 1 + PINCH 1 + DOUBLE_TAP 1 全绿——详见文末 E-80 章节。
**传感器模拟注入 + %f 真 bug 修复**——`sensors sim <id> <x> <y> <z> <v>` 手写整数解析注入；`sensors demo` 一次注入 8 类模拟值；修复 sensors 输出 `%f`（内核 vga_printf 不支持浮点）改整数显示——详见文末 E-79 章节。

**V4.33 修订（E78）**：**help 分组标题 i18n**——新增 6 条分组消息（系统/信息/进程/文件/设备/安全，四语言），help_group_title 改查 i18n_get，`lang set zh` 后 `help all` 分组标题全中文渲染——详见文末 E-78 章节。

**V4.32 修订（E77）**：**i18n 标签全覆盖**——sysinfo 的 uptime/内存使用标签、fs df 的存储剩余标签均走 i18n_get（含中文渲染）；修复 ide_ext2.c 硬编码枚举错位（I18N_STORAGE_FREE=9）——详见文末 E-77 章节。

**V4.31 修订（E76）**：**输入法候选翻页 UX**——readline TAB 内嵌 IME 增加 0 键翻页（`0:pg(x/y)` 提示），多页候选可翻页浏览后选字——详见文末 E-76 章节。

**V4.30 修订（E75）**：**ext2 文件管理命令深化——`fs cp <src> <dst>`**——读源文件→写目标（ide_ext2_read_file + write_raw），实测 `fs write hello hello` → `fs cp hello hello2`（5 字节复制）→ `fs cat hello2` 内容一致——详见文末 E-75 章节。

**V4.29 修订（E74）**：**i18n 命令组输出全覆盖**——unknown cmd、help 标题、ps 标签（进程总数）等高频输出走 i18n_get，`lang set zh` 后全中文渲染（实测"未知命令: qzz (help)"、`进程总数: 1, running: 0`）——详见文末 E-74 章节。

**V4.28 修订（E73）**：**GB2312 一级字库扩全（3755 字）+ 完整拼音字典集成**——字库从 173 字扩至 3775 字（GB2312 一级 3755 + 原字/标点，unicode 有序）；vga_cjk 查找改二分（O(log n)）；readline TAB 内嵌 IME 改用旧 IME 完整拼音表（词组+简拼+单字+频率排序，`ime_query_py` 新接口）；vgaread 槽码显示扩至 0x80-0xDF——详见文末 E-73 章节。

**V4.27 修订（E72）**：**内嵌式 IME**——`keyboard_readline` 内嵌拼音输入：输入拼音后按 TAB 弹出候选行、数字选字、原位替换拼音（ime_candidate_count/ime_candidate_utf8 接口）——见文末 E-72 章节。

**V4.26 修订（E71）**：**IME 上屏集成**——`keyboard_readline` 增加 UTF-8 三字节收包状态机（0xE0 起始 → 组合 → CJK 渲染进命令行）；`ime pick` 选字后经 `keyboard_inject` 注入键盘缓冲，汉字直接上屏到 shell 命令行（实测 "中/文" 进命令行并被 shell 解析）；IME 从"演示"走向"可用输入通道"——详见文末 E-71 章节。

**V4.25 修订（E70）**：**拼音输入法引擎（IME）**——`kernel/drivers/ime.c` 24 条拼音字典（覆盖字库常用字）、`ime <py>|pick <n>|buf|clear` 命令、cjk_render_unicode 单字渲染接口；修复候选/提交逐字节 UTF-8 乱码（改走 CJK 管线）与 `ime buf` 条件写反（`==0`→`!=0`）；实测 "中文" 输入链路全绿——详见文末 E-70 章节。

**V4.24 修订（E69）**：**横幅 i18n 化**——`banner` 命令按当前语言重绘欢迎横幅（实测 `lang set zh` 后中文横幅按字渲染）、about 版本标签走 i18n_get、首次引导横幅 i18n——详见文末 E-69 章节。

**V4.23 修订（E68）**：**程序化 CJK 字库 + 动态槽位**——`tools/gen_cjk_font.py`（PIL + NotoSerifCJK-Bold）生成 173 字 16x16 点阵库（39KB）；`vga_cjk.c` 64 字滑动窗口（静态预载 32 + 动态 LRU），`cjk stats`；**修复 `vga_printf %s` 逐字节破坏 UTF-8 的真 bug**——改走 vga_puts CJK 管线（实测 `lang set zh` 中文按字渲染、命中统计正常）——详见文末 E-68 章节。

**V4.22 修订（E67）**：**CJK 16x16 点阵渲染管线**——`vga_cjk.c` 标准 VGA 字体写入序列（SR4 关奇偶 + SR2 plane2 + GC6 段选择）把 CJK 子集写入 plane 2 槽（每字 2 槽），`vga_puts` 集成 UTF-8 3 字节解码 → 槽码输出；`cjk check` 读回比对 **MATCH**、`cjk <text>` 命令、vgaread 支持 0x80+ 槽标记——详见文末 E-67 章节。

**V4.21 修订（E66）**：**驱动×设备树集成**——`dtb_self_check()` 启动自检（PIT 频率/VGA 尺寸/RTC IRQ vs DTB 描述一致性）、`sysinfo` 增加平台行（实测 `platform: hepu-x86 (dtb=13 nodes, cpu=1000MHz)`）——详见文末 E-66 章节。

**V4.20 修订（E65）**：**内置设备树（DTB）**——`dtb_init(NULL)` 构建 Hepu 参考平台树（13 节点：CPU/内存/定时器/键盘/鼠标/VGA/IDE/RTC/PM/触屏），`dtb show` 树形显示、`dtb find <name> <prop>` 按名前缀查询属性（实测 rtc interrupts=8、vga cols=80）；dtb_node_at/count 访问器——详见文末 E-65 章节。

**V4.19 修订（E64）**：**多语言 i18n 框架**——`kernel/i18n/i18n.c`：EN/ZH/JA/DE 四语言消息表（UTF-8 存储）、`lang list/set/show` 命令；实测 `lang set de` 输出德文、`lang set zh` 切换成功（非拉丁字符因 VGA ASCII 字库暂显占位，待位图字库）——详见文末 E-64 章节。

**V4.18 修订（E63）**：**SELinux 式强制访问控制（MAC）**——新模块 `kernel/security/mac.c`：subject(进程名)×object(文件前缀)×perm(R/W/X) 规则表、默认拒绝（enforcing）/放行（permissive）；ext2 VFS read/write 挂钩；`mac add/show/enforce/stats` + `fwrite` 测试命令；实测无规则写被拒（wrote -3 EACCES）、加规则后放行——详见文末 E-63 章节。

**V4.17 修订（E62）**：**ext2 磁盘配额**——`ext2_write_data` 写前预检新增块上界、写后按实际分配记账（uid 取 current->pid，root 无限制）；新增 `quota set <uid> <blocks>` / `quota show` 命令；实测 root 记账 527 块、uid 9 限额 5 块生效——详见文末 E-62 章节。

**V4.16 修订（E61）**：**软中断延迟直方图**——softirq 消费时按 raise→run 的 tick 差分桶统计（0-1/2-5/6-10/11-20/21-50/>50），`softstat` 显示直方图与平均延迟；实测 192/193 次在 0-1 tick 内处理、avg=1 tick（底半部机制高效）；`perf` 同步展示——详见文末 E-61 章节。

**V4.15 修订（E60）**：**内核态异常自动恢复**——内核态异常（ISR 6/13 等）halt 前检查 iret-target eip 合法性：eip 不在内核文本区（如弹到 VGA 显存 0xA0000 的垃圾帧）→ 视为栈损坏，cli + 重置 current=NULL + TSS.ESP0=内核初始栈 + longjmp 回 Shell（系统不死）；正常内核地址崩溃仍 halt 便于定位。3 轮 schtest 压测零 ISR、ps 判据绿——详见文末 E-60 章节。

**V4.15 修订（E60）**：**内核异常自恢复**——内核态异常时检查 iret-target EIP 合法性（0x10000~0x9F000），非法（栈损坏/垃圾帧，如 eip=0xA0000 显存）不再直接 halt，而是 cli + 重置 current/TSS.ESP0 到内核初始栈 + longjmp 回 Shell 主线——"隔离出错任务不宕整个内核"；连续 3 轮 schtest+ps 零 ISR 通过——详见文末 E-60 章节。

**V4.14 修订（E59）**：**闹钟完善**——`rtc alarm add weekday <hh:mm>` 一键设置周一至五（展开 5 个 weekly 槽位）；`rtc alarm snooze <min>` 稍后提醒（闹钟触发后追加 N 分钟一次性闹钟，无触发时提示）；槽位上限 4→8；修复 snooze 解析缺 `q++` 导致数字输入无限循环（反汇编 jmp 自身证实）；以 volatile 函数指针挂钩规避 gcc -O2 对分支体 DCE 删除——详见文末 E-59 章节。

**V4.13 修订（E58）**：**多组闹钟并存**——RTC 闹钟槽位化（最多 4 个，一次性/每日/周几三类可同时存在）；`rtc alarm add <sec>` / `add daily <hh:mm>` / `add weekly <dow> <hh:mm>` 追加槽位，`rtc alarm list` 列出，`del <idx>` 删除，`off` 清空；IRQ8 遍历槽位，时间匹配类共享一次时间读取；旧接口（alarm <sec>/daily/weekly）兼容并入槽位；实测 slot0 one-shot + slot1 daily 并存、oneshot 触发自动移除、回归零 ISR——详见文末 E-58 章节。

**V4.12 修订（E57）**：新增 `man` 详细命令手册——55 个手册页（man <cmd> / man all），覆盖全部命令组的用法、参数、示例与注意事项（help 表确认 57 条命令全有条目，无遗漏）；纯 ASCII 输出避免 VGA 中文字库乱码；实测 `man pm`/`man rtc` 显示正常，回归零 ISR——详见文末 E-57 章节。

**V4.11 修订（E56）**：新增**周几重复闹钟**——`rtc alarm weekly <dow> <hh:mm|hhmm>`（dow 支持 0-6 或 mon/sun 名称），按星期+时间匹配，触发后保留每周重复（同分钟不重复触发）；与一次性/每日闹钟互斥；修复三处 bug：① MC146818 weekday 映射（1=周日..7=周六 → 0=周日..6=周六，原 `%7` 错位一位）；② 数字 dow 解析后未前进指针（`weekly 6 1419` 被解析为 6:00）；③ rtc_irq_handler 开头早退条件未含 weekly（weekly 分支永不执行）；实测 `weekly thu 14:34`+`pm sleep`→`weekly alarm fired`→系统唤醒，回归零 ISR——详见文末 E-56 章节。

**V4.10 修订（E55）**：**版本体系修正**——内核版本与系统版本分离：`hepu_kernel_version` 恢复独立内核版本 "1.0.2"，新增 `hepu_system_version` "1.02"（与镜像命名 hepu-os-v1.02.* 一致）；`version`/`about` 命令分开显示 System 版本与 Kernel 版本；文档明确三版本体系（系统/内核/文档各自独立演进）——详见文末 E-55 章节。

**V4.09 修订（E54）**：新增**每日闹钟**——`rtc alarm daily <hh:mm>`（支持 `1301` 无冒号格式）按时间匹配触发，与一次性闹钟互斥；RTC 秒中断（1Hz）内每秒读 CMOS 时间匹配 hh:mm，触发后置唤醒 pending（与一次性闹钟同链：softirq→pm_wakeup）并自动关闭当次；实测 `daily 13:22`+`pm sleep`→`[RTC] daily alarm fired`→闹钟时间后系统恢复响应；回归零 ISR；清理命令表重复 about 行——详见文末 E-54 章节。

**V4.08 修订（E53）**：新增 `about` 完整系统信息命令（版本/架构/kheap/进程/特性清单：ext2、电源管理、RTC 闹钟、多任务、ELF/.hug、设备、软中断）；统一内核版本串 `hepu_kernel_version` 为 "1.02"（与系统版本/镜像版本体系统一，此前 "1.0.2" 与用户版本体系不一致）；清理重复 cmd_about 定义（保留完整版）；实测 `about` 输出完整特性、`version` 显示 1.02；回归零 ISR——详见文末 E-53 章节。

**V4.07 修订（E52）**：新增**低电量自动深度睡眠**——电量 ≤5% 且放电时 pm_tick 自动置 pending（仅置标志，不在中断上下文睡眠），shell 输入等待循环（keyboard_getchar）消费后进入 pm_sleep 深度睡眠（RTC 闹钟/按键可唤醒）；新增**唤醒抑制窗口**（唤醒后 30s 内不重复自动睡，防"睡-醒-睡"死循环）；修复 pm_batt_set 旧逻辑直接置 SUSPEND（绕过抑制窗口）——统一走 pm_tick 自动路径；实测 `pm batt 3` → 自动 SUSPEND → 按键唤醒 → 抑制窗口内正常执行 ps 两次；回归零 ISR——详见文末 E-52 章节。

**V4.06 修订（E51）**：新增 **RTC 闹钟唤醒**——CMOS RTC 实时钟驱动扩展：`rtc` 命令显示 CMOS 时间（2026-09-17 12:15:10 实测走动）、`rtc alarm <sec>` 设置 N 秒后闹钟（1Hz 周期中断 IRQ8 + 软件倒计时）、`rtc alarm off` 关闭；IRQ8 顶半部注册（irq_register_handler(8)）只清中断+倒计时，到点置 pending 由 softirq 底半部调 pm_wakeup 唤醒（与键盘唤醒同链）；实测 `rtc alarm 3` + `pm sleep` → 3 秒后 RTC 自动唤醒、睡眠统计计入（total=9.49s）、`alarm: fired` 确认；回归 ktest+schtest+ps 全绿、零 ISR——详见文末 E-51 章节。

**V4.05 修订（E50）**：新增 `sysinfo` 命令——系统性能指标采集汇总：运行时间（sched tick，100Hz）、进程分布（sched_stats）、上下文切换次数（sched_switches，切换点计数）、关键 IRQ 计数（PIT/键盘/鼠标/总数）、kheap 用量（used/free/total + 整数百分比，内核无浮点）；验证：ktest 后 ISR=0、指标数值与 uptime/PIT 计数自洽（PIT=28803 ≈ uptime 288.03s×100）——详见文末 E-50 章节。

**V4.04 修订（E49）**：电源管理增强——`pm sleep` 从"仅置状态标志"升级为**深度睡眠等待循环**（SUSPEND 置状态+灭屏+冻结后台 → softirq 驱动键盘唤醒 → hlt 停机省电，带超时保护）；新增**唤醒冷却**（进入睡眠后 10 ticks 内扫描码视为残留不唤醒，防 8042 延迟注入的释放码立即唤醒）；新增**睡眠统计**（sleeps/wakes/total/last 时长，`pm` 无参显示）；新增 `keyboard_flush_raw()` 清空键盘残留（睡眠前调用）；回归：ktest+schtest+ps 全绿、零 ISR——详见文末 E-49 章节。

**V4.03 修订（E48）**：ext2 写入全链路验证与增强——数据盘可写（`fs write` 自动创建/落盘）、持久性验证（重启后数据保留）、新增 `fs stat`（inode/大小/块数）、`fs df`（空间统计）、`fs sync`（显式落盘）；修复 i_blocks 统计（旧按 size 推算与实际分配不符，改为按实际分配块数计数，host debugfs 核对一致）；host 侧 debugfs 独立验证落盘正确性——详见文末 E-48 章节。

**V4.02 修订（E47）**：IO 策略框架——设备注册表（键盘/鼠标=中断驱动、IDE/串口=轮询）、忙等让步（`iopolicy_relax` 每 64 次 pause，消除忙等空转）、`iostat` 命令（IRQ/轮询/让步计数）、IDE 等待循环接入让步。稳定性三连修：进程退出保留主线内核栈（防 kheap 复用踩踏）、主线 Shell 锚定链接器保留栈 `__stack_top`（汇编入口 `hepu_enter_mainline`，与 kheap 彻底隔离）、主线独立中断栈 `mainline_irq_stack`（TSS.ESP0 指向 bss 静态栈，中断帧与主线栈分离）——详见文末 E-47 章节。

**V4.01 修订（E46）**：新增 IDE 总线主控 DMA（BMIDE）驱动与批量 IO 接口。PCI 枚举（0xCF8/0xCFC 全设备×全功能，class 读 offset 0x08）定位 IDE 控制器并驱动 BMIDE（BMCMD/BMSTATUS/PRDT）；对外 ide_batch_read/write 批量接口（≤2 扇区 DMA，大请求 PIO 128 扇区分段，失败回退）；shell 新增 `dmatest` 验证 PIO/DMA 一致性+耗时；QEMU TCG 经 hypervisor leaf 0x40000000（"TCGTCGTCGTCG"）检测自动禁用 DMA 回退 PIO（QEMU 7.2 BMDMA 单条 PRD 上限 1024B 且竞态不稳），真机硬件正常启用——详见文末 E-46 章节。

**V4.00 修订（E45）**：缓存友好布局——process_t 调度热字段聚簇到同一缓存行（热路径缓存行 4→2）、新增 cacheinfo 布局诊断命令、ISR 6 残留窗口彻底关闭（sys_exit 第二循环全程 cli，3 轮组合零复发）、-m32 偏移复核（page_dir@68 正确）——详见文末 E-45 章节。
**V3.99 修订（E44）**：根治 ktest+schtest 组合偶发 ISR 6——内核态异常打印 iret 目标（EIP/CS/EFLAGS）→ 定位 EIP=0xB8965（VGA 显存区，iret 弹出显存垃圾）→ 根因=sys_exit 回收自己内核栈后到 longjmp 之间中断压栈到已释放栈 → 修复=kfree 自己栈前 cli + hepu_longjmp 目标处 sti——详见文末 E-44 章节。
**V3.98 修订（E43）**：压力测试自动化——`ktest` 一键全量 KUnit 回归、压力套件（kunit_stress）、修复 10 个既有失败测试（含 SHA-256 ROTL/ROTR 真算法 bug、path_normalize 双斜杠 bug）、回归脚本 tools/regression.sh、失败/断言落串口——详见文末 E-43 章节。
**V3.97 修订（E42）**：CPU 亲和性——process_t 亲和位图、`sched_set_affinity` API（校验 mask 与在线 CPU 有交集）、`smp_online_cpus()`、调度器 pick 尊重亲和性、`affinity` 命令、ps aff 列——详见文末 E-42 章节。
**V3.96 修订（E41）**：指标采集补实——进程阻塞时长统计（ps blk 列）、kheap 碎片率（空闲块/最大连续块/碎片率%）、perf 增补上下文切换与系统 tick 行——详见文末 E-41 章节。
**V3.95 修订（E40）**：死锁检测实用化——spinlock_t 增加持锁监控字段（hold_tick/holder_pc）、锁监控注册表、IRQ0 每 tick 持锁超时告警（`[LOCKWARN]` 带调用者 PC）、`lockstat`/`lockhold` 命令——详见文末 E-40 章节。
**V3.94 修订（E39）**：编译优化切换 -Os → -O2（禁用 strict-aliasing/向量化防误优化）+ 全量回归通过——详见文末 E-39 章节。
**V3.93 修订（E38）**：中断底半部 softirq 框架——类型表驱动（键盘/鼠标真实底半部 + 通用 fn 队列）、延迟统计、`softirqtest`/`softstat`/`softtick` 命令；修复系统 tick 在 idle 时不递增的隐藏缺陷——详见文末 E-38 章节。
**V3.91 修订（E37）**：资源限额强化——内存超限持续处置（降级 BKG + 超限回收）、统一 `rlimit` 命令、kheap 低水位保护（512KB）——详见文末 E-37 章节。

**V3.90 修订（E36）**：就绪位图调度加速（O(1) 优先级定位）+ 运行时 `prio` 命令（RT/NORM/BKG 动态调整）+ ps 优先级列——详见文末 E-36 章节。

**V3.89 修订（E20~E35）**：崩溃日志、ext2 修复、fsck、调度器优化、僵尸清除、内存报表、看门狗增强、IRQ0 延迟统计、内核栈 canary、proc_table 表级锁（E-35）——详见文末 E 系列章节。

**V3.88 修订**：新增「开发规范（硬性约束）」章节——多平台兼容层的用户态边界、统一沙盒、兼容性预期管理三条最高优先级约束。

---

