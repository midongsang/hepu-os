---
title: 赫普OS 未关闭问题清单
status: active
sys_version: 0.54.6
updated: 2026-10-31
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
| OPEN-03 | QEMU sendkey 不被键盘驱动读取 | 0.13.50 已知限制 | 验证中 |
| OPEN-04 | 安装器不拷贝系统文件 | install64.c L213-L231 | 待解决 |
| SEC-01 | ~~用户程序可直接读写内核内存~~ **已修复（v0.14.0~v0.18.0）**：私有 PML4/PDPT 默认开、高半区直接映射/RAM>1GB、fork 复制独立地址空间、SMEP+SMAP、brk 分页+NX(W^X)；QEMU 512MB/1.5GB 均到桌面、sec_test 通过。**v0.33.0 补**：用户堆/TLS 预留区搬入用户窗口私有分区（PDPT[1]，原落在内核共享 PDPT[0] 树导致用户态写自己堆 #PF）；大页不可当页表写（新增 vmm64_split_huge_pde）；PMM 显式保留内核栈页（原与用户栈物理重叠） | user_aspace64.c/user_access64.c/vmm64.c/pmm64.c；VERSIONING 0.18.0/0.33.0 | 已关闭 |
| SEC-02 | 旧模式页表/用户页帧仍不释放（隔离模式已回收）；fork 仍整页深拷贝而非写时复制(COW) | user_process.c user_process_release；user_aspace64.c | 部分 |
| SEC-03 | fork 父子进程调度。**v0.34.0 已修复**：根因是 schedule() 里 `if (old && old->stack)` 跳过上下文切换，去掉后子进程可调度；子进程exit后通过 fork_return_to_parent 切换到父进程CR3+恢复GPR快照+sysret回父用户态。forktest端到端通过：CHILD输出可见、g_shared隔离正确、PARENT恢复运行、干净exit(0)。 | process64_full.c, syscall64_full.c, entry_64.S | 已关闭 |
| SEC-04 | 裸盘读写系统调用无权限控制；sys_kill 目前仅限自身，缺 uid/capability 模型 | syscall64_full.c sys_disk_*/wrap_kill | 待解决 |
| SEC-05 | 系统调用静态缓冲区不可重入（kernel_buf/file_buf/elf_buffer），内核栈仅 4KB | syscall64_full.c | 待解决 |
| SEC-06 | ~~NX/W^X/SMEP/SMAP 未启用~~ **已修复（v0.16.0/v0.17.0/v0.18.0）**：SMEP、SMAP、堆 NX+brk 分页均已开；内核态缺页仍 hlt。**v0.33.0 补**：SMAP 在 x86-64 是可选特性（QEMU 默认 qemu64 不支持），原先无条件 stac/clac 会 #UD 死机；现按 CR4 真实状态门控（hepu_smap.h），启动打印 hardware state: SMEP/SMAP。**v0.33.0 收尾再补**：SMEP 与 SMAP 是**两个独立机制** —— SMAP 管数据访问（靠 AC 标志）、SMEP 管取指（靠 CR4 位），STAC 解除不了取指限制。内核在用户页表下执行用户代码（IRELATIVE resolver）时必须同时临时清 SMEP，否则 `--cpu max` 下必然 `#PF error=0x11`（bit4=1 取指违规）。`hepu_smap.h` 已补 `hepu_user_exec_enter/exit()`，按 CR4 真实状态对称处理 | vmm64.c、hepu_smap.h、x86_64.c、user_process.c | 部分（SMEP/SMAP 启用与门控已闭环；内核态缺页仍 hlt） |
| SEC-07 | **部分落地（v0.33.0）**：用户态异常只终止该进程已实现（lib64.c 判定 stack_frame[20]&3==3 → user_process_fault_current() 改写中断帧经 user_return_trampoline 回内核，user_process_t 增 faulted/fault_vector，用户寄存器转储）。每进程 fd 表、sys_kill 限自身、只读/只写 fd 限制、路径≥255 拒绝、ext2 跳过已删除目录项**仍待做** | lib64.c、user_process.c；VERSIONING 0.33.0 | 部分（异常隔离已验证，fd/权限项待做） |
| SEC-08 | **已落地（v0.33.0）**：sec_test.elf 自动运行收进 HEPU_BOOT_SELFTEST 开关，默认关（make BOOT_SELFTEST=1 打开）；自检项含 Linux syscall、user_access、aspace、EXT2、[LINUXPIE]、[PIEPROBE]。**v0.33.0 收尾**：新增可判定汇总 `[SELFTEST] battery finished: exceptions total=N expected(hello_static)=M unexpected=K [PASS/FAIL]`（唯一计数源 = `g_user_exception_count`）；QEMU 默认 CPU 与 `--cpu max` **各连续 3 次** 均 `unexpected=0x0` [PASS]；默认构建（不带自检）双 CPU 均 0 异常进 shell；主机端 5 套件 5/5 | kernel_main_64.c、arch/x86_64/Makefile；VERSIONING 0.33.0 | 已关闭 |
| SEC-10 | **已修复（v0.33.0）**：① glibc static-PIE 崩溃 —— 根因是内核进入用户态时**未按 Linux ABI 清零通用寄存器**（`iretq` 只恢复 RIP/CS/RFLAGS/RSP/SS，其余 GPR 带着内核残留值进用户态）。glibc 静态程序 `_start` 把 `rdx` 当 `rtld_fini` 传给 `__libc_start_main`，后者把它注册成退出析构函数，进程退出时跳到该野指针 → 取指 #PF（实测 rip=0x202，恰好等于内核 `rflags` 字面量 0x202）。证据链：崩溃时 `r14=0x500C0108` = 兼容区基址+`__exit_funcs` 偏移 → 内核直读 glibc 退出链，`ror17(stored)^guard` 解码正好得到 0x202；atoi 与其它条目解码为正常代码地址。修复：`user_enter` 汇编按 Linux `ELF_PLAT_INIT` 清零 rax/rbx/rcx/rdx/rsi/rdi/rbp/r8-r15 ② `pie_probe.elf` AT_PHNUM 读到内核地址 —— 根因是 `entry_64.S` 的 `syscall_entry_64` 里 `movq %rsp,%r13` 发生在 `pushq %r13` **之前**，破坏了用户 r13，已删除该指令 ③ 探针读 raw stack 越界 —— 探针自身缺陷，已改为向下读并删除调试 dump ④ **v0.33.0 收尾追加**：`sys_newfstatat` 把用户指针直传 `ext2_64_stat`（漏 `copy_user_path`）⇒ SMAP 下内核态 #PF 整机挂死；`syscall_handler_64` 用 `-1` 兼表「未处理」与 errno ⇒ 合法 errno 被误报 `Unknown syscall`；`sys_arch_prctl` 未知 code 返回笼统 `-1`（改按 Linux 语义 `ENODEV`/`EINVAL`）；**SMEP 拦截内核态调用用户页 IRELATIVE resolver**（`error=0x11`，bit4=1 取指违规）⇒ 按 SMAP 对称方式临时清 `CR4.SMEP`；`syscall64_test()` 自测撞号 + 栈越界读 + 初始化顺序错（`full_init` 必须早于 `test`） | user_switch.S、entry_64.S、user_process.c、user_stack64.c、lib64.c、syscall64_full.c、syscall64_simple.c、hepu_smap.h、kernel_main_64.c、user/apps/pie_probe/ | 已关闭 |
| SEC-11 | **v0.33.0 新开**：**ext2 读块失败被静默吞掉**。`ext2_64_read_file()` / `ext2_64_read_file_offset()` 原来在块读取失败时 `break`，却仍返回完整 `size` ⇒ 调用方以为整份文件读成功，实际拿到「前半段正确、后半段是垃圾」的缓冲区。表现为低频且看起来毫不相关的故障：900KB 的 `glibc_test.elf` 跑到指令中间（rip 落在指令中间 ⇒ 控制流被破坏）、`.so` 模块加载偶发失败 / 符号解析不到（GOT 留 0 ⇒ 跳到地址 0）。**v0.33.0 已修上层语义**（失败如实返回 -1，不再谎报长度）。**v0.33.0 收尾复核**：修后 `hello_static.elf` 一度读不出（`[USER64] exec ERROR: Cannot read file`）—— 该文件在镜像内存在且与原件逐字节一致；主机端用 40KB 跨一级间接块的 `big.bin` 做逐字节比对**完全一致**，`ext2_algo_read_file()` 返回值与期望长度相符 ⇒ **ext2 共享算法本身正确，底层失败点未复现**；v0.33.0 最终回归中 `hello_static.elf` 读取正常（日志 `[USER64] exec: read 4736 bytes` + `Hello from static!`）。另发现 `ext2_algo_read_data_block()` 用 `ctx->block_buf` 兼作「间接块缓冲」与「目标数据块缓冲」，二者同址（读间接块会覆盖目标块），实测因写入顺序恰好自洽而未暴露 —— 属**潜在**缺陷，已登记待加固。**v0.33.1 补记**：补齐 SEC-12 源码后出现过一次同源但形态不同的偶发故障（ext2 读取**挂死**而非报错），详见 SEC-18 | ext2_64.c、kernel/shared/fs/ext2_algo.c | 部分（上层已修；底层未复现，见 SEC-18；**block_buf 同址复用已于第二轮审计 R2-7 修复**，改用独立 ind_buf，见 docs/security/AUDIT_64BIT_R2.md） |
| SEC-14 | **v0.33.0 新开**：**历史扩展 syscall 号与原生号表撞号**。`syscall64_simple.c` 里的 `SYS_HELLO=0 / SYS_PUTCHAR=1 / SYS_PUTS=2 / SYS_EXIT=3 / SYS_GETPID=4` 是早期 Hepu 扩展号，而 `syscall64_full.c` 的原生号表里 2=read、4=close —— 同一号两套语义。`syscall64_test()` 曾用 `SYS_GETPID=4` 测 getpid，实际测的是 close（且在 `full_init` 之前调用，dispatch 还没就绪 ⇒ 返回 -1，日志打印 `PID: FFFFFFF`）。v0.33.0 已把自测改为用原生 `getpid=20` 并修正初始化顺序（`full_init` 必须早于 `test`），但那套 `case SYS_*` 分支本身仍在文件里，后续误用风险未消除 | syscall64_simple.c、kernel_main_64.c | 待解决 |
| SEC-12 | **已关闭（v0.33.1）**：**启动自检依赖无源码的历史二进制产物**。`hello_linux.elf`、`hello_static.elf`、`hello_c2.elf`、`glibc_test.elf`、`dynhello.elf`、`dynhello2.elf`、`hello2/3/4.elf`、`libtest.so`、`libtest2.so` 只有 `user/build/` 里的编译产物，源码不在仓库中，也没有 Makefile 规则 ⇒ 镜像无法从源码完整重建（交付准则不满足）。**v0.33.1 已关闭**：通过反汇编 + 符号表 + 寄存器转储逐条还原这 12 个程序的语义（含 `test_add`=a+b、`my_syscall3`=裸 syscall 封装、`get_value`/`double_value`/`call_func` 走 `test_value` 指针与 `func_table` 间接表），源码补到 `user/apps/legacy/`（含共享头 `common_linux_abi.h`），并纳入 `user/Makefile` 的 `legacy` 目标与 `all`；`tools/make_single_img64.sh` 统一从 `user/build/legacy/` 写进镜像；旧的无源码产物已从工作区删除 ⇒ 镜像**完全**可从源码重建。补源码过程中牵出三个真实内核缺陷（SEC-15/16/17）与一处**错误的历史结论** | user/apps/legacy/、user/Makefile、tools/make_single_img64.sh | 已关闭 |
| SEC-15 | **v0.33.1 新开**：**内核对带 PT_INTERP（动态链接器）的 ELF 没有任何判据**，且历史上用了一个**按文件名猜** ELF 性质的启发式 —— `user_process.c` 里 `if (path[i]=='d' && path[i+1]=='y' && path[i+2]=='n') is_dyn=1;`，即**路径里含 "dyn" 三个字符**就算动态程序，于是走 V0.20.80 的遗留 `hepu_dynlink_relocate()` 路径。该路径自己按 `pva - poff` 算 load bias，对 `/bin/dynhello.elf` 算出 `0x1000`（既不在兼容区 [0x50000000,0x60000000) 也不在用户窗口内），DT_NEEDED 字符串用 `exe_buf + dynstr + val` 取，读出乱码，程序在没初始化好的 GOT/PLT 上跑飞 → `#GP vector=0xD`，且**打断整个启动自检电池**。反过来说，名字里不带 "dyn" 的真·动态程序会被漏判。**v0.33.1 已修**：新增 `elf64_has_interp()`（判据来自 ELF 的 PT_INTERP），在 exec 阶段对带 PT_INTERP 的程序**明确拒绝**并给出可执行建议（HepuOS 只加载静态/静态 PIE）；删除「文件名含 dyn」启发式及其调用 | elf64.c、elf64.h、user_process.c | 已关闭 |
| SEC-16 | **v0.33.1 新开**：**`open()` 会占用 fd 0/1/2**。Linux 里 stdin/stdout/stderr 是保留槽位，进程启动时已打开，`open()` 永远分配不到这三个号；而 `sys_open()` 原来从 0 开始找空槽，于是进程的**第一次** `open()` 就拿到 fd=0。同时 `sys_read()` 把 `fd==0` 一律当 stdin 处理（阻塞等键盘，`while (!keyboard64_haschar()) hlt;`）—— 于是「打开一个文件再读它」这种最普通的用法会**永久卡死**。现场：`hello4.elf`（open `/bin/hello3.elf` 再 read 64 字节）打开成功拿到 fd=0，紧接着 `read(0,...)` 走进 stdin 分支 → 整机停在该进程上，启动自检电池直接断在那里（表现为「日志停在 [LINUX4] Running」）。**v0.33.1 已修**：`sys_open()` 改为从 fd=3 开始分配，与 Linux 语义一致 | syscall64_full.c | 已关闭 |
| SEC-17 | **v0.33.1 新开**：**启动自检电池对「预期异常」的归因是错的**。`kernel_main_64.c` 原来把 `hello_static.elf` 注释为「**故意**跳到坏地址的异常测试」，并把它产生的 1 次用户态异常计入 `expected`。反汇编证明这是**误判** —— `hello_static.elf` 的内容只有 `write(1,msg,25)` + `exit(42)`，**根本没有跳坏地址**；它当时之所以炸，是因为缺 `.note.ABI-tag` 被判成 Hepu 原生 ABI ⇒ Linux `exit(60)` 在原生号表里查不到（原生 `exit` 是 6）⇒ 进程不肯退出 ⇒ 执行流落到后面的字符串数据 ⇒ `"He"` 被解成 `rex.W gs insb` ⇒ CPL=3 执行 `insb` ⇒ **`#GP(0)`**（`vector=0xD error=0x0 rip=0x4000102E`，rip 恰好是字符串首地址 —— 这正是当初误判为「故意跳坏地址」的由来）。后果有两层：① 电池的「预期异常」数字与真实故障混在一起，掩盖了判别力；② SEC-07「用户态异常只终止该进程」的验证**建立在一个假故障上** —— 补上 ABI note 后 `hello_static` 干净退出，异常消失，SEC-07 的覆盖就归零了。**v0.33.1 已修**：补 ABI note 后 `hello_static` 正常 `exit(42)`；新增真正的 `crash_jump.elf`（显式写未映射地址 `0xDEAD0000` ⇒ `#PF`）承担 SEC-07 覆盖；电池判据改为 `expected(crash_jump)=N`，现双 CPU 各 3 次均 `exceptions total=0x1 expected(crash_jump)=0x1 unexpected=0x0 [PASS]` | kernel_main_64.c、user/apps/legacy/crash_jump.c、user/apps/legacy/hello_static.c | 已关闭 |
| SEC-18 | **v0.33.1 新开（未复现，登记备查）**：**ext2 文件读取路径偶发挂死**。补齐 SEC-12 源码后的第一次启动回归中，`hello4.elf`（`open("/bin/hello3.elf")` → `read(fd,buf,64)` → `close`）在切到用户进程后**日志完全停住**，整机不再有任何输出（无 EXCEPTION、无返回），表现为「启动自检电池断在 `[LINUX4] Running`」。**排除项**：① 用 `debugfs` 从镜像分区抽出 `/bin/hello3.elf` 与源文件 `cmp` **逐字节一致**（13400 字节），文件本身无损坏；② `ext2_64_read_file()` 的 `size` 被 `max_size` 夹到 4096 ⇒ 只读 1 个数据块，**不涉及间接块**；③ `ext2_find_in_dir()` 的目录遍历有 `rec_len == 0` 保护，**不会死循环**；④ 同一镜像加开 `HEPU_TRACE_LINUX_SYSCALL=1` 重跑，**一次通过**（`read+close ok`）；⑤ 随后双 CPU 各 3 次自检电池 + 各 3 次交付构建回归（共 12 次启动）**全部通过，无一次复现**。⇒ 判定为**低频偶发**，与 SEC-11 描述的「ext2 大文件/目录跨块读取偶发失败」同源，但**失败形态不同**（本次是挂死而非报错）。当前**未定位到具体失败点**，遗留待复现 | ext2_64.c、kernel/shared/fs/ext2_algo.c | 未解决（低频，未复现） |
| SEC-13 | **v0.33.0 新开**：**用户态 ABI 约定的三处违背**（均由 v0.33.0 定位并修复，登记备查）：① 入口 GPR 未清零（同 SEC-10）② **用户栈 16 字节对齐**：内核给的初始 RSP 曾为 ≡0(mod16)，而 gcc 生成的 `_start` prologue 固定减少 0x38 字节，随后 `movaps %xmm0,(%rsp)` 要求 RSP≡0 ⇒ 实际落在 ≡8 ⇒ `#GP`（error code bit0=0 = 对齐检查失败）。按 SysV 约定改为 **RSP ≡ 8 (mod 16)** ③ **CR4.OSFXSR/OSXMMEXCPT 从未置位**：用户态执行任何 SSE 指令直接 `#GP`；x86-64 架构要求 SSE2，glibc/gcc 默认生成 SSE 指令 ⇒ 这不是可选加速而是用户态 ABI 硬要求（Linux `start_thread` 同样置位）。现三项均已修，启动打印 `hardware state: SMEP=.. SMAP=.. SSE=..` | user_switch.S、user_stack64.c、x86_64.c | 已关闭 |
| SEC-09 | ~~进程正常退出后记录仍 active，槽位不回收~~ **已修复（0.14.0）**：user_return_cleanup 正常退出也 release 槽位（active=0、释放内核栈、清 fd 表）；QEMU 连续 10 次 exec 无「No free process」、空闲页 delta=0 | user_process.c:user_return_cleanup | 已关闭 |

