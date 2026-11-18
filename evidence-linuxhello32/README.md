# OBS-07 证据目录（32 位 `linuxhello` 自检卡死/退出）

分支：`fix/linuxhello32`（基于 main `391e1db`）。日期：2026-11-18。

## 结论

**结论 A：定位并修好。** 根因是 `kernel/fs/transcomp.c` `transcomp_selftest()` test7
在内核栈上声明 64 KB+ 的 `transcomp_mgr_t stats`，在 16 KB 内核栈上向下越界，踩坏相邻
BSS 的 binfmt 格式表 `formats[]`；`binfmt_exec` 随后按被踩坏的探测/加载指针分发到野地址，
触发 `#PF/#GP` 风暴并复位/挂起退出，自检卡在 `[AUTO] linuxhello start`。

修复：test7 改为直接读静态管理器标量 `tc_mgr.compress_count` 与
`transcomp_get_ratio()`，不再在栈上整体拷贝大结构；公共 API `transcomp_get_stats()`
保留未删。

## 文件清单

| 文件 | 说明 |
|---|---|
| `before-baseline.log` | 改前基线（main `391e1db`，未加探针）串口全文。`linuxhello start` 后 `[PM] low battery, auto suspend pending`，无 `linuxhello done`。 |
| `after-fix.log` | 改后干净构建串口全文。`linuxhello` 进用户态打印 `Hello from Linux ELF on HEPU!`，`[AUTO] linuxhello done`，序列续跑后续自检项。 |
| `excerpt-before.txt` | 改前 AUTO 序列关键行摘录。 |
| `excerpt-after.txt` | 改后 AUTO 序列与各程序 pid/用户态输出摘录。 |
| `investigation-trace-dbg12.log` | 插桩期探针日志：`formats[]` 影子完整性检查在各启动里程碑全部 `ok count=0x00000003`，最终 `elf_load enter path=linuxhello.elf`。仅作调查留痕。 |
| `smoke_check.sh` | 私有冒烟判定脚本，口径同 `tools/regress32.sh` 头部注释；不启动 QEMU、不 `pkill`、不写共享目录。 |
| `smoke-results.txt` | 对改前/改后两份日志的判据结果。 |

## 冒烟判据（与 `tools/regress32.sh` 一致）

- `serial`：含 `[SERIAL] COM1 16550 UART ready`
- `initrd`：含 `head=0x55504548`
- `fault`：**不含** `PANIC|#GP|#PF|fatal error`（=0 才过）
- `net`：含 `[NET-TEST] === Self-Test Complete ===`
- `desk`：含 `[DESKTOP] elastic free desktop init done`
- `bound`：`tools/check_gfx_boundary.sh` 退出码 0

复现判定（不要直接执行 `tools/regress32.sh`，其内部 `pkill -9 -x qemu-system-i386`
会杀其他 agent 的 QEMU，日志写共享目录）：

```bash
bash evidence-linuxhello32/smoke_check.sh evidence-linuxhello32/after-fix.log
```

结果：`serial=1 initrd=1 net=1 desk=1 fault=0 bound=1`，冒烟 PASS。

> 说明：五条通用判据在改前基线也成立（串口/initrd/网检/桌面都在 linuxhello 之前完成）；
> OBS-07 的区分性证据是 AUTO 序列在 `linuxhello start` 后是否出现 `done` 并续跑后续项，
> 见 `excerpt-before.txt` / `excerpt-after.txt`。

## 根因取证要点

- `transcomp.h`：`TRANSCOMP_MAX_BLOCKS=4096`，`transcomp_block_map_t` 16 B/项，
  `transcomp_mgr_t.block_map[4096]` ≈ 65 536 B，外加 7 个 `uint32_t` 计数字段。
- `kernel/include/constants.h:85`：`KERNEL_STACK_SIZE=16384`（16 KB）。
- 探针二分：`binfmt_init` 结束时 `formats[]` 完好（ELF32 id=1 / PE32 id=2 / MACHO id=3，
  probe/load 为合法 `.text` 地址）；`after-kmalloc-test` 仍完好；到 `transcomp_selftest()`
  之后表损坏（`fmt_count=2`，指针成垃圾）。
- 修复后 objdump 实量 `transcomp_selftest` 帧最大 `sub $0x24,%esp`，在 16 KB 栈内。

## 排除项

- `fork-test rc=-2`（`HEPU_E_NOENT`）：initrd 无此文件的预期返回，不建进程槽、不污染
  后续 exec，不是污染源。
- 插桩构建（dbg14–16）曾在加载器绑定标准流时 `vfs_find("/dev/console")` 命中 VFS 链表
  尾部野指针触发 `#PF`（vec=0xE，err=0）；剥离探针后的干净构建未复现，判为插桩导致内存
  布局偏移的次生现象，未计入 OBS-07 残留缺陷。若干净树复现，下一步在 desktop/app init
  各阶段转储 root 子链表与 slab 块头定位写入者。

## 探针处置

所有临时 `[DBG32]` 打点（cmd_run_auto 入口/出口、exec 返回码、加载器分发、syscall、
异常向量、`formats[]` 影子副本与完整性检查、VFS 链表遍历转储）收尾时已从源码删除；
提交内仅保留 `kernel/fs/transcomp.c` 的栈局部修复。插桩期原始大日志（dbg14–16，各约
24 MB）未纳入版本库，留在本 worktree 的 `.qemu-logs/` 下。
