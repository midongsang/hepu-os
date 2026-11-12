---
title: V0.33 开发路线板
status: active
sys_version: 0.58.0
updated: 2026-11-11
owner: kernel
source_of_truth: ../planning/ROADMAP.md
supersedes: null
---

> ⚠️ **版本说明**：sys_version 对齐 0.54.11（冻结，未 bump）；正文口径仍为 v0.13.x ~ v0.33.x 历史设计稿。源码事实请以 `kernel/arch/x86_64/Makefile` 实际编入为准；现行缺陷状态以 `docs/status/OPEN_BUGS.md` 为准。


# V0.33 路线板（每轮开工前对照本表）

> 本表是**本轮开发的状态板**。优先级来源仍是 `docs/planning/ROADMAP.md`，
> 缺陷清单来源是 `docs/status/OPEN_BUGS.md`。
> 修复流程遵循 `docs/security/BUG_FIX_GUIDELINES.md`（证据优先 → 根因 → 最小修改 → 验证闭环 → 文档记录）。

## 本轮主题

**SEC-12 收官：历史测试二进制补源码 ⇒ 镜像完全可从源码重建。**
补源码过程中牵出三个真实内核缺陷（SEC-15/16/17），一并修掉。

## 阶段与状态

| # | 阶段 | 状态 | 判据 |
|---|------|------|------|
| A | 定位「环境被重置」与 v0.33.0 遗留状态 | ✅ 完成 | 环境恢复脚本落地；交付物核验通过 |
| B | **SEC-12**：12 个历史二进制补源码 + 纳入构建 | ✅ 完成 | `make -C user legacy` 可重现产出；旧产物已删除 |
| C | 补源码牵出的 3 个内核缺陷（SEC-15/16/17） | ✅ 完成 | 每个都有日志证据 + 双 CPU 回归 |
| D | 回归验证（默认 CPU + `--cpu max` 各 3 次） | ✅ 完成 | `exceptions total=0x1 expected(crash_jump)=0x1 unexpected=0x0 [PASS]` × 6 |
| E | 文档同步（OPEN_BUGS / VERSIONING / ROADMAP / COMPAT_ROADMAP） | ✅ 完成 | 与代码行为一致 |
| F | 交付（源码包 + 镜像） | ✅ 完成 | 源码包解包重建内核**逐字节一致**（`dae78c5f…`）+ 镜像双 CPU 0 异常进 shell |

## v0.33.1 新修的内核缺陷

| 编号 | 缺陷 | 根因 | 位置 |
|------|------|------|------|
| 15 | 带 `PT_INTERP` 的 ELF 被加载到一半跑飞（`#GP`），还打断整个自检电池 | 内核对「是否动态链接」**没有判据**，历史上靠「路径里含 `dyn` 三个字符」猜 ⇒ `/bin/dynhello.elf` 撞上，走 V0.20.80 遗留路径，算出 `load bias=0x1000` | elf64.c（+`elf64_has_interp()`）、user_process.c |
| 16 | `open()` 拿到 fd 0 ⇒ 随后的 `read(0,…)` 走进 stdin 分支**永久 hlt** | `sys_open()` 从 0 开始找空槽，未保留 Linux 的 stdio 槽位 0/1/2 | syscall64_full.c |
| 17 | 自检电池的「预期异常」归因错误，SEC-07 覆盖建立在**假故障**上 | 把 `hello_static` 当成「故意跳坏地址」，实际是缺 `.note.ABI-tag` ⇒ `exit(60)` 落空 ⇒ 执行流跑到字符串上 ⇒ `#GP` | kernel_main_64.c + 新增 `crash_jump.elf` |
| 18 | `vga_put_hex()` 自带 `0x` 前缀，调用点又手写 `"0x"` ⇒ **54 处 `0x0x…` 双前缀**日志 | `lib64.c` 的 `vga_put_hex()` 内部已 `vga_puts("0x")`，调用点未察觉 | lib64.c + 14 个 .c 文件 |
| 19 | 补齐 SEC-12 源码后 ET_EXEC **全部被拒**（`exec ERROR: Linux ET_EXEC linked at 0x…`） | `ld` 默认把 `.note.ABI-tag` 放在 text 之前的低地址（实测 `0x400000`），而 `elf64_link_base()` 取**最低 PT_LOAD 的 p_vaddr** ⇒ `link_base < UWIN_START` ⇒ 判成「链接在窗口外」 | user/Makefile（`--section-start=.note.ABI-tag=0x40001000`） |

