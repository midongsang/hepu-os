---
title: 兼容层落地路线（COMPAT_ROADMAP）
status: active
sys_version: 0.58.0
updated: 2026-11-12
owner: planning
source_of_truth: null
supersedes: null
---

# Hepu OS 兼容层落地路线

> 来源：用户指导 + 自研 OS 经验教训
> 更新：2026-10-30, v0.33.1

## 核心原则

1. **不要零损耗幻想**。哪怕 FreeBSD Linuxulator 打磨几十年，syscall 密集 workload 仍有 5~15% 固有损耗。ABI 语义差异无法消除。
2. **先原生，后兼容**。自己的 ABI/内核/图形栈不干净，后面兼容层会越堆越乱。
3. **用户态优先**。能在用户态做的翻译不要下沉内核，保持内核干净。
4. **混合模式**：高频 syscall 后期选择性下沉，不做完整 Linuxulator。

## 阶段划分

### 阶段1（当前已完成大部分）：原生 POSIX
- [x] 内核原生系统调用表（不模仿 Linux 号）
- [x] 原生 ELF 加载器（静态链接 ELF）
- [x] 原生 shell / 桌面 / 文件管理 / 终端
- [x] ext2 文件系统、AHCI、网络栈基础
- [x] 安全隔离（页表、SMEP/SMAP、用户地址空间）
- [ ] 真机稳定启动（当前黑屏回 BIOS，待修）
- [ ] WiFi/网线连接完善
- [ ] 通知系统

### 阶段2（当前进行中）：用户态 Linux ELF 加载 + syscall 翻译
**策略**：不污染内核。Linux syscall 号 → Hepu syscall 号的翻译表在 dispatch 层。
**预期性能**：慢 20~30%，可接受做原型。

已完成：
- [x] Linux x86_64 号翻译表：`linux_map[350]` 表容量 350，其中**实际有映射的约 45 项**（其余为 0 表示"不翻译"）。号表事实源见 `specs/SYSCALL_ABI.md`
- [x] ELF 加载 + 6 种重定位（RELMATIVE/64/GLOB_DAT/JUMP_SLOT/PC32/PLT32）
- [x] PT_DYNAMIC 解析（stripped .so 支持）
- [x] 跨模块符号查找
- [x] mprotect 真正改 PTE
- [x] 完整 144 字节 stat 结构
- [x] fork + exec + waitpid 链路
- [x] pipe 管道
- [x] PT_INTERP 检测（且解释器加载已落地，见下方 2026-11-11 实况更新）
- [~] DT_NEEDED 枚举：内核遗留枚举/重定位 `hepu_dynlink_relocate` 未接线，DT_NEEDED 依赖链由用户态 ld.so 完成

v0.33.0 阶段性结论（已落地并实测）：
- [x] **ABI 按进程判定**：`user_process_t.env_type`（ENV_NATIVE / ENV_LINUX_COMPAT）取代全局 `g_running_linux_elf`。
      原实现无条件置位该全局开关，导致原生 read(2) 被当 open(2)、open(3) 被当 close(4) —— 原生 ABI 被兼容层绑架。
      现在只有 ENV_LINUX_COMPAT 进程才走 Linux 号翻译；原生 ABI 优先、兼容层是附加层。
- [x] **PIE（ET_DYN）装载路径**：装载偏置（map_base - 映像原点）+ `R_X86_64_RELATIVE`（交 glibc 自己应用，内核不重复加基址）。
- [x] **SysV 初始栈**：`hepu_build_initial_stack()` 构建 argc/argv/envp/auxv（AT_PHDR/AT_PHENT/AT_PHNUM/AT_PAGESZ/AT_BASE/AT_FLAGS/AT_ENTRY/AT_UID…/AT_RANDOM/AT_EXECFN/AT_NULL），
      16 字节对齐，RSP 指向向量区起点。**仅对 ENV_LINUX_COMPAT 进程启用**，不污染 Hepu 原生（ulib）进程的栈语义。
- [x] **R_X86_64_IRELATIVE 由内核应用**：Linux 语义是内核在装载时逐条调用 resolver 并把结果写回 GOT。
      static-PIE 的 glibc `_dl_relocate_static_pie` 只处理 RELATIVE、不处理 IRELATIVE，漏掉这一步 GOT 里就是未初始化的桩地址。
      实现要点：扫描**全部** SHT_RELA 段（.rela.dyn / .rela.plt / .rela.iplt）—— dynamic 段未必留 DT_RELA；
      resolver 地址与 GOT 槽地址都**必须加装载偏置**（重定位项里的地址是链接期地址）；调用时切用户 CR3。
