---
title: 赫普OS 未关闭问题清单
status: active
sys_version: 0.58.0
updated: 2026-11-12
owner: status
source_of_truth: kernel/arch/x86_64/
---

# 未关闭问题清单（OPEN BUGS）

> 只收录未关闭项。已修复项见 history/devlog/ 与 VERSIONING.md。
> 来源标注：DEVLOG=开发日志、BOOTLOADER_PLAN=引导器方案、VERSIONING=版本记录、SOURCE=源码。

| 编号 | 问题 | 现状依据 | 状态 |
|------|------|---------|------|
| BUG-13 | ATA PIO 读盘 BSY 恒 1 | DEVLOG 01 L25 标"待解决" | 待解决 |
| BUG-48 | INT13 磁盘重置在关中断下调用 | boot/BOOTLOADER_DEFINITION_PLAN.md, 待 QEMU 验证 | 待验证 |
| OPEN-01 | QEMU 高 LBA(P2/P3) 写成功读全 0 | VERSIONING.md 0.13.57 已知限制 | 验证中 |
| OPEN-02 | UHCI 写路径时序竞态 | 0.13.42 遗留 | 待解决 |
| OPEN-03 | ~~QEMU sendkey 不被键盘驱动读取~~ **已关闭（非真 bug，旧测量误解）**。PS/2 与 USB HID 两条键盘链路端到端可用，无需先点击聚焦。四重实测证据（ef3c7cc 内核，PS/2 配置未挂 USB）：① 中断级 `g_kbd_irq_count`（符号地址 0x1f29f4）每次按键 +2（按下/释放各 1 次），键盘环形缓冲 head/tail 同步前移且条目被消费；`sendkey 2` 后计数 11→13、缓冲指针 3→4；② 界面级 `sendkey 2` 使 OOBE 由 1/4 推进到 2/4（截屏像素差 73616 px），账户创建页输入 h/e/p/u，用户名框真实回显 "hepu"；③ USB HID 键盘对照（`-device piix3-usb-uhci -device usb-kbd`）sendkey 改走 USB 队列（此时 PS/2 IRQ1 静默），同样推进向导、回显 hepu、Tab 切换输入框焦点（像素差 3008 px）。**应用层边界（避免再误判）**：OOBE 语言页只绑定数字键 `1`/`2`，Tab/方向键/回车在该页未绑定，属应用设计；旧「按了没反应」源于此，加之此前缺中断级取证，把「界面没变化」误判为「驱动没收到键」 | `g_kbd_irq_count` 中断计数 + 环形缓冲指针；OOBE 页面推进与文本回显；USB HID 对照链路 | 已关闭（非缺陷） |
| OPEN-04 | 安装器不拷贝系统文件 | install64.c L213-L231 | 待解决 |
| SEC-01 | ~~用户程序可直接读写内核内存~~ **隔离主路径已修复**：私有 PML4/PDPT 默认开（`Makefile:205` `USER_ISOLATION ?= 1`）、SMEP+SMAP 按 CR4 真实状态门控、brk 分页+NX(W^X)；QEMU 512MB 隔离模式到桌面、sec_test 通过。**边界口径（勿与下文混淆）**：物理内存 >1GB 时内核恒等映射覆盖用户窗口，自动退回旧模式（`user_aspace64.c:76-77`）；fork 的 COW 复制已落地（`uaspace_fork_copy`），clone 仍共享父页表，见 SEC-02（部分）。**v0.33.0 补**：用户堆/TLS 预留区搬入用户窗口私有分区（PDPT[1]，原落在内核共享 PDPT[0] 树导致用户态写自己堆 #PF）；大页不可当页表写（新增 vmm64_split_huge_pde）；PMM 显式保留内核栈页（原与用户栈物理重叠） | user_aspace64.c:76-77；user_access64.c/vmm64.c/pmm64.c；Makefile:205；提交 3375fbe | 已关闭（隔离主路径；RAM>1GB 退回旧模式与 fork 复制见 SEC-02） |
| SEC-02 | **状态精确化（2026-11-12 源码复核）**：① **fork 已接入 COW** —— `sys_fork`（`syscall64_full.c:1875`）调 `uaspace_fork_copy`（`user_aspace64.c:165`），深拷贝 PML4/PDPT/PD/PT、物理页共享 + `cow_ref` + 清 W 位 + 打 `UA_COW` 标记，缺页走 `vmm64_try_cow`（`vmm64.c:432-457`，PAGE_COW bit9）。旧表述「fork 未接入」不成立。② **clone 未实现（返回 ENOSYS）** —— 原 `sys_clone` 用错误的结构体偏移（`+8` 写 active 实际落在 rip、`+24` 写页表实际落在 rflags、真正的 `active(+4)`/`page_table(+32)` 从未赋值），导致子槽**永远不会被调度**，clone 只是返回一个 pid 的空实现；一旦把 active 改对，子线程会带脏页表启动。v0.57.1 已改为明确返回 `-38 ENOSYS`，真线程需 trampoline + 独立内核栈 + 子 rax 清零 + 共享页表引用计数（见 `FIXES_0_57_0.md` §1.82）。③ 旧模式（非隔离）下页表/用户页帧仍不释放（隔离模式已回收） | `syscall64_full.c:915-953`、`user_aspace64.c:165`、`vmm64.c:426-457`、`user_process.c user_process_release` | 部分（fork COW 已闭环；**clone 已诚实化为 ENOSYS**，真线程未实现） |
| SEC-03 | fork 父子进程调度。**v0.34.0 已修复**：根因是 schedule() 里 `if (old && old->stack)` 跳过上下文切换，去掉后子进程可调度；子进程exit后通过 fork_return_to_parent 切换到父进程CR3+恢复GPR快照+sysret回父用户态。forktest端到端通过：CHILD输出可见、g_shared隔离正确、PARENT恢复运行、干净exit(0)。 | process64_full.c, syscall64_full.c, entry_64.S | 已关闭 |
| SEC-04 | 裸盘读写/reboot/mkfs 已加 **PID==0 临时闸**（`sys_disk_read` `syscall64_full.c:2572`、`sys_disk_write:2587`、`sys_reboot:2614`、`sys_mkfs:485`），拒绝所有真实用户进程（PID 恒 ≥100，口径见文末「PID 权限闸口径注记」）；但仍缺 uid/capability 能力模型，`sys_kill` 目前仅限自身。用户态 install64/mbrtool 的 reboot/mkfs/disk 路径属已知不完整遗留（见 OPEN-04） | syscall64_full.c:485,2572,2587,2614；wrap_kill | 待解决（临时闸已加；能力模型未落地） |
| SEC-05 | 原 `kernel_buf`/`file_buf`/`elf_buffer` 已改为调用期 `kmalloc`（`syscall64_full.c:276`、`:378`、`:1975`，提交 3375fbe）；残留 `sys_readdir` 静态 `names[64][256]`/`types[64]`（`:1606-1607`）仍不可重入；内核栈仅 4KB 的约束仍在 | syscall64_full.c:276,378,1606-1607,1975 | 部分（主路径缓冲已堆分配；readdir 静态表与 4KB 栈约束待后续） |
| SEC-06 | ~~NX/W^X/SMEP/SMAP 未启用~~ **已修复（v0.16.0/v0.17.0/v0.18.0）**：SMEP、SMAP、堆 NX+brk 分页均已开；内核态缺页仍 hlt。**v0.33.0 补**：SMAP 在 x86-64 是可选特性（QEMU 默认 qemu64 不支持），原先无条件 stac/clac 会 #UD 死机；现按 CR4 真实状态门控（hepu_smap.h），启动打印 hardware state: SMEP/SMAP。**v0.33.0 收尾再补**：SMEP 与 SMAP 是**两个独立机制** —— SMAP 管数据访问（靠 AC 标志）、SMEP 管取指（靠 CR4 位），STAC 解除不了取指限制。内核在用户页表下执行用户代码（IRELATIVE resolver）时必须同时临时清 SMEP，否则 `--cpu max` 下必然 `#PF error=0x11`（bit4=1 取指违规）。`hepu_smap.h` 已补 `hepu_user_exec_enter/exit()`，按 CR4 真实状态对称处理 | vmm64.c、hepu_smap.h、x86_64.c、user_process.c | 部分（SMEP/SMAP 启用与门控已闭环；内核态缺页仍 hlt） |
| SEC-07 | **部分落地**：用户态异常只终止该进程已实现（lib64.c 判定 stack_frame[20]&3==3 → user_process_fault_current() 改写中断帧经 user_return_trampoline 回内核，user_process_t 增 faulted/fault_vector，用户寄存器转储）。已落地：每进程 fd 表（`syscall64_full.c:227-237`，退出经 `syscall_fd_table_reset` 清空）、只读/只写 fd 限制（`:318` 只读禁写、`:374` 只写禁读）、sys_kill 限自身。残留待做：路径 ≥255 目前截断而非拒绝（`:451`）；ext2 跳过已删除目录项待复核 | lib64.c、user_process.c；syscall64_full.c:227-237,318,374,451 | 部分（异常隔离 + fd 隔离/读写限制已验证；路径长度策略与目录项清理待做） |
| SEC-08 | **已落地（v0.33.0）**：sec_test.elf 自动运行收进 HEPU_BOOT_SELFTEST 开关，默认关（make BOOT_SELFTEST=1 打开）；自检项含 Linux syscall、user_access、aspace、EXT2、[LINUXPIE]、[PIEPROBE]。**v0.33.0 收尾**：新增可判定汇总 `[SELFTEST] battery finished: exceptions total=N expected(hello_static)=M unexpected=K [PASS/FAIL]`（唯一计数源 = `g_user_exception_count`）；QEMU 默认 CPU 与 `--cpu max` **各连续 3 次** 均 `unexpected=0x0` [PASS]；默认构建（不带自检）双 CPU 均 0 异常进 shell；主机端 5 套件 5/5 | kernel_main_64.c、arch/x86_64/Makefile；VERSIONING 0.33.0 | 已关闭 |
| SEC-10 | **已修复（v0.33.0）**：① glibc static-PIE 崩溃 —— 根因是内核进入用户态时**未按 Linux ABI 清零通用寄存器**（`iretq` 只恢复 RIP/CS/RFLAGS/RSP/SS，其余 GPR 带着内核残留值进用户态）。glibc 静态程序 `_start` 把 `rdx` 当 `rtld_fini` 传给 `__libc_start_main`，后者把它注册成退出析构函数，进程退出时跳到该野指针 → 取指 #PF（实测 rip=0x202，恰好等于内核 `rflags` 字面量 0x202）。证据链：崩溃时 `r14=0x500C0108` = 兼容区基址+`__exit_funcs` 偏移 → 内核直读 glibc 退出链，`ror17(stored)^guard` 解码正好得到 0x202；atoi 与其它条目解码为正常代码地址。修复：`user_enter` 汇编按 Linux `ELF_PLAT_INIT` 清零 rax/rbx/rcx/rdx/rsi/rdi/rbp/r8-r15 ② `pie_probe.elf` AT_PHNUM 读到内核地址 —— 根因是 `entry_64.S` 的 `syscall_entry_64` 里 `movq %rsp,%r13` 发生在 `pushq %r13` **之前**，破坏了用户 r13，已删除该指令 ③ 探针读 raw stack 越界 —— 探针自身缺陷，已改为向下读并删除调试 dump ④ **v0.33.0 收尾追加**：`sys_newfstatat` 把用户指针直传 `ext2_64_stat`（漏 `copy_user_path`）⇒ SMAP 下内核态 #PF 整机挂死；`syscall_handler_64` 用 `-1` 兼表「未处理」与 errno ⇒ 合法 errno 被误报 `Unknown syscall`；`sys_arch_prctl` 未知 code 返回笼统 `-1`（改按 Linux 语义 `ENODEV`/`EINVAL`）；**SMEP 拦截内核态调用用户页 IRELATIVE resolver**（`error=0x11`，bit4=1 取指违规）⇒ 按 SMAP 对称方式临时清 `CR4.SMEP`；`syscall64_test()` 自测撞号 + 栈越界读 + 初始化顺序错（`full_init` 必须早于 `test`） | user_switch.S、entry_64.S、user_process.c、user_stack64.c、lib64.c、syscall64_full.c、syscall64_simple.c、hepu_smap.h、kernel_main_64.c、user/apps/pie_probe/ | 已关闭 |
| SEC-11 | **v0.33.0 新开**：**ext2 读块失败被静默吞掉**。`ext2_64_read_file()` / `ext2_64_read_file_offset()` 原来在块读取失败时 `break`，却仍返回完整 `size` ⇒ 调用方以为整份文件读成功，实际拿到「前半段正确、后半段是垃圾」的缓冲区。表现为低频且看起来毫不相关的故障：900KB 的 `glibc_test.elf` 跑到指令中间（rip 落在指令中间 ⇒ 控制流被破坏）、`.so` 模块加载偶发失败 / 符号解析不到（GOT 留 0 ⇒ 跳到地址 0）。**v0.33.0 已修上层语义**（失败如实返回 -1，不再谎报长度）。**v0.33.0 收尾复核**：修后 `hello_static.elf` 一度读不出（`[USER64] exec ERROR: Cannot read file`）—— 该文件在镜像内存在且与原件逐字节一致；主机端用 40KB 跨一级间接块的 `big.bin` 做逐字节比对**完全一致**，`ext2_algo_read_file()` 返回值与期望长度相符 ⇒ **ext2 共享算法本身正确，底层失败点未复现**；v0.33.0 最终回归中 `hello_static.elf` 读取正常（日志 `[USER64] exec: read 4736 bytes` + `Hello from static!`）。另发现 `ext2_algo_read_data_block()` 用 `ctx->block_buf` 兼作「间接块缓冲」与「目标数据块缓冲」，二者同址（读间接块会覆盖目标块），实测因写入顺序恰好自洽而未暴露 —— 属**潜在**缺陷，已登记待加固。**v0.33.1 补记**：补齐 SEC-12 源码后出现过一次同源但形态不同的偶发故障（ext2 读取**挂死**而非报错），详见 SEC-18 | ext2_64.c、kernel/shared/fs/ext2_algo.c | 部分（上层已修；底层未复现，见 SEC-18；**block_buf 同址复用已于第二轮审计 R2-7 修复**，改用独立 ind_buf，见 docs/security/AUDIT_64BIT_R2.md） |
| SEC-14 | **已关闭**：历史扩展 syscall 号与原生号表撞号。旧 `SYS_PUTS=2 / SYS_GETPID=4` 与原生号表 read=2/close=4 同号两套语义；撞号 `case` 分支已删除，仅保留原生表未占用的 `SYS_HELLO=0`/`SYS_PUTCHAR=1`（`syscall64_simple.c:10-15` 注释、`:91`/`:95` 分支）；自测改用原生 `getpid=20` 并修正初始化顺序（`full_init` 早于 `test`）。源码核实：文件内已无 `SYS_PUTS`/`SYS_GETPID=4` 撞号分支 | syscall64_simple.c:10-15,91-95；kernel_main_64.c；提交 3375fbe | 已关闭 |
| SEC-12 | **已关闭（v0.33.1）**：**启动自检依赖无源码的历史二进制产物**。`hello_linux.elf`、`hello_static.elf`、`hello_c2.elf`、`glibc_test.elf`、`dynhello.elf`、`dynhello2.elf`、`hello2/3/4.elf`、`libtest.so`、`libtest2.so` 只有 `user/build/` 里的编译产物，源码不在仓库中，也没有 Makefile 规则 ⇒ 镜像无法从源码完整重建（交付准则不满足）。**v0.33.1 已关闭**：通过反汇编 + 符号表 + 寄存器转储逐条还原这 12 个程序的语义（含 `test_add`=a+b、`my_syscall3`=裸 syscall 封装、`get_value`/`double_value`/`call_func` 走 `test_value` 指针与 `func_table` 间接表），源码补到 `user/apps/legacy/`（含共享头 `common_linux_abi.h`），并纳入 `user/Makefile` 的 `legacy` 目标与 `all`；`tools/make_single_img64.sh` 统一从 `user/build/legacy/` 写进镜像；旧的无源码产物已从工作区删除 ⇒ 镜像**完全**可从源码重建。补源码过程中牵出三个真实内核缺陷（SEC-15/16/17）与一处**错误的历史结论** | user/apps/legacy/、user/Makefile、tools/make_single_img64.sh | 已关闭 |
| SEC-15 | **已关闭（判据层）**：历史缺陷是内核对「是否动态链接」没有客观判据，靠「路径里含 `dyn` 三字符」猜 ELF 性质（`path[i]=='d' && path[i+1]=='y' && path[i+2]=='n'`），误走 V0.20.80 遗留 `hepu_dynlink_relocate()`，对 `/bin/dynhello.elf` 算出 load bias=0x1000（既不在兼容区也不在用户窗口），DT_NEEDED 读出乱码、在未初始化的 GOT/PLT 上跑飞 `#GP vector=0xD` 并打断自检电池；名字不含 dyn 的真动态程序则被漏判。**修复**：新增 `elf64_has_interp()`，判据取自 ELF 自身的 PT_INTERP 段（`elf64.c:75-85`、`elf64_find_interp:148-155`），删除文件名启发式及其调用。**口径变更（v0.54.6 P1-7，提交 3375fbe）**：当前源码**不再拒绝** PT_INTERP 程序 —— 内核把主程序与解释器 ld.so 各自按偏置装入用户地址空间，并填好 AT_BASE/AT_ENTRY/AT_PHDR（`user_process.c:1931-2128`）；DT_NEEDED、符号查找、重定位、TLS 初始化全部由用户态 ld.so 完成。v0.33.1 时期「带 PT_INTERP 即明确拒绝、只加载静态/静态 PIE」的描述已被该实现取代，台账各处不得再沿用「明确拒绝」说法 | elf64.c:61-85,147-155；user_process.c:1938-2128；提交 3375fbe | 已关闭 |
| SEC-16 | **v0.33.1 新开**：**`open()` 会占用 fd 0/1/2**。Linux 里 stdin/stdout/stderr 是保留槽位，进程启动时已打开，`open()` 永远分配不到这三个号；而 `sys_open()` 原来从 0 开始找空槽，于是进程的**第一次** `open()` 就拿到 fd=0。同时 `sys_read()` 把 `fd==0` 一律当 stdin 处理（阻塞等键盘，`while (!keyboard64_haschar()) hlt;`）—— 于是「打开一个文件再读它」这种最普通的用法会**永久卡死**。现场：`hello4.elf`（open `/bin/hello3.elf` 再 read 64 字节）打开成功拿到 fd=0，紧接着 `read(0,...)` 走进 stdin 分支 → 整机停在该进程上，启动自检电池直接断在那里（表现为「日志停在 [LINUX4] Running」）。**v0.33.1 已修**：`sys_open()` 改为从 fd=3 开始分配，与 Linux 语义一致 | syscall64_full.c | 已关闭 |
| SEC-17 | **v0.33.1 新开**：**启动自检电池对「预期异常」的归因是错的**。`kernel_main_64.c` 原来把 `hello_static.elf` 注释为「**故意**跳到坏地址的异常测试」，并把它产生的 1 次用户态异常计入 `expected`。反汇编证明这是**误判** —— `hello_static.elf` 的内容只有 `write(1,msg,25)` + `exit(42)`，**根本没有跳坏地址**；它当时之所以炸，是因为缺 `.note.ABI-tag` 被判成 Hepu 原生 ABI ⇒ Linux `exit(60)` 在原生号表里查不到（原生 `exit` 是 6）⇒ 进程不肯退出 ⇒ 执行流落到后面的字符串数据 ⇒ `"He"` 被解成 `rex.W gs insb` ⇒ CPL=3 执行 `insb` ⇒ **`#GP(0)`**（`vector=0xD error=0x0 rip=0x4000102E`，rip 恰好是字符串首地址 —— 这正是当初误判为「故意跳坏地址」的由来）。后果有两层：① 电池的「预期异常」数字与真实故障混在一起，掩盖了判别力；② SEC-07「用户态异常只终止该进程」的验证**建立在一个假故障上** —— 补上 ABI note 后 `hello_static` 干净退出，异常消失，SEC-07 的覆盖就归零了。**v0.33.1 已修**：补 ABI note 后 `hello_static` 正常 `exit(42)`；新增真正的 `crash_jump.elf`（显式写未映射地址 `0xDEAD0000` ⇒ `#PF`）承担 SEC-07 覆盖；电池判据改为 `expected(crash_jump)=N`，现双 CPU 各 3 次均 `exceptions total=0x1 expected(crash_jump)=0x1 unexpected=0x0 [PASS]` | kernel_main_64.c、user/apps/legacy/crash_jump.c、user/apps/legacy/hello_static.c | 已关闭 |
| SEC-18 | **v0.33.1 新开（未复现，登记备查）**：**ext2 文件读取路径偶发挂死**。补齐 SEC-12 源码后的第一次启动回归中，`hello4.elf`（`open("/bin/hello3.elf")` → `read(fd,buf,64)` → `close`）在切到用户进程后**日志完全停住**，整机不再有任何输出（无 EXCEPTION、无返回），表现为「启动自检电池断在 `[LINUX4] Running`」。**排除项**：① 用 `debugfs` 从镜像分区抽出 `/bin/hello3.elf` 与源文件 `cmp` **逐字节一致**（13400 字节），文件本身无损坏；② `ext2_64_read_file()` 的 `size` 被 `max_size` 夹到 4096 ⇒ 只读 1 个数据块，**不涉及间接块**；③ `ext2_find_in_dir()` 的目录遍历有 `rec_len == 0` 保护，**不会死循环**；④ 同一镜像加开 `HEPU_TRACE_LINUX_SYSCALL=1` 重跑，**一次通过**（`read+close ok`）；⑤ 随后双 CPU 各 3 次自检电池 + 各 3 次交付构建回归（共 12 次启动）**全部通过，无一次复现**。⇒ 判定为**低频偶发**，与 SEC-11 描述的「ext2 大文件/目录跨块读取偶发失败」同源，但**失败形态不同**（本次是挂死而非报错）。已加目录遍历 guard 上限（`ext2_64.c:332-333` `guard++ < 256`，另 `:338-339`/`:562-563`/`:938-943` 有 rec_len 越界/下溢保护），排除死循环与越界遍历；但**挂死的具体失败点仍未定位、未复现**，继续备查 | ext2_64.c:332-339,562-563,938-943；kernel/shared/fs/ext2_algo.c | 部分（遍历 guard 已加；偶发挂死根因未复现，遗留待复核） |
| SEC-19 | **v0.57.2 新开**：**用户栈第二页 `#PF`（`ps` 触发）**。`SYS_HEPU_PS`(227) 返回后，`ps.elf` 第一次跨到栈的第二页（`0x6FFFEFE8`，`rsp=0x6FFFF008`）即 `#PF error=0x15 rip=0x0 CR2=0x0`，现场 `PML4e=0x25DD027`、`PDPTe=0x25C2023`、`PDe=0xE3`（PD 项物理地址部分为 0，指向物理 0 的 PT）。排查已排除：① 不是栈太小 —— `user_process.c` 的 `for i<256` 确实映射 1 MB 栈；② 不是初始 RSP 贴顶（已加 512 B head-room 并验证 rsp 落到 `0x6FFFF0xx`）；③ 不是 `vmm64_get_or_create_table` 分配失败（失败会打印 `[VMM64] ERROR`，日志无）；④ 不是进程名终止符（内核已补 `name[31]=0`）。**未定位**：PD 项为何被写成物理 0 —— 怀疑与 1 MB 栈的 256 次 `vmm64_map_page` 在同一 PD 项下反复创建/拆分 PT 有关（`vmm64_split_huge_pde` 会吃掉页表页）。影响面：任何栈用量 > 4 KB 的用户程序都会崩，故 `ps` 目前不进 autorun。 | `user/apps/ps.c`、`syscall64_full.c sys_ps`、`user_process.c`、`vmm64.c:197-265` | 待解决 |
| SEC-13 | **v0.33.0 新开**：**用户态 ABI 约定的三处违背**（均由 v0.33.0 定位并修复，登记备查）：① 入口 GPR 未清零（同 SEC-10）② **用户栈 16 字节对齐**：内核给的初始 RSP 曾为 ≡0(mod16)，而 gcc 生成的 `_start` prologue 固定减少 0x38 字节，随后 `movaps %xmm0,(%rsp)` 要求 RSP≡0 ⇒ 实际落在 ≡8 ⇒ `#GP`（error code bit0=0 = 对齐检查失败）。按 SysV 约定改为 **RSP ≡ 8 (mod 16)** ③ **CR4.OSFXSR/OSXMMEXCPT 从未置位**：用户态执行任何 SSE 指令直接 `#GP`；x86-64 架构要求 SSE2，glibc/gcc 默认生成 SSE 指令 ⇒ 这不是可选加速而是用户态 ABI 硬要求（Linux `start_thread` 同样置位）。现三项均已修，启动打印 `hardware state: SMEP=.. SMAP=.. SSE=..` | user_switch.S、user_stack64.c、x86_64.c | 已关闭 |
| SEC-09 | ~~进程正常退出后记录仍 active，槽位不回收~~ **已修复（0.14.0）**：user_return_cleanup 正常退出也 release 槽位（active=0、释放内核栈、清 fd 表）；QEMU 连续 10 次 exec 无「No free process」、空闲页 delta=0 | user_process.c:user_return_cleanup | 已关闭 |