## 关键证据（留档用）

- **SEC-15**：`[DYNLINK] load bias=0x0000000000001000`（既不在兼容区也不在用户窗口）
  → `[DYNLINK] Needs: <乱码>` → `[EXCEPTION] vector=0xD error=0x0 rip=0x1204AC`。
  修后（v0.33.1 时期）：`[USER64] exec ERROR: dynamic linking is not supported.` + 明确建议，电池不再被打断。
  **注**：该「明确拒绝」行为后被 v0.54.6 P1-7（提交 3375fbe）取代——当前源码加载 PT_INTERP 解释器，现行口径见 OPEN_BUGS SEC-15。
- **SEC-16**：`hello4.elf` 的 `open("/bin/hello3.elf")` 成功返回 **fd=0** ⇒ `read(0,…)`
  命中 `sys_read` 的 stdin 分支（`while (!keyboard64_haschar()) hlt;`）⇒ 日志停在 `[LINUX4] Running`。
  修后：`read+close ok` 正常输出。
- **SEC-17**：`hello_static.elf` 反汇编 = `write(1,msg,25)` + `exit(42)`，**没有跳坏地址**。
  `rip=0x4000102E` 正是字符串 `"Hello from static!\n"` 的首地址 ——
  `exit(60)` 在原生号表里查不到（原生 `exit`=6）⇒ 进程不退出 ⇒ 执行流落到字符串 ⇒
  `"He"` = `48 65 6c` = `rex.W gs insb` ⇒ **CPL=3 执行 `insb` ⇒ `#GP(0)`**。
  修后：`Hello from static!` 干净退出；`crash_jump.elf` 产生**真正的** `#PF`（`vector=0xE error=0x6`）。
- **note 段地址陷阱**：`ld` 默认把 `.note.ABI-tag` 放在 text 之前的低地址（实测 `0x400000`），
  而 `elf64_link_base()` 取**最低 PT_LOAD 的 p_vaddr** ⇒ `link_base < UWIN_START`
  ⇒ ET_EXEC 被判「链接在窗口外」而拒绝。
  修法：`--section-start=.note.ABI-tag=0x40001000` + `-Ttext=0x40002000`，两者都在窗口内。
- **构建目录在顶层，不在 `kernel/arch/x86_64/` 下**：`rm -f build_x86_64/*.o` 若在
  `kernel/arch/x86_64/` 里执行会**静默失败**（该路径不存在），于是旧 `.o` 继续被链进新内核。
  实测踩过：交付内核混着 `BOOT_SELFTEST=1` 的对象，与干净重建差 512 字节、哈希不一致。
  正确写法：`cd /workspace/project/hepu-os && rm -f build_x86_64/*.o`。
  **可重现性判据**：干净重建的 `hepu-kernel64.bin` 与「解包源码后重建」的产物 sha256 必须一致。
- **`vga_put_hex()` 自带 `0x` 前缀**（lib64.c），而调用点普遍又手写了 `"0x"` ⇒ 日志出现
  `0x0x400000` 这种双前缀，误导排障。全项目 54 处已统一修正。

## v0.33.0 已修的内核缺陷（沿用）