- [x] **ET_EXEC 明确拒绝**：绝对地址已烧进代码/数据，无法重基址。装载时给出明确诊断
      “ET_EXEC absolute addresses are baked in - cannot be rebased. Rebuild as PIE (-fPIE / -static-pie)”，不静默错装。
- [x] **进程入口 ABI 契约（v0.33.0 补）**：兼容层要跑真 glibc，内核就必须兑现 Linux 的进程入口约定——
      ① **入口 GPR 清零**（Linux `ELF_PLAT_INIT`）：`iretq` 只恢复 RIP/CS/RFLAGS/RSP/SS，其余 GPR 会带着内核残留值进用户态。
      glibc 静态程序 `_start` 把 `rdx` 当 `rtld_fini` 传给 `__libc_start_main`，后者注册成退出析构函数 ⇒ 退出时跳到野指针
      （实测 rip=0x202 = 内核 `rflags` 字面量）。**这是「兼容层反噬内核」的典型：兼容层假定 ABI，内核必须履约。**
      ② **初始 RSP ≡ 8 (mod 16)**（SysV 约定）：gcc 生成的 `_start` prologue 固定减少 0x38 字节，其后 `movaps` 要求 RSP≡0。
      ③ **CR4.OSFXSR|OSXMMEXCPT 必须置位**：用户态 SSE 指令否则直接 #GP（x86-64 架构要求 SSE2，这是 ABI 硬要求，不是可选加速）。
- [x] **glibc static-PIE 端到端跑通**：`linux_pie_demo.elf` 完成输出 → atexit 链 → `exit_group` 正常退出（`[SYSCALL64] Process exited with code 0`）。
- [x] **ABI 判定补齐 `.note.ABI-tag` 通道**：内核用 `PT_NOTE` 里的 `NT_GNU_ABI_TAG` 判定 GNU/Linux ABI，
      但链接脚本的 `/DISCARD/ : { *(.note*) }` 把 `.note.ABI-tag` 一并丢掉了 ⇒ 用 Linux 号表的裸 syscall 程序被误判成原生程序
      （Linux 号全部打错、exit 变成 Unknown syscall、跑飞到野地址）。链接脚本改为只丢 `.note.GNU-stack`。

v0.33.0 收尾追加（双 CPU 对齐，`--cpu max` 暴露出的内核侧真缺陷）：
- [x] **SMEP 与 SMAP 是两个独立机制，必须分别处理**。
      兼容层要在**用户页表下执行用户代码**（IRELATIVE resolver = ifunc 解析器），
      这在内核态（ring0）发生 ⇒ SMEP=on 时 ring0 **不能取指用户页** ⇒ `#PF error=0x11`
      （bit4=1 取指违规 + bit2=0 内核态发起）。
      **SMAP 救不了这个**：SMAP 管数据访问（靠 AC 标志），SMEP 管取指（靠 CR4 位），STAC 解除不了取指限制。
      `hepu_smap.h` 补 `hepu_user_exec_enter/exit()`，按 CR4 真实状态对称地临时清/恢复 SMEP。
- [x] **「用户指针进内核函数」必须逐一封堵**：`sys_newfstatat` 把 path 直传 `ext2_64_stat()`，
      而 `ext2_64_resolve_path()` 第一件事就是解引用 `path[0]` ⇒ SMAP 下内核态 #PF ⇒ 整机挂死。
      已改 `copy_user_path`；同批排查了 syscall64_full.c 里全部 ext2_* 调用点。
- [x] **「是否处理过」与「返回值」两个语义彻底分离**：旧 `syscall_handler_64` 用 `-1` 兼表
      「这个号没人处理」与「处理结果就是一个 errno=-1」，导致 glibc 正常的 `arch_prctl` 调用被误报成
      `[SYSCALL] Unknown syscall: 9E`。现引入 `g_syscall64_handled` 独立标志。
- [x] **arch_prctl 错误码对齐 Linux**：`ARCH_SET_CPUID` 不支持 ⇒ `ENODEV(-19)`；`ARCH_GET_CPUID` ⇒ 返回 1（可用）；
      未知 code ⇒ `EINVAL(-22)`（原先笼统 `-1`）。
