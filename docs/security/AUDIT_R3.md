# 审计 R3：syscall 层与遗留项复核

- 审计范围：`kernel/arch/x86_64/` 的 syscall 入口、路径处理、目录列举、SMP 调度门控、遗留孤儿代码
- 方法：只读源码 + 逐条对照 `docs/status/OPEN_BUGS.md` 的记载，**以代码为准**
- 与上一轮区别：本轮重点不是找新漏洞类型，而是核**台账当前是否还成立**，以及找"已被修过一次、换个函数又复发"的同类模式
- 时间：2026-11-14

> 编号 R3-x 独立于 OPEN_BUGS 的 BUG/SEC/OPEN 编号。经确认要立案的，按 OPEN_BUGS
> 的前缀约定升级为对应编号。

---

## R3-1 readdir 的缓冲边界差一（新发现，中）

`syscall64_full.c:1789-1798`：

```c
int need = l + 2;
if (used + need >= (int)sizeof(kbuf)) break;
for (int j = 0; j < l; j++) kbuf[used++] = names[i][j];
kbuf[used++] = 0;                 /* name 结尾 */
kbuf[used++] = types[i] ? 'd' : 'f';
kbuf[used++] = 0;                 /* 项结尾 */
```

实际写入 **l + 3** 字节（name 的 l 字节 + 终止符 + type 1 字节 + 终止符），
但容量检查用的是 `need = l + 2`。

**当前为什么不炸**：break 条件是 `>=` 严格比较，所以通过检查时 `used + l + 2 <= 2047`，
即 `used + l + 3 <= 2048`，恰好落在 `kbuf` 最后一字节之内。**这是凑巧，不是正确**。
一旦把 `kbuf` 尺寸改成非 2048、或把 break 条件改成 `>`，立刻越界 1 字节写栈。

修法：`need = l + 3`。

**为什么值得单独立案**：缓冲区边界靠"两个错误互相抵消"维持，是典型的会在后续
改动里静默失效的形态。它不像崩溃那样显眼，改的人不会意识到自己踩了它。

## R3-2 readdir 输出截断被静默（新发现，中）

`syscall64_full.c:1796`：

```c
if (used > outsz) used = outsz;
```

用户给的 `outsz` 小于实际内容时，直接截断并返回 `used`，调用方会以为这就是全部
条目。目录项被切在中间时，最后一个条目还会变成半截字符串。

**这是 SEC-11 已修过的同一类问题换了个函数复发**：SEC-11 的教训是"ext2 读块失败
被 `break` 吞掉却仍返回完整 `size`，调用方以为读成功"。这里形态相同 ——
**截断不报错，长度照报**。

正确行为二选一：要么返回 `-ENOBUFS` 让调用方换大缓冲，要么在返回长度里明确
表达"还有更多"（Linux getdents 的做法是返回已写部分，调用方靠反复调用推进）。
当前实现两者都不做。

## R3-3 readdir 的 static 表仍是全局单份（SEC-05 残留确认，中）

`syscall64_full.c:1783-1784`：

```c
static char names[64][256];   /* 16 KB BSS */
static uint8_t types[64];
```

台账 SEC-05 记的"`sys_readdir` 静态 `names[64][256]`/`types[64]` 仍不可重入"**确认
仍然成立**，且有两个新增事实：

1. **占用是 16 KB 常驻 BSS**，不是栈。这一点台账没写清，容易让人以为它只是局部变量。
2. **函数自身栈帧约 2.3 KB**：`kernel_path[256]` + `kbuf[2048]`。内核栈上限 4 KB
   （SEC-05 也记了这条约束）。也就是说这个函数单独吃掉超过一半的内核栈，
   而它下面还要调 `ext2_64_list_dir_entries`（同样的 256 宽指针数组、可能再压栈）。

**叠加风险**：如果此刻发生中断，trap frame 再压约 0xA8 字节；若 ext2 读块路径的
局部变量也多，4 KB 会被击穿。这不是理论风险 —— SEC-05 已经把"4 KB 栈"列为未解决
约束，而 `kbuf[2048]` 是当前最大的单个栈数组。

**建议**：`kbuf` 从栈改 `kmalloc`（与 SEC-05 主路径已有的做法一致），或者直接把
`outbuf` 分段拷给用户、取消中间 `kbuf`。

## R3-4 台账过时：路径超长现在已"拒绝"，不是"截断"（文档问题，低但影响判断）