## v0.33.2–v0.33.5 安全修复记录

| 编号 | 问题 | 修复 | 状态 |
|------|------|------|------|
| SEC-14 | syscall64_simple.c 旧扩展号 SYS_PUTS=2/SYS_GETPID=4 与原生号表 read=2/close=4 撞号 | 删除撞号 case，保留 SYS_HELLO=0/SYS_PUTCHAR=1（`syscall64_simple.c:10-15,91-95`，提交 3375fbe） | 已关闭 |
| — | hepu_apply_irelative 在 ring0 call *resolver，r_addend 来自 ELF，可内核态执行任意代码 | 删除 call，GOT 直接写 r_addend+load_bias（`user_process.c`） | 已关闭（随 3375fbe 快照入仓） |
| — | sys_futex 直接解引用 uaddr 未校验 | 加 `verify_user_read`（`user_access64.c:231`；调用 `user_process.c:1694-1695`） | 已关闭（随 3375fbe 快照入仓） |
| — | hepu_user_ptr 不查 U/S 位和用户窗口 | 加 [0x40000000,0x80000000) 检查 + 每级 PTE 查 U/S（`user_access64.c`） | 已关闭（随 3375fbe 快照入仓） |
| — | hepu_elf_vaddr_to_fileoff 无边界检查 | 加 elf_size 参数，phnum/offset/filesz 校验（`elf64.c`） | 已关闭（随 3375fbe 快照入仓） |
| — | hepu_user_ptr 写用户页未查 NX(W^X) | 新增 `hepu_user_write_ptr`（`user_process.c:431`，GOT 写 `:596`），写用户页走它 | 已关闭（随 3375fbe 快照入仓） |
| — | syscall64_compat.c 曾是未接入的兼容层 | 已从构建与源码移除（死代码；64 位树内无该文件） | 已关闭（3375fbe） |
| BLD-15 | 构建/启动脚本硬编码绝对路径。源码核实（HEAD ef3c7cc）仍有 **4** 个脚本未相对定位：`tools/build_hybrid_iso.py:12`、`tools/build_iso.py:10`、`tools/build_uefi_dual.py:15` 写死 `/home/user/Doubao/chats/38448056008342018/...`；`tools/start_qemu.sh:3` 写死 `/home/user/.super_doubao/.../workspace/hepu-os`。旧条目称「v0.33.5 已改 `$(dirname $0)` 相对定位」与源码不符，予以更正 | 待改为 `$(dirname $0)`/脚本自定位 | 待解决（未修复） |