| 编号 | 缺陷 | 根因 | 位置 |
|------|------|------|------|
| 1 | glibc static-PIE 退出阶段跳野指针 `rip=0x202` | 进入用户态时未按 Linux `ELF_PLAT_INIT` 清零 GPR，`rdx` 残留值被 glibc 当 `rtld_fini` 注册成退出析构函数 | `kernel/arch/x86_64/user_switch.S` |
| 2 | 用户态 SSE 指令 #GP | `CR4.OSFXSR/OSXMMEXCPT` 从未置位，而 gcc/glibc 默认生成 SSE 指令 | `kernel/arch/x86_64/x86_64.c` |
| 3 | 用户栈 16 字节对齐违约 → `movaps` #GP | 进程入口 RSP 给成 ≡0(mod16)，而 SysV 要求 ≡8(mod16) | `kernel/arch/x86_64/user_stack64.c` |
| 4 | Linux ABI 判定失效 | 链接脚本 `*(.note*)` 把 `.note.ABI-tag` 一起丢掉了 | `user/user.ld` |
| 5 | ext2 读块失败被静默吞掉 | 读失败 `break` 却返回完整 size ⇒ 上层拿到「前半段对、后半段是垃圾」 | `kernel/arch/x86_64/ext2_64.c` |
| 6 | 测试程序 ABI 误判 | `comptest.elf` 无 `.note.ABI-tag` ⇒ 被判成原生 ABI | `user/apps/comptest.c` |
| 7 | `sys_newfstatat` 把**用户指针**直传 `ext2_64_stat` ⇒ SMAP 下内核态 #PF → 整机挂死 | 漏了 `copy_user_path` | `kernel/arch/x86_64/syscall64_full.c` |
| 8 | `syscall_handler_64` 用 `-1` 兼表「未处理」和 errno | 「是否处理过」与「返回值」两个语义混在一处 | `syscall64_full.c` + `syscall64_simple.c` |
| 9 | `sys_arch_prctl` 未知 code 返回笼统 `-1` | 不符 Linux 语义（`ENODEV`/`EINVAL`） | `syscall64_full.c` |
| 10 | **SMEP 拦截内核态调用用户页 IRELATIVE resolver** ⇒ `#PF error=0x11` | `hepu_apply_irelative` 在 ring0 `call *resolver`，SMEP=on 时 ring0 不能取指用户页 | `user_process.c` + `hepu_smap.h` |
| 11 | `syscall64_test()` 自测在说谎 + 栈越界读 | ① 跑在 `full_init` 之前 ② 用 `SYS_GETPID=4`（原生表里是 close）③ 传内核地址给 `copy_from_user` ④ `char buf[8]` 无 NUL 结尾 | `syscall64_simple.c` + `kernel_main_64.c` |

## SEC-12 补源码：历史测试程序清单

源码在 `user/apps/legacy/`，构建产物在 `user/build/legacy/`，
`make -C user legacy` 可重现产出，`tools/make_single_img64.sh` 统一写进镜像。

| 程序 | 类型 | 链接地址 | 用途 | 内核上的表现 |
|------|------|---------|------|-------------|
| `hello_static` | freestanding（裸 syscall） | `0x40002000` | write + `exit(42)` | ✅ `Hello from static!` |
| `hello_linux` | freestanding | `0x40002000` | write + `exit(0)` | ✅ `Hello Linux!` |
| `hello2` | freestanding | `0x40002000` | Linux `getpid`(39) | ✅ `getpid returned: 16` |
| `hello3` | freestanding | `0x40002000` | Linux `open`(2) 真实文件 | ✅ `opened file` |
| `hello4` | freestanding | `0x40002000` | `open`+`read`(0)+`close`(3) | ✅ `read+close ok` |
| `hello_c2` | freestanding（gcc 编 C） | `0x40002000` | C 版 hello（对照汇编版） | ✅ `Hello from C!` |
| `crash_jump` | freestanding | `0x40002000` | **故意**写未映射地址 ⇒ `#PF` | ✅ 预期异常，进程被隔离 |
| `glibc_test` | glibc static-PIE | 兼容区 | 真 glibc 静态程序 | ✅ `Hello from glibc!` |
| `hello_libc` | glibc static-PIE | 兼容区 | glibc + `write` | ✅ `Hello libc!` |
| `dynhello` / `dynhello2` | 动态链接（`PT_INTERP`） | — | 动态加载器 + `DT_NEEDED` | ⛔ v0.33.1 时期明确拒绝（历史状态）；**v0.54.6 P1-7（提交 3375fbe）已改为内核加载解释器 ld.so**，现行口径见 OPEN_BUGS SEC-15/DYN-01~03 |
| `libtest.so` / `libtest2.so` | 共享库 | `0x60000000` | 导出 `test_add`/`get_value`/`func_table` 等 | 随动态程序保留 |