## v0.33.2–v0.33.5 安全修复记录

| 编号 | 问题 | 修复 | 状态 |
|------|------|------|------|
| SEC-14 | syscall64_simple.c 旧扩展号 SYS_PUTS=2/SYS_GETPID=4 与原生号表 read=2/close=4 撞号 | 删除撞号 case，保留 SYS_HELLO=0/SYS_PUTCHAR=1 | v0.33.4 关闭 |
| — | hepu_apply_irelative 在 ring0 call *resolver，r_addend 来自 ELF，可内核态执行任意代码 | 删除 call，GOT 直接写 r_addend+load_bias | v0.33.3 关闭 |
| — | sys_futex 直接解引用 uaddr 未校验 | 加 verify_user_read | v0.33.2 关闭 |
| — | hepu_user_ptr 不查 U/S 位和用户窗口 | 加 [0x40000000,0x80000000) 检查 + 每级 PTE 查 U/S | v0.33.2 关闭 |
| — | hepu_elf_vaddr_to_fileoff 无边界检查 | 加 elf_size 参数，phnum/offset/filesz 校验 | v0.33.2 关闭 |
| — | hepu_user_ptr 写用户页未查 NX(W^X) | 新增 hepu_user_write_ptr，GOT 写改用它 | v0.33.5 关闭 |
| — | syscall64_compat.c 曾是未接入的兼容层 | 已从构建与源码移除（死代码） | v0.54.6 关闭 |
| — | 5个脚本硬编码 /home/user/Doubao 路径 | 改 $(dirname $0) 相对定位 | v0.33.5 关闭 |