## v0.34.0+ 开发记录

| 版本 | 内容 | 状态 |
|------|------|------|
| v0.34.0 | fork调度修复（schedule()跳过idle进程） | ✅ |
| v0.35.x | 恢复图形桌面 + hepu_nographics标志 | ✅ |
| v0.36.x | tgkill + shell kill + ps PPID/MEM/PRIO + hepu_panic栈回溯 | ✅ |
| v0.37.x | kill统一 + sigaction桩 + install命令 | ✅ |
| v0.38.0 | install命令（ext2文件复制） | ✅ |
| v0.38.4 | SEC-18 ext2 目录遍历 guard 上限（`ext2_64.c:332-333`）；偶发挂死根因未复现，条目未全闭环 | 🟡 部分 |
| v0.39.0 | 页错误处理框架（CR2/PTE转储） | ✅ |
| v0.40.0 | SEC-02 COW：缺页复制框架 `vmm64_try_cow`（`vmm64.c:432-457`，引用计数+清 W 位+PF 复制页）；后续 `uaspace_fork_copy` 让 **fork** 接入 COW，**clone 仍未接入** | 🟡 部分 |
| v0.40.2 | signal框架（sigaction存handler） | ✅ |
| v0.41.0 | 抢占式调度（PIT IRQ0触发schedule） | ✅ |
| v0.42.0 | 信号投递（用户栈改写 + pending_signals位图） | ✅ |
| v0.42.1 | pkg list命令 | ✅ |
| v0.43.0 | QoS优先级调度 + nice命令 | ✅ |
| v0.43.1 | 隔离域列表 + process64_count_by_env | ✅ |
| v0.44.1 | wm list命令 | ✅ |
| v0.45.0 | hepua AI命令（v1.0+启用） | ✅ |
| v0.54.6 | P1-7 动态链接：内核按 PT_INTERP 加载 ld-linux 并摆 auxv（`user_process.c:1931-2128`），DT_NEEDED 由用户态 ld.so 解析；`hello_dyn` 输出 Hello, World!（提交 3375fbe） | ✅ |