> 历史上 `hello_linux`/`hello2`/`hello3`/`hello4`/`hello_c2` 链在 `0x401000` ——
> 落在用户窗口外，内核按设计拒绝（SEC-13）。补源码时改链到窗口内，
> 它们的测试价值才真正兑现（SEC-13 随之消解）。
> `glibc_test`/`hello_libc` 历史上是 `ET_EXEC @ 0x400000`（同样在窗口外），
> 改用 `-static-pie` 走兼容区，与 `linux_pie_demo` 同一路径。

## 遗留（写入 OPEN_BUGS）

- **SEC-18**（v0.33.1 补记）**ext2 文件读取偶发挂死**。补齐 SEC-12 源码后的首次回归中，
  `hello4.elf`（open → read → close）切到用户进程后日志**完全停住**，整机无任何输出。
  排除项：镜像里的 `/bin/hello3.elf` 与源文件 `cmp` 逐字节一致（13400B）；
  `ext2_64_read_file()` 的 `size` 被夹到 4096 ⇒ 只读 1 块，**不涉间接块**；
  目录遍历有 `rec_len==0` 保护，**不死循环**；开 trace 重跑一次通过；其后 12 次启动全部通过。
  ⇒ 判定为**低频偶发**，与 SEC-11 同源但形态不同（挂死而非报错），**失败点未定位**。
- **SEC-11** ext2 大文件 / 目录跨块读取偶发失败。已修「静默吞掉」+ 上层如实报错；底层失败点未复现（主机端 40KB 跨间接块文件读取逐字节比对一致）。
  另：`ext2_algo_read_data_block()` 的 `ctx->block_buf` 复用待加固。
- **SEC-14** ~~撞号遗留~~ **已关闭**（提交 3375fbe）：撞号 `case` 分支已删除，仅保留
  `SYS_HELLO=0`/`SYS_PUTCHAR=1`，见 OPEN_BUGS SEC-14。
- **SEC-02** clone 子线程共享父进程页表指针，未接入 COW；COW 缺页框架
  `vmm64_try_cow` 已存在（`vmm64.c:432-457`），fork/clone 路径尚未接入。**部分**。
- **SEC-05** 主路径缓冲（kernel_buf/file_buf/elf_buffer）已改为调用期 kmalloc；
  `sys_readdir` 静态 names/types 表残留不可重入，4KB 内核栈约束仍在。**部分**。

## 验证命令（复现用）

```bash
# 环境（沙箱会周期性重置，qemu/nasm 会被抹掉）
bash tools/env_restore.sh

# 内核（默认构建 / 自检电池）
cd kernel/arch/x86_64 && make -j4
cd kernel/arch/x86_64 && make -j4 BOOT_SELFTEST=1
# 注意：改编译宏必须**全量重建**，且删除路径是**顶层** build_x86_64/，不是
#       kernel/arch/x86_64/build_x86_64/（那目录不存在，rm 会静默失败，
#       于是 make 靠时间戳判断「没变化」，旧 .o 继续被链进新内核 ——
#       实测踩过：以为已经全量重建，结果交付内核里混着 BOOT_SELFTEST=1 的 .o，
#       与干净重建的产物差 512 字节、哈希不一致）：
#       cd /workspace/project/hepu-os && rm -f build_x86_64/*.o
#       cd kernel/arch/x86_64 && make -j4 BOOT_SELFTEST=1

# 用户程序
make -C user            # 原生 + PIE demo/probe + legacy 全套
make -C user legacy     # 只补历史测试程序

# 主机端单元测试（5 套件）
bash tools/host-tests/run_host_tests.sh

# 打包 + 启动回归
bash tools/make_single_img64.sh
bash tools/hepu_qemu_boot.sh --sec 50 --log logs/run.log            # 默认 CPU
bash tools/hepu_qemu_boot.sh --sec 50 --cpu max --log logs/run.log  # SMEP/SMAP/SSE 全开
tr -d '\0' < logs/run.log            # 日志含 NUL
```