## v0.34.0+ 开发记录

| 版本 | 内容 | 状态 |
|------|------|------|
| v0.34.0 | fork调度修复（schedule()跳过idle进程） | ✅ |
| v0.35.x | 恢复图形桌面 + hepu_nographics标志 | ✅ |
| v0.36.x | tgkill + shell kill + ps PPID/MEM/PRIO + hepu_panic栈回溯 | ✅ |
| v0.37.x | kill统一 + sigaction桩 + install命令 | ✅ |
| v0.38.0 | install命令（ext2文件复制） | ✅ |
| v0.38.4 | SEC-18 ext2挂死修复（目录遍历guard上限） | ✅ |
| v0.39.0 | 页错误处理框架（CR2/PTE转储） | ✅ |
| v0.40.0 | SEC-02 COW fork（引用计数+清W位+PF复制页） | ✅ |
| v0.40.2 | signal框架（sigaction存handler） | ✅ |
| v0.41.0 | 抢占式调度（PIT IRQ0触发schedule） | ✅ |
| v0.42.0 | 信号投递（用户栈改写 + pending_signals位图） | ✅ |
| v0.42.1 | pkg list命令 | ✅ |
| v0.43.0 | QoS优先级调度 + nice命令 | ✅ |
| v0.43.1 | 隔离域列表 + process64_count_by_env | ✅ |
| v0.44.1 | wm list命令 | ✅ |
| v0.45.0 | hepua AI命令（v1.0+启用） | ✅ |
| v0.54.6 | P1-7 动态链接：内核按 PT_INTERP 加载 ld-linux 并摆 auxv，DT_NEEDED 由用户态 ld.so 解析；`hello_dyn` 输出 Hello, World! | ✅ |