## P1-7 动态链接修复记录（2026-11）

| 编号 | 问题 | 修复 | 状态 |
|------|------|------|------|
| DYN-01 | **syscall 入口丢弃第 6 个参数**。`entry_64.S` 只把用户 rdi/rsi/rdx/r10/r8（arg1..5）交给 C 处理函数，Linux 的 arg6（r9）从未传出。mmap 的第 6 参正是文件偏移 ⇒ ld.so 的 `MAP_FIXED` 分段映射全部按 offset=0 读文件，libc 各段内容错位，最终在 ld.so 内部 `#PF`（rip=0x60015E11） | 入口把栈槽里的用户 r9 作为第 7 个参数压栈传给 `syscall_handler_64`（`entry_64.S:104` 保存 r9、`:131-138` arg6 栈槽传参），dispatch/表调用把 arg6 继续下传（seccomp args[5] 同步）；提交 3375fbe | 已关闭 |
| DYN-02 | **fstat 的 st_dev/st_ino 恒为 0**。glibc 的 `_dl_map_object_from_fd` 用这对值判断「文件是否已加载」；全为 0 时 libc.so.6 被误判成主程序已加载对象，于是版本检查对主程序做（`no version information available`），符号查找报 `undefined symbol: __libc_start_main, version GLIBC_2.34`，进程 exit 127 | `ext2_64_stat_ino()` 返回 ext2 inode 号；`sys_fstat`/`sys_newfstatat` 填 st_dev=1、st_ino=真实 inode（`syscall64_full.c:1484,1489-1490`、`:2172,2179-2180`）；提交 3375fbe | 已关闭 |
| DYN-03 | Linux 兼容层若干桩实现阻塞 ld.so：`openat` 固定返回 fd 3、`fstat` 恒返回 0、`read` 只读首 4KB、缺少 `pread64` | `openat` 转真 `sys_open`；`fstat` 填完整 Linux struct stat；`read` 按 fd 偏移读；新增 `pread64`（`syscall64_full.c:401`，`SYS_LNX64_PREAD=136` 分发 `:2646`）；文件偏移 `mmap`（含 MAP_FIXED）与 `USER_MMAP_BASE=0x64000000` 避开程序/解释器区；提交 3375fbe | 已关闭 |