## 回归结论（v0.33.1）

| 场景 | 结果 |
|------|------|
| 自检电池 · 默认 CPU · 连续 3 次 | 3/3 `[PASS]`（`exceptions total=0x1 expected(crash_jump)=0x1 unexpected=0x0`） |
| 自检电池 · `--cpu max` · 连续 3 次 | 3/3 `[PASS]`（同上） |
| 自检电池 · **干净全量重建**后 · 双 CPU 各 1 次 | 2/2 `[PASS]`（专查「旧 .o 混入」的污染，见下） |
| 自检电池 · **日志格式修复后**复验 · 双 CPU 各 1 次 | 2/2 `[PASS]`，且全量日志 `0x0x` 双前缀 **0 处**（原 54 处 + 7 处内联型） |
| 交付镜像内核 | 与干净重建产物 sha256 一致；`strings` 核对无 `battery finished` / `IREL0` / `raw stack` 残留 |
| 默认构建 · 默认 CPU | 0 异常，进 shell（`Kernel v0.33.1`） |
| 默认构建 · `--cpu max` | 0 异常，进 shell（`SMEP=on SMAP=on SSE=on`） |
| 主机端 5 套件 | 5/5 通过 |
| Linux PIE demo | `Hello from Linux PIE on Hepu!` + `PIE ok, len=30` |
| legacy 全套 | hello_linux / hello_static / hello2 / hello3 / hello4 / hello_c2 / glibc_test / hello_libc 全部正常输出 |
| crash_jump | 产生 1 次预期 `#PF`（`vector=0xE error=0x6`），进程被隔离，内核继续 |
| dynhello / dynhello2 | v0.33.1 复测时**明确拒绝**（`dynamic linking is not supported`），不再打断电池；该拒绝行为后被 v0.54.6 P1-7 解释器加载实现取代（提交 3375fbe），现行状态见 OPEN_BUGS |

> `total=0x1` 的那 1 次异常是 **crash_jump.elf 的故意坏地址测试**
> —— 用来验证 SEC-07「用户态异常只终止该进程、不整机 hlt」。
> v0.33.0 时这个角色被错误地挂在 `hello_static` 身上（见 SEC-17）。

### 可重现性判据（本轮新增，必须守住）

交付源码包解包后重建的内核，必须与工作区**干净重建**的产物 **sha256 逐字节一致**。

> 本轮真的踩了一次：以为「全量重建」了，实际 `rm -f build_x86_64/*.o` 是在
> `kernel/arch/x86_64/` 下执行的，而构建输出目录在**顶层** —— 该路径不存在，
> `rm` 静默失败，make 于是沿用旧 `.o`，交付内核里混进了 `BOOT_SELFTEST=1` 的对象，
> 与干净重建差 512 字节、哈希对不上。
> 现已固化为：`tools/make_source_pkg.sh VERIFY=1` 会自动解包重建并比对哈希。

### 批量文本替换的教训（v0.33.1 收尾，本轮真踩了）

修「`0x0x` 双前缀」这类**跨 60+ 文件**的机械问题时，用正则批量替换比手改危险得多。
本轮连续踩了三个坑，全部是**同一个 Perl 替换串写错**引起的连锁破坏：

1. 替换串 `$1";` —— 捕获组里已含 `vga_puts("`，补 `";` 得到的是 `vga_puts("...";`，
   **缺右括号**；正确写法是 `$1");`。