- [x] **内核自检本身也要诚实**：`syscall64_test()` 曾用撞号（`SYS_GETPID=4` 实为 close）、传内核地址给
      `copy_from_user`、跑在 `full_init` 之前 ⇒ 日志里的 `PID: FFFFFFF` 与
      `[SECURITY] copy_from_user: invalid user address` 全是自测写错造成的假象；已全部改正。

v0.33.1 追加（SEC-12 收官：历史测试程序补源码，牵出三个内核侧真缺陷）：
- [x] **历史测试二进制全部补源码，镜像完全可从源码重建**（SEC-12 关闭）。
      12 个无源码的历史程序经反汇编 + 符号表 + 寄存器转储逐条还原语义，源码补到 `user/apps/legacy/`：
      `hello_static`（write+exit(42)）、`hello_linux`、`hello2`（Linux getpid）、`hello3`（Linux open 真实文件）、
      `hello4`（open+read+close）、`hello_c2`（gcc 编 C 版）、`crash_jump`（**故意**写未映射地址）、
      `glibc_test`/`hello_libc`（真 glibc 静态）、`dynhello`/`dynhello2`（动态链接）、
      `libtest.so`/`libtest2.so`（共享库，导出 `test_add`/`get_value`/`func_table` 等）。
      全部带 `.note.ABI-tag` —— 这批程序当年**正因缺它**而被打成原生 ABI（见 SEC-10/缺陷 6）。
- [x] **PT_INTERP 明确拒绝，不再猜**（SEC-15）。
      内核对「这是不是动态链接程序」原本**没有判据**，靠「路径里含 `dyn` 三个字符」猜 ——
      `/bin/dynhello.elf` 恰好撞上，于是走了 V0.20.80 的遗留 `hepu_dynlink_relocate()`：
      它自己按 `pva - poff` 算 load bias（算出 `0x1000`，既不在兼容区也不在用户窗口内），
      DT_NEEDED 用 `exe_buf + dynstr + val` 取（读出乱码），程序在没初始化好的 GOT/PLT 上跑飞 `#GP`，
      还把整个启动自检电池打断。
      现按 ELF 自身的 `PT_INTERP` 判定（`elf64_has_interp()`），在 exec 阶段就拒绝并给出建议
      （HepuOS 只加载静态 / 静态 PIE）；文件名启发式及其调用一并删除。
      **兼容层的边界因此清晰：静态与静态 PIE 是支持的，动态链接器不在支持范围内。**
      > **2026-11-11 实况更新**：上述 v0.33.1 的"exec 阶段拒绝 PT_INTERP"已被后续实现取代——PT_INTERP 解释器加载现已接入 exec（`user_process.c:1938-2128`：从 ext2 读 ld.so、按偏置装入库区、映射进进程并切换入口，填 AT_BASE/AT_ENTRY/AT_PHDR）；DT_NEEDED 符号/重定位交由用户态 ld.so，内核遗留 `hepu_dynlink_relocate()` 不再被调用（`user_process.c:2140-2150`）。完整 Linux 动态链接（glibc .so 依赖链）仍未端到端打通。
- [x] **`open()` 不得占用 fd 0/1/2**（SEC-16）。
      兼容层要跑 Linux 程序，而 Linux 里 stdin/stdout/stderr 是保留槽位。内核 `sys_open()` 原来从 0 开始
      找空槽，于是进程第一次 `open()` 拿到 fd=0；而 `sys_read()` 把 `fd==0` 一律当 stdin（阻塞等键盘）
      ⇒ 「打开文件再读它」这种最普通的用法把整机卡死（现场：`hello4.elf` 打开成功却挂死，日志停在
      `[LINUX4] Running`）。现改为从 fd 3 开始分配。
- [x] **自检电池的「预期异常」必须来自真实意图**（SEC-17）。
      电池原先把 `hello_static` 当成「故意跳坏地址的异常测试」，但反汇编证明它只有 `write`+`exit(42)`，
      炸掉是因为缺 ABI note ⇒ `exit(60)` 在原生号表查不到 ⇒ 进程不退出 ⇒ 执行流落到字符串数据 ⇒
      `"He"` 被解成 `rex.W gs insb` ⇒ CPL=3 执行 `insb` ⇒ `#GP(0)`（`rip` 恰好是字符串首地址，
      这正是当初误判的由来）。**SEC-07 的覆盖建立在一个假故障上**；补 note 后它干净退出，覆盖就归零了。
      现新增 `crash_jump.elf`（显式写未映射地址 `0xDEAD0000` ⇒ 真正的 `#PF`）承担该角色，
      电池判据改为 `expected(crash_jump)=N`。