## 路线图完成状态

| 优先级 | 内容 | 状态 |
|--------|------|------|
| P0 安全 | SEC-02 COW（部分：缺页框架已有，fork 已接入，clone 返回 ENOSYS）/ SEC-04 裸盘（PID==0 临时闸已加，capability 模型待落地）/ SEC-05 动态缓冲（部分：主路径已堆分配，readdir 静态表残留）/ SEC-06 panic栈（部分：SMEP/SMAP 启用与门控已闭环，内核态缺页仍 hlt）/ SEC-07 fd隔离（部分：异常隔离+每进程 fd 表+读写限制已验证；路径长度策略待做）/ SEC-14 撞号（已关闭）/ SEC-18 ext2挂死（部分：遍历 guard 已加，根因未复现） | 🟡 未全部完成（仅 SEC-14 关闭；其余为部分/待解决，详见主表） |
| P1 进程 | fork/waitpid/kill/tgkill/ps/sigaction/抢占调度/信号投递已落地；COW 写时复制见 SEC-02（部分） | 🟡 基本完成（COW 未闭环） |
| P2 文件 | 基础工具/install/pkg list | ✅ 基本完成 |
| P3 隔离域 | 优先级调度/nice/domains | ✅ 基本完成 |
| P4 图形生态 | VBE桌面/gui/wm list/hepua | ✅ 基础完成 |

**v1.0+ 目标**：glibc兼容层、gcc移植、shell脚本解释器、Vulkan后端、窗口拖动完善。

## R1–R4 审计修复落地清单（已关闭，含证据与提交）

> 口径：一条「已关闭」同时具备 (a) 源码对应修复代码 file:line、(b) 提交 hash。R1/R2/R3/R4 均在 HEAD `ef3c7cc` 之前合入；64 位树早期修复随初始快照 `3375fbe` 进入本仓，无独立分拆 hash。

### R4 · 组合 A 安全止血包（提交 `ef3c7cc`，6 P0 + 12 P1）

修复详情与回退方式见 `docs/security/FIX_SECURITY_BUNDLE_A_R4.md`（该文档结论不改写）。