`OPEN_BUGS.md` SEC-07 写："残留待做：路径 ≥255 目前截断而非拒绝（`:451`）"。

代码实测 `syscall64_full.c:32-41`：

```c
static int copy_user_path(char *dst, size_t dstsz, const char *user_path)
{
    if (!dst || dstsz < 2 || !user_path) return -1;
    int n = copy_str_from_user(dst, user_path, dstsz);
    if (n < 0 || (size_t)n >= dstsz - 1) { dst[0] = 0; return -1; }   /* 拒绝 */
    return n;
}
```

**超长路径返回 -1（拒绝），不是截断**。而且所有路径类 syscall 都走这个统一入口
（grep 到 12 处调用点），所以这不是"某个函数修了、别的没修"，是整体已收敛。

**结论**：SEC-07 可以关掉这一条待办。台账应当更新 —— 一条记着"待做"的项其实已经
做完了，会让后续审计把精力花在不存在的问题上（这与 OPEN_BUGS 开头"写错前缀等于
对外宣称不成立的结论"是同一条纪律）。

## R3-5 SB-25 确认：AP 调度门控仍硬关（高，但已知）

`smp.c:289`：

```c
if (cpu_id != bsp_cpu_id && 0) {
```

`&& 0` 意味着**除 BSP 之外的所有 CPU 永远进不了调度分支**。实测确认，与台账一致。

影响面比台账写的更宽：这不只是"AP 不跑任务"，而是**任何依赖多核并发的设计都还
不成立**。具体连带：

- R3-3 的 static 表在真正 SMP 后必炸（两个 CPU 同时 readdir 踩同一块 BSS）；
- `g_fork_parent_gprs[15]`（`syscall64_full.c:1812`）是**单份全局数组**，
  fork 期间保存父进程 GPR 快照，`syscall64_full.c:2101` 写入。两个 CPU 同时 fork 会互相覆盖，
  后果是父进程恢复时寄存器全错 —— 这类问题在单核下永远不会暴露。

所以 SB-25 不是一个独立开关，它是"把一批单核假设摊到台面上"的开关。开之前要先清掉
这些单份全局量。建议在 SB-25 的验收项里明确列出这两条，否则打开就崩。

## R3-6 孤儿 API 与死代码确认（低）

- `apic64_timer_recalibrate()`：`apic64.c:839` 定义，全仓库**无调用点**；
  `smp.c:577` 只在注释里提到它（"进桌面后由 apic64_timer_recalibrate 补校准"），
  注释承诺的行为从未接线。`apic64_timer_needs_recall()` 全仓库无定义。
  → 要么接上，要么删掉，注释不能当作已实现。
- `gdt64_init()`：`gdt64.c:113` 定义，无调用点。GDT 实际由别处初始化，
  这个函数是历史残留。

两者都不影响正确性，属于"文档/注释声称的系统行为不存在"这类问题。

## R3-7 fork 父上下文是单份全局（潜在，SMP 后必爆）

`syscall64_full.c:1806-1818` 一组 `g_fork_parent_{rip,rsp,rflags,cr3,kstack,ustack,retval,gprs}`。

单核下安全（fork 在 syscall 内不被打断）。但在 SB-25 打开后，两个 CPU 同时 fork
会互相覆盖父上下文快照。与 R3-5 同源。

**当前不立项**，但必须写进 SB-25 的解锁前置清单。

---

## 本轮未发现问题的地方（避免下轮重复翻）

- `copy_user_path` 已统一到 12 处调用点，无遗漏的裸 `copy_str_from_user(path)`。
- `sys_readdir` 的 `outsz <= 2` 早退正确，不会写零长度。
- `ext2_64_list_dir_entries_ex` 的 `sizes` 允许 NULL，`ext2_64_list_dir_entries`
  正确转发 NULL，无空指针解引用。

## 建议的处置顺序

| 优先 | 项 | 动作 |
|---|---|---|
| 1 | R3-1 | 改 `need = l + 3`，一行 |
| 2 | R3-2 | 定策略（返回 ENOBUFS 或 getdents 语义），需要改调用方 |
| 3 | R3-3 | `kbuf` 改 `kmalloc`，与 SEC-05 方向一致 |
| 4 | R3-4 | 更新 OPEN_BUGS 的 SEC-07 表述 |
| 5 | R3-6 | 删死代码 / 接线注释承诺 |
| — | R3-5 / R3-7 | 写进 SB-25 前置清单，不单独动手 |