- [x] **历史程序的链接地址改到用户窗口内**（SEC-13 随之消解）。
      `hello_linux`/`hello2`/`hello3`/`hello4`/`hello_c2` 当年链在 `0x401000`（窗口外 ⇒ 内核按设计拒绝），
      现链到 `0x40002000`；`glibc_test`/`hello_libc` 当年是 `ET_EXEC @ 0x400000`（同样在窗口外），
      改用 `-static-pie` 走兼容程序区 —— 与 `linux_pie_demo` 同一条已验证的路径。
      连带修掉一个隐蔽陷阱：`ld` 默认把 `.note.ABI-tag` 放在 text 之前的低地址（实测 `0x400000`），
      而 `elf64_link_base()` 取**最低 PT_LOAD 的 p_vaddr** ⇒ `link_base < UWIN_START`
      ⇒ ET_EXEC 被判「链接在窗口外」而拒绝；现用 `--section-start=.note.ABI-tag` + `-Ttext` 双地址钉在窗口内。

用户地址空间分区契约（PDPT[1]，用户窗口 [0x40000000,0x80000000)）：
| 分区 | 范围 | 说明 |
|------|------|------|
| 原生区 | 0x40000000–0x50000000 | Hepu 原生 ABI 程序 |
| 兼容 PIE 区 | 0x50000000–0x60000000 | Linux ET_DYN/PIE 装载基址 |
| 库区 | 0x60000000–0x68000000 | 共享库/预加载 |
| TLS 预留 | 0x67810000–0x68010000 | glibc `__libc_setup_tls` 自留 TLS 块（建进程时一次性映射） |
| 堆区 | 0x68000000–0x6F000000 | brk 区，初始 break 0x68010000 |
| 栈区 | 0x6FFC0000–0x70000000 | 栈顶 0x70000000，1MB = 256 页 |

进行中/下一步：
- [ ] 真实 ld-linux + libc.so.6 入 rootfs，跑动态链接程序
- [ ] vDSO：把 clock_gettime/gettimeofday 映射到用户页，避免 syscall
- [ ] 收集能跑的静态 Linux 二进制列表
- [ ] 错误码翻译（-EINVAL 等）
- [ ] **SEC-12**：给剩余历史二进制（hello_linux/hello_static/hello_c2/glibc_test/dynhello*/hello2~4/libtest*）补源码并纳入
      `user/Makefile`，让镜像可从源码完整重建（交付准则）
- [ ] **SEC-14**：清理 `syscall64_simple.c` 里与原生号表撞号的历史扩展号（`SYS_PUTS=2` 实为 read、`SYS_GETPID=4` 实为 close）

### 阶段3（未来）：混合模式，热点下沉
**触发条件**：阶段2 跑通后，profile 出哪些 syscall 调用最频繁。
**做法**：
- read/write/clock_gettime/epoll 这类超高频，在内核加快速兼容入口
- 大部分仍走用户态翻译
- 不做完整 Linuxulator

候选热点（按预期频率排序）：
1. read/write (stdio)
2. clock_gettime / gettimeofday
3. epoll_wait
4. mmap/munmap
5. futex

### 阶段4（远期）：完整 Linuxulator
**触发条件**：桌面生态刚需 Linux 二进制（如微信、Chrome 必须跑）。
**注意**：这是大工程，不轻易启动。

### Windows 兼容：非常后期
- Wine 移植是独立大工程
- 等 Linux 兼容稳定后再启动
- 不要一开始就想同时兼容三家

## 不要做的事

1. **不要在阶段2就做完整 Linuxulator**——过度工程。
2. **不要把 Linux syscall 号直接硬编码进内核**——保持 Hepu 原生 ABI，翻译在 dispatch 层。
3. **不要追求零损耗**——接受 5~30% 损耗。
4. **不要先做 Windows 兼容**——Wine 比 Linux 兼容难得多。
5. **不要为了跑某个软件改内核语义**——改兼容层，不改内核。