| 编号 | 缺陷 | 证据 file:line | 提交 | 状态 |
|------|------|---------------|------|------|
| INT-01 | LAPIC 定时器校准到真实 1kHz（弃用硬编码 ICR=6250，改用 PIT channel-0 1ms tick 测 50ms 窗口；校准期屏蔽 LVT） | `apic64.c:507`（CAL_WINDOW_MS）、`:511`（g_apic_timer_hz）、`:520`（apic64_calibrate_timer）、`:570`（icr=hz/1000） | ef3c7cc | 已关闭 |
| SC-01 | writev 任意用户指针/无界长度：iovcnt 上限 1024、总字节封顶 1MB，iov 与数据分批走 copy_from_user | `syscall64_full.c:1210-1213`（常量）、`:1214`（sys_writev） | ef3c7cc | 已关闭 |
| SC-02 | clone child_tid 裸写 / TLS 裸写：写前 hepu_is_user_address 校验，child_tid 改 copy_to_user | `syscall64_full.c:915`（sys_clone）、`:938-942`（child_tid）、`:946-950`（tls） | ef3c7cc | 已关闭 |
| SC-03 | reboot 无权限闸：入口 PID==0 临时闸 | `syscall64_full.c:2613-2614` | ef3c7cc | 已关闭（临时闸，见文末 PID 口径注记） |
| SC-04 | mkfs 仅靠源码内魔数鉴权：魔数之外加 PID==0 临时闸 | `syscall64_full.c:484-485`（闸）、`:486`（魔数保留） | ef3c7cc | 已关闭（临时闸，见文末 PID 口径注记） |
| SC-05 | sigaction 把 act 指针当 handler 存储：新增 k_sigaction 按 glibc x86_64 布局 copy_from_user，解析真实 sa_handler | `syscall64_full.c:1347-1377` | ef3c7cc | 已关闭 |
| MM-01 | COW 缺页丢失 NX 位：新 PTE 保留 bit63 NX | `pmm64.c:116` | ef3c7cc | 已关闭 |
| MM-02 | 用户栈字符串区越界：strbuf[512] 填充前按 strings_len 上限校验 | `user_stack64.c:224`（strbuf）、`:227-230`（上限判断） | ef3c7cc | 已关闭 |
| NET-01 | TCP 发送成功判据恒假、序列号不推进：判据改 `sret > 0` | `net64_tcp.c:159` | ef3c7cc | 已关闭（真机/带网络后端环境待复核，见 R4 文档第五节） |
| NET-02 | 纯 ACK 静默丢弃：新增 tcp_process_ack helper（RTT 平滑/清未确认/cwnd），data 与纯 ACK 分支均调用 | `net64_tcp.c:275`（helper）、`:370`/`:374`（调用点） | ef3c7cc | 已关闭（真机流量待复核） |
| NET-05 | 接收缓冲填满后新数据静默丢弃：取尽复位游标，部分取走则压缩剩余数据到缓冲头 | `net64_tcp.c:241-250` | ef3c7cc | 已关闭（长连接待复核） |
| FS-03 | 块设备部分写失败后脏读：raw write 失败先 blk_cache_invalidate 再返回 -1 | `blk_cache64.c:115`（前向声明）、`:127`（失效调用） | ef3c7cc | 已关闭 |
| FS-04 | free_inode inode 表块 32 位截断：先 64 位算 itb64、校验后再截断 | `ext2_algo.c:625-626`、`:647-648` | ef3c7cc | 已关闭 |
| FS-05 | 写文件末尾偏移 32 位溢出：end_offset 改 64 位计算 | `ext2_64.c:1319`（end_offset64） | ef3c7cc | 已关闭 |
| BLD-14 | CSPRNG 熵源 extern 宽度/元素数不匹配：extern 改 uint64_t irq_count[256]，256 项 uint64 累加 | `csprng.c:64-69` | ef3c7cc | 已关闭 |
| GFX-01 | Tab 补全候选列表栈溢出：out[80] 改逐字符有界拼接，将满安全截断并强制 NUL | `hs_apps.c:461`（t_complete）、`:495`/`:504`/`:507`（有界写） | ef3c7cc | 已关闭 |
| DRV-02 | 内核模块 PLT 静态索引跨模块累计：删 static 改模块级局部 | `kmod64.c:287`（plt_slot_idx 局部） | ef3c7cc | 已关闭 |
| WM-02 | 鼠标/平板坐标按硬编码 1024 钳制：改取 gfx64_get_width/height 真实尺寸 | `usb64.c:1357-1362`（scr_w/scr_h/max_x/max_y） | ef3c7cc | 已关闭 |

### R1 · 首轮内核安全审计修复（提交 `fec3c87`）

| 编号 | 缺陷 | 证据 | 提交 | 状态 |
|------|------|------|------|------|
| R1-1 | auth64 SHA-256 单缓冲强制 len≤55，修复超长密码写穿 msg[128] 栈缓冲 | `auth64.c` | fec3c87 | 已关闭 |
| R1-2 | net64_ip total_len 自报值钳制到实际帧长，防截断包越界读内核内存 | `net64_ip.c` | fec3c87 | 已关闭 |
| R1-3 | sys_pipe/wrap_net_stats 直写用户指针改 copy_to_user | `syscall64_full.c` | fec3c87 | 已关闭 |
| R1-4 | ext2_64(legacy) 目录项遍历校验 offset+8/rec_len 不跨块/rec_len≥actual/GDT 组号<32 | `ext2_64.c` | fec3c87 | 已关闭 |

### R2 · 第二轮审计（提交见下；报告 `docs/security/AUDIT_64BIT_R2.md`）

| 编号 | 缺陷 | 证据 | 提交 | 状态 |
|------|------|------|------|------|
| R2-1 | DNS 压缩指针无限循环（DoS）：指针须包内且严格前移，跳转计数上限 64 | `net64_dns.c` | b64a2df | 已关闭 |
| R2-2 | CSPRNG 种子被 reseed 抹成固定值 + getrandom 假随机：删自毁 reseed，getrandom 接 csprng_get_bytes | `csprng.c`、`syscall64_full.c`、`kernel_main_64.c` | 9ba7098 | 已关闭 |
| R2-3 | pmm64 MAX_MEMORY_SIZE 整数溢出：强制 uint64 运算 | `pmm64.c` | e16dd10 | 已关闭 |
| R2-4 | getdents 直写用户指针：内核栈构造条目后 copy_to_user | `syscall64_full.c` | f0ed982 | 已关闭 |
| R2-5 | DHCP 内联选项遍历缺边界检查 | `net64_dhcp.c` | 9a41ea0 | 已关闭 |
| R2-6 | TCP data_offset 缺合法性校验 | `net64_tcp.c` | 363c0ba | 已关闭 |
| R2-7 | ext2 间接块与目标数据同址复用（SEC-11 底层）：独立 ind_buf | `ext2_algo.c` | ea9cdd8 | 已关闭 |
| R2-8 | 构建链 MBR 无自动构建：缺失自动构建，失败即中止 | `tools/make_single_img64.sh` | 77d57c8 | 已关闭 |