2. 模式写成 `\);` 而实际字符序是 `";)` —— 括号顺序反了，匹配不上。
3. `[^"]*` 贪婪 + 回溯：在 `vga_puts("(repo not loaded; run 'drv update')\n")` 这种
   **字符串里本身带分号**的地方，会把字符串内部的 `;` 当成匹配点，把字面量截断。

**教训**：
- 跨文件正则替换**必须**在替换后立刻编译验证，不能「跑一遍就信」；
- 破坏形态不唯一（缺括号 / 多括号 / 字符串被截断），得按形态分类统计再修；
- **不要**用「同一套 Perl 改两轮」—— 第二轮的匹配会被第一轮的结果影响，越修越乱。
- 兜底手段：**从已知可编译的交付包恢复**（`deliver/hepu-os-v0.33.1-src.tar.gz`
  里有 09:54 的干净版本），比在坏状态上继续修更快也更可靠。
- 改用 Python 逐行判定「下一行是否是自带 `0x` 前缀的内联 hex 块」，
  比正则安全得多 —— 判定依据是**语义**（块内有没有 `buf[0]='0'`），不是字符形状。

## 交付物（v0.33.1）

| 文件 | 说明 | 校验 |
|------|------|------|
| `hepu-os-v0.33.1-src.tar.gz` | 源码包（19MB / 1607 条目） | `VERIFY=1` 解包后 `make` 重建内核，产物与工作区**逐字节一致**（内核 sha256 `745f21f7c4d00da5…`，`cmp` 通过）。**交付物自身的哈希见 `deliver/SHA256SUMS`**（不要在文档里写死包哈希 —— 包含文档，写死会自引用） |
| `hepu64_single.img` | 稀疏镜像（逻辑 256MB / 物理 4.2MB） | 默认 CPU 与 `--cpu max` 均 **0 异常**进 shell；`/bin` 下 legacy 全套 + 原生程序齐全 |
| `hepu64_single.img.gz` | 压缩镜像（1.9MB） | `gzip -t` 通过 |

> 交付镜像内的内核 = 干净全量重建产物（顶层 `build_x86_64/` 全删后重建），
> **不含** `BOOT_SELFTEST` 电池与任何诊断代码 —— 用 `strings` 核对：
> `CRASH] Running`=0、`IREL0`=0、`raw stack`=0（`SELFTEST` 剩 1 处是
> 「电池已禁用」的提示文本，属正常输出）。

打包脚本：`bash tools/make_source_pkg.sh`（`VERIFY=1` 加解包重建比对），
剔除项在脚本里写死，保证每次打出来的包内容一致。

源码包剔除项（均为可再生成的产物或与本项目无关的第三方二进制）：
`build_x86_64/`、`build_v250/`、`logs/`、`deliver/`、`test_results/`（自动测试截图）、
`user/build/`、`tools/share/`、`tools/firmware/`（OVMF 固件，QEMU 自带 seabios 即可）、
`tools/qemu/`（打包的 QEMU，改用系统 qemu，见 `tools/env_restore.sh`）、
`tools/gnu-efi/`、`hepu-os-v1.02-source-fix.tar.gz`（历史交付物）、`*.o`、`__pycache__`。

## 下一步（按顺序）

1. ~~**SEC-14**：清理撞号历史扩展号~~ **已关闭**（提交 3375fbe，见 OPEN_BUGS SEC-14）。
2. **ROADMAP P0-2**：把 `syscall64_compat.c` 与 security/kunit/compat/tls 接入 64 位 `kernel/arch/x86_64/Makefile` 的 `C_SOURCES`（注意 VPATH / 重复符号）。
3. **SEC-11**：构造大文件 / 目录跨块的可复现用例（当前未复现）；`ctx->block_buf` 同址复用已由 R2-7（0ae1ead）改为独立 ind_buf，余留底层失败点待复现。
4. **SEC-02**：把 fork/clone 路径接入已有 COW 框架（`vmm64_try_cow`），当前 clone 仍共享父进程页表指针。