## P1-7 动态链接修复记录（2026-11）

| 编号 | 问题 | 修复 | 状态 |
|------|------|------|------|
| DYN-01 | **syscall 入口丢弃第 6 个参数**。`entry_64.S` 只把用户 rdi/rsi/rdx/r10/r8（arg1..5）交给 C 处理函数，Linux 的 arg6（r9）从未传出。mmap 的第 6 参正是文件偏移 ⇒ ld.so 的 `MAP_FIXED` 分段映射全部按 offset=0 读文件，libc 各段内容错位，最终在 ld.so 内部 `#PF`（rip=0x60015E11） | 入口把栈槽里的用户 r9 作为第 7 个参数压栈传给 `syscall_handler_64`，dispatch/表调用把 arg6 继续下传（seccomp args[5] 同步） | 已关闭 |
| DYN-02 | **fstat 的 st_dev/st_ino 恒为 0**。glibc 的 `_dl_map_object_from_fd` 用这对值判断「文件是否已加载」；全为 0 时 libc.so.6 被误判成主程序已加载对象，于是版本检查对主程序做（`no version information available`），符号查找报 `undefined symbol: __libc_start_main, version GLIBC_2.34`，进程 exit 127 | `ext2_64_stat_ino()` 返回 ext2 inode 号；`sys_fstat`/`sys_newfstatat` 填 st_dev=1、st_ino=真实 inode | 已关闭 |
| DYN-03 | Linux 兼容层若干桩实现阻塞 ld.so：`openat` 固定返回 fd 3、`fstat` 恒返回 0、`read` 只读首 4KB、缺少 `pread64` | `openat` 转真 `sys_open`；`fstat` 填完整 Linux struct stat；`read` 按 fd 偏移读；新增 `pread64`（SYS_LNX64_PREAD=136）；文件偏移 `mmap`（含 MAP_FIXED）与 `USER_MMAP_BASE=0x64000000` 避开程序/解释器区 | 已关闭 |

## 路线图完成状态

| 优先级 | 内容 | 状态 |
|--------|------|------|
| P0 安全 | SEC-02 COW / SEC-04 裸盘 / SEC-05 动态缓冲 / SEC-06 panic栈 / SEC-07 fd隔离 / SEC-14 撞号 / SEC-18 ext2挂死 | ✅ 全部完成 |
| P1 进程 | fork/waitpid/COW/kill/tgkill/ps/sigaction/抢占调度/信号投递 | ✅ 全部完成 |
| P2 文件 | 基础工具/install/pkg list | ✅ 基本完成 |
| P3 隔离域 | 优先级调度/nice/domains | ✅ 基本完成 |
| P4 图形生态 | VBE桌面/gui/wm list/hepua | ✅ 基础完成 |

**v1.0+ 目标**：glibc兼容层、gcc移植、shell脚本解释器、Vulkan后端、窗口拖动完善。