### R3 · 桌面体验与稳定性（提交 `4fbafb5`；报告 `docs/security/AUDIT_DESKTOP_STABILITY_R3.md`）

| 编号 | 缺陷 | 证据 | 提交 | 状态 |
|------|------|------|------|------|
| R3-1 | HepuShell 窗口槽位泄漏：id==-1 空闲槽 + 统一回收 hs_window_recycle | `hepushell.c`；主机测试 `tools/host-tests/winslot_test.c` | 4fbafb5 | 已关闭 |
| R3-2 | 纯 APIC 模式 PIT 中断冻结（MADT ISA IRQ0→GSI2 覆盖）：acpi_irq_to_gsi 翻译真实 GSI | `acpi.c`/`acpi.h`、`apic64.c`、`kernel_main_64.c` | 4fbafb5 | 已关闭 |

## PID 权限闸口径注记（避免误判为「闸失效」）

- **用户进程 PID 恒 ≥100**：`user_process.c:289`/`:629` `proc->pid = idx + 100`；clone 子线程为 `200 + idx`（`syscall64_full.c:930`、`:940`、`:952`）。不存在 PID=0 的真实用户进程。
- 因此 reboot/mkfs/disk 路径上的 **PID==0 临时闸对所有真实用户进程一律拒绝**；这与既有 `sys_disk_read`/`sys_disk_write` 的 `pid != 0` 闸（`syscall64_full.c:2572`、`:2587`）是**同一口径的有意策略**，不是闸没生效。
- 用户态 install64（调 disk 26/27、mkfs 31、reboot 28）与 mbrtool 的 reboot 路径，本就被既有 disk 闸阻断，且 OPEN-04「安装器不拷贝系统文件」仍标待解决，属**已知不完整遗留路径**。
- 长期授权安装流需随未来 uid/capability 能力模型一并解决；本轮临时闸不搭 cred/capability 大体系。

## 已复核关闭条目（实测定论）

### OPEN-03 · QEMU sendkey 是否被键盘驱动读取 —— 已关闭（非真 bug，旧测量误解）

- **结论**：sendkey 链路端到端可用，PS/2 与 USB HID 两条键盘路径均被键盘驱动正常接收与消费，无需先点击聚焦。旧问题属测量误解，**无修复动作**。
- **实测环境**：本轮 ef3c7cc 内核；PS/2 配置（未挂 USB）；USB HID 键盘对照（`-device piix3-usb-uhci -device usb-kbd`）。
- **四重证据**：
  1. **中断级**：`g_kbd_irq_count`（符号地址 0x1f29f4）每次按键 +2（按下 + 释放各 1 次）；键盘环形缓冲 head/tail 同步前移且条目被消费。执行 `sendkey 2` 后计数 11→13、缓冲指针 3→4。
  2. **界面级（PS/2）**：`sendkey 2` 使 OOBE 由 1/4 页推进到 2/4 页（截屏像素差异 73616 px）；随后在账户创建页输入 h/e/p/u，用户名输入框真实回显 "hepu"。
  3. **USB HID 对照**：sendkey 改走 USB 队列（此时 PS/2 IRQ1 静默），同样推进向导、回显 hepu、Tab 切换输入框焦点（像素差异 3008 px）。
  4. **边界确认**：两条链路均端到端可用，不存在「驱动收不到键」。
- **应用层边界（避免后续误判）**：OOBE 语言页只绑定数字键 `1`/`2`，Tab/方向键/回车在该页未绑定，属应用设计而非驱动缺陷。此前「按了没反应」的两个成因：(a) 语言页按键绑定有限；(b) 缺少中断级取证，把「界面没变化」误判为「驱动没收到键」。

### R3-CURSOR · 渲染光标坐标 `g_shell.mouse_x/y` 与全局 `mouse_x/y` 一致性 —— 测量误解（关闭，非真 bug，无需修复）

- **结论**：Shell 选定坐标源后逐帧直拷，渲染侧无二次偏移、无错位；两套坐标源的差异是相对/绝对语义与设备路径不同造成的测量误解。**修复动作：无**。
- **符号与偏移**（nm + 链接后 ELF 反汇编互证）：`g_shell` = 0x23ced60；`mouse_x` 结构内偏移 0x5148（即 0x23d3ea8）、`mouse_y` 偏移 0x514c（即 0x23d3eac）；`hepushell_update_mouse` 对这两个字段为直拷赋值。
- **PS/2 路径证据**：7 组移动/钳制采样点，`g_shell` 内坐标与 `ps2mouse64.c` 文件内静态 `mouse_x/y` **逐点相等**（512,384 → 562,384 → 612,384；边缘钳制 1020,767），增量、方向、边界全部一致。
- **tablet 路径证据**：`g_shell` 跟随 USB tablet 的绝对坐标（结构 `abs_x` 偏移 0x30、`abs_y` 偏移 0x34、结构大小 0x48、基址 0x30b1e0），基线为 (0,0)；而 HMP `mouse_move` 只向 PS/2 发相对 delta，改不到绝对指针设备，`mouse_set 0/1/2` 亦无效，7.2.0 无绝对定位命令 —— 于是 tablet 光标停在初始 (0,0)（左上角出屏），被误读成「光标不动/坐标错」。
- **误解机制**：把两套独立坐标源（Shell 结构内坐标 vs PS/2 驱动文件内静态变量）直接对比，并混用相对/绝对两套语义；实际渲染无错位。
- **可选加固建议（非缺陷，本轮未实施）**：tablet 在首个报告前把坐标默认到屏幕中心而非 (0,0)（`usb64.c:223` 字段定义与 `:1787` `usb64_tablet_get` 路径，约 ≤5 行）。该项不计入缺陷台账，仅作后续体验优化记录。

## SEC-20 用户线程不是调度单元（clone 无法真正并发）

- 现象：`sys_clone` 能创建线程（`[CLONE] created thread pid=0x4`），但子线程
  从不运行；父线程被 `context_switch` 抽走后回来 `rip=0 / CR2=0`。
- 根因：`process64_tick()` 只轮转内核线程；`user_process_switch` 的用户执行流
  不在调度表里，无上下文保存槽位；`smp_timer_tick` 的 AP 调度被 `&& 0` 关闭。
- 影响：所有依赖用户态并发的功能（线程、并行、异步 IO 回调）。
- 现状：`sys_clone` 保留实现但 `g_clone_threads_enabled=0`，对外返回 ENOSYS。
- V0.57.4 进展：用户线程调度单元（`uthread64.c`）已把"线程不在调度表"补上，
  切换逻辑成立（`g_ut_cur` 交替、帧注入 `rax/rip/rsp` 均正确），但子线程被切到后
  现场不推进，下一轮继续定位中断返回路径。
