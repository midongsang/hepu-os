# epoll / eventfd2 / pselect6 v1 回归结论（2026-11-18）

> 范围：仅记录 B 段 v1 LT 实现（提交 `a1308bd`，实现落点 `kernel/arch/x86_64/epoll64.c` /
> `epoll64.h`、薄壳 `syscall64_full.c`、探针 `user/apps/epolltest.c`）的回归判定。
> 语义与限制见 `docs/specs/SYSCALL_ABI.md` 文末「epoll / eventfd2 / pselect6 v1 语义与限制」小节。

## 1. 验收结论

**epoll/eventfd2/pselect6 的 v1 LT 实现验收成立**：探针 `epolltest.elf` 在真实 QEMU 内跑通，
**23/23 项断言 PASS，0 项 FAIL**；本次内核串口全日志无 `PANIC` / `#PF` / `#GP`。

探针关键返回（串口 `[EPOLL]` 行，证据日志 `epolltest_serial_final.log`）：

- `epoll_create` 返回合法 fd=3；`pipe()` 成功；`epoll_ctl ADD` 管道读端返回 0；
- 空管道 `wait(100ms)` 超时返回 0；写管道后 `wait` 返回 1、事件含 `EPOLLIN`、`data` 回传读端 fd=4；
- `DEL` 返回 0，`DEL` 后 `wait` 超时返回 0；对 epfd 误用 `read/write` 返回 `-22(EINVAL)`；
- `eventfd2` 返回 fd=6，write=8 / read=8，读回计数=5，读空返回 `-11(EAGAIN)`；
- 注册 eventfd 后就绪 `wait` 返回 1、含 `EPOLLIN`；
- `pselect6` 空管道超时返回 0，有数据返回 1，位图保留读端（位图值 64）。

## 2. 未被破坏的回归面（附证据行）

| 回归面 | 证据行 | 来源 |
|---|---|---|
| 静态 PIE 用户程序 | `Hello World from ELF64 user space!` | `epolltest_serial_final.log` |
| 兼容层探针 comptest | `[UPROC] exec path=/bin/comptest.elf rc=100`（无崩溃） | `epolltest_serial_final.log` |
| epoll 探针本体 | `[EPOLL] RESULT: PASS`，`epolltest.elf rc=100`，故障计数 0 | `epolltest_serial_final.log` |
| fd 表并发复用 fdstress | `[FDSTRESS] RESULT: PASS`；`open_fail=0`、`close_fail=0`、`bad_range=0`、`conflict=0`、`leak_probe_fd=3 (no leak)` | `regress64_fdstress.log`（`BOOT_SELFTEST=1` 构建） |
| 32 位内核冒烟 | `serial=1 initrd=1 net=1 desk=1 fault=0 bound=1 lines=638`，`== 32 位内核冒烟 PASS`；修复提交 `c28d4d9` | `regress32_serial.log` / `regress32_pass.txt` |

> fdstress 为编译期电池探针，默认发布构建不编入；上表数值来自 `make BOOT_SELFTEST=1`
> 重建后的 64 位内核在同一镜像上的开机自跑结果。

## 3. dyn 证据未取得 + 现象（原样记录，未美化）

`tools/gui_test.py --scenario dyn` 被崩溃挡住，**未取得输出**。现象：

- 执行 `/bin/glibchello.elf` 后客户机触发 **vector=0x8（双重错误）**；
- `rip=0x1001C5`、`cs=0x8`、`rsp=0xF000FF53`；
- 崩溃发生在 `uname` 之后；QEMU 随之终止，**无 exit code**。

## 4. 归因证据：既有动态加载/兼容层问题，非 epoll 改动引入

对照方法（B 段排查时实测）：

1. 把 B 段全部改动从工作树撤出：被跟踪的 3 个文件 `git checkout HEAD --`
   （`syscall64_full.c`、`kernel/arch/x86_64/Makefile`、`user/Makefile`），
   新建的 `epoll64.c` / `epoll64.h` 移到 `/tmp/epoll_stash/`；
2. 用 **HEAD 基线提交 `efbd497`** 内核（未含任何 epoll/eventfd2/pselect6 改动）跑**同一镜像**；
3. 复现**同一 `rip=0x1001C5`、同一 `vector=0x8`、同一 `rsp=0xF000FF53`** 的双重错误 PANIC。

据此判定：该崩溃属于**既有动态加载 / glibc 兼容层问题**，与本次 epoll/eventfd2/pselect6 v1
改动无关。dyn 回归因此**未取得**，列为独立待办，不影响本条交付。

## 5. 待办与交叉引用

- **独立待办（dyn 双重错误）**：排查 `/bin/glibchello.elf`（及开机自跑的 `/bin/dynhello.elf`）
  在 `uname` 之后触发 vector=0x8 的根因；修复后重跑 `tools/gui_test.py --scenario dyn`，
  断言串口出现 `Hello, World!`。该项归属动态链接 / ELF 装载兼容线（ROADMAP P1-2 / P1-7），
  **不是** epoll 回归项。
- 与 `docs/specs/SYSCALL_ABI.md`「已知不支持 / 后续接入点」小节交叉引用：本 v1 实现的限制
  （EPOLLET 边沿触发、`EPOLLOUT` 分类、`EFD_SEMAPHORE`、eventfd 阻塞读空、注册表未按进程隔离、
  无 pipe 写唤醒钩子）属功能完整度问题；**dyn 双重错误不在该限制清单内**，勿与 v1 功能限制混淆，
  也勿据此认为 dyn 崩溃是被 epoll 改动弄坏的。