- 修法方向：为每条用户执行流建立 rip/rsp/rflags/GPR/fsbase 上下文，由中断
  调度器轮转并保证恢复；fork 的显式恢复路径可作为模板。
- **V0.57.6 已修复（功能层）**：`uthread64.c` 的 `UT_FRAME_GPR_BASE 2` 修正了
  IRQ 帧偏移（`IRQ_ENTRY` 把 `ds/es` 压在最顶，rax 在 `f[16]` 而非 `f[14]`，
  此前写 rax 其实写进了 rbx）。同时给每个线程配独立 syscall 内核栈。
  实测串口出现 `PASS: child ran in shared address space, both counters advanced`，
  共享地址空间、独立用户栈、子 rax=0、交替推进计数器四项成立。
- 剩余：进程收尾阶段的 #PF 未收敛，见 SEC-21；两次运行有一次 FAIL 的波动同源。

## SEC-21 用户线程进程收尾阶段 rip=0 的 #PF

- 现象：clone 测试父线程打印 `PASS` 后，紧接着
  `[EXCEPTION] vector=0xE error=0x15 rip=0x0 / CR2=0x0`，
  用户寄存器 `rsp=0x6FFFF008`（正好是 `USER_INIT_RSP + 8`），
  随后 `[CLONE] aspace kept: live clone threads=0x1`、进程因异常终止。
- error=0x15 含义：保护违例 + 用户态 + 取指（P=1,W=0,U=1,I/D=1），即从地址 0 取指。
- 影响：不影响系统整体（异常被用户态 fault 处理器捕获，进程终止后内核继续，
  桌面正常加载），但会污染测试结论，且导致偶发 FAIL。
- 已排除：栈太小（256 页）、初始 RSP 贴顶（已留一页 + 512B head-room）、
  `vmm64_map_page` 分配失败（无 `[VMM64] ERROR`）。
- 可疑点：`create_user_page_table_ex` 的 256 页循环未检查 `vmm64_map_page` 返回值；
  syscall 返回路径的 `g_syscall_ret_rip/rflags/ustack` 仍是全局单值。
- 关联：与 SEC-19（用户栈第二页 #PF / `PDe=0xE3`）高度同型，怀疑同一根因。
- **V0.57.7 已修复**。两个独立根因：
  (1) `uthread_clear_owner` 在 `sys_exit` 路径里释放了**当前正在使用**的内核栈页，
      返回路径把栈上的 rcx（sysretq 的 RIP）踩成 0；
  (2) crt0 用 32 位 `int $0x80` 退出，在被 clone 切换过的上下文里返回不可靠。
  修法：内核栈改为槽位复用、不随退出释放；clone_test 改用 64 位 `exit()`。
  三次独立启动实测 `PASS=1 FAIL=0 EXCEPTION=0`。

## SEC-22 crt0 的 32 位 `int $0x80` 退出路径

- 现象：`user/libhepu/crt0.S` 的 `_start` 结尾用 `pushl/call main/movl/int $0x80`
  （32 位约定），在被用户线程切换过的上下文里，`main` 返回后会落到地址 0
  取指（`error=0x15 rip=0x0`）。
- 影响：所有 `return` 而非显式 `exit()` 的程序都可能命中；单线程程序目前未复现。
- 现状：`clone_test` 已改为显式 `exit(0)` 规避；crt0 本身未改。
- 修法方向：crt0 改成 64 位入口（`call main` + `mov $5,%eax; syscall`），
  需要全量回归所有用户程序，属独立改动。

## SEC-23 clone 测试偶发 `child never ran` —— 已修复（V0.58.2）

**根因**：`uthread_create()` 为了让子线程"下一次 tick 必定被切到"，把时间片计数
直接顶到阈值（`g_ut_slice = 10`），但没有同时记录这个计数属于哪个 owner。
`uthread_timer_tick()` 开头的 `if (g_ut_slice_owner != owner) { g_ut_slice = 0; }`
会把它当场复位成 0，于是"立即切换"的意图被悄悄取消，退化成要等满 10 个 tick。
父线程在这 10ms 内跑完计数进入等待，子线程就整轮轮不到 —— 表现为
`FAIL: child never ran`，且 `EXCEPTION=0`、`[CLONE] aspace kept: live clone threads=0x1`
（线程创建成功了，但一直没被调度）。

**修法**：create 时把 `g_ut_slice_owner` 一并写成当前 owner，让"顶满时间片"真正生效。
保留 `[UT] exit ticks2=<n> switches=<m>` 退出取证日志：前者是同 owner 存在多线程时
的 tick 次数，后者是实际切换次数，二者都为 0 才说明调度没跑起来。

**验证**：修复前同一镜像 6 次启动出现 2 次失败；修复后连续 20 次启动 **20/20 PASS**，
每次 `switches` 稳定在 0x21~0x25（33~37 次切换），子线程确实被反复调度过。

**关联问题（已修）**：`sys_sleep` 原本是内核态 `nop` 忙等，睡眠期间用户态 tick
不触发，"sleep 兜底"等于没有。已把 `ulib.h` 的 `sleep()` 改为用户态忙等（见
FIXES §1.91），睡眠期现在能正常轮转。真正的阻塞式 sleep（`wake_tick` +
tick 跳过未到期线程）仍未实现，属独立改造。

## SEC-24 桌面主循环长时间停止推进（已修复，V0.58.3）

- 现象：`desktop64_run()` 的 `while(1)` 只推进十余轮就不再前进，画面冻结，
  时钟与侧栏 Uptime 一起停住；放在主循环里创建的新窗口永远等不到重绘。
- **原判断有误**：一度记为"主循环在 `hlt` 后停止推进"并怀疑 cli/EOI 遗留。
  用 gdbstub 四次采样锁定 RIP 连续停在同一处 —— `http64_get+421`，
  即**桌面主循环被同步 HTTP 请求阻塞**，与 `hlt`、EOI 无关。
- 两处成因：
  1. `net64_http.c` 连接等待是 `5000 轮 × 100 万次空转`。QEMU 软件 CPU 下单轮
     需数十毫秒，外网不可达时会跑满 5000 轮 —— 主循环被堵住数分钟。
     → 改为 RTC 秒级 3 秒墙钟上限 + 短空转，连不上最多损失 3 秒。
  2. `gfx64.c` 的 `TTF_CACHE_ITEMS` 只有 24 槽，而一屏有上百个（字符,字号）
     组合，缓存几乎全程未命中，每次重绘都要重新光栅化 —— 单串文本绘制被放大到
     数百毫秒。
     → 扩到 256 槽，单槽 128×128 收到 64×64（总占用 1 MB）。
- 附带：天气重试从 6 次×60 帧降到 3 次×300 帧，启动期累计阻塞控制在 10 秒内。
- 验证：到桌面耗时 46–55 s → **13–14 s**；HOL blocking 消失后 Uptime 连续推进
  （15 s → 2 m 39 s），时钟逐分前进，通知与网络状态正常渲染。
