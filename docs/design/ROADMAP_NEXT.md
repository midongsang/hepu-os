# HepuOS 下一阶段路线（2026-11-23）

## 0. 基线（只写有证据的部分）

- 32 位：`regress32` PASS（`serial=1 initrd=1 net=1 desk=1 fault=0 bound=1`，909 行）
- 32 位 VFS：写阶段 16/16、读阶段 7/7，连续 3 次 `RC=0`；`-Wall -Wextra` 0 告警
- 页归属：buddy 分配前逐页查 pmm `memory_bitmap`，DMA 缓冲区不再与 e1000/页缓存重叠
- 64 位：源码可编译，**本轮未做镜像级回归**
- kunit：最近一次启动 6 个套件返回非 0（`syscall deep` 1、`user scenario` 3、
  `integration` 2、`security E2E` 1、`compatibility` 1、`performance` 1），
  且 `integration` 从 1 漂到 2、`performance` 是新出现的 —— 结果不稳定
- 技术债：全树 85 处 `TODO/FIXME/stub/未实现`

## 0.1 执行记录（2026-11-23，对应 D2 → D1 → D3）

按“D2 先锁方向 > D1 锁挂载语义 > D3 先测后定”的顺序先落地了三件决策，
P0 门禁仍是下一阶段第一优先：

- **D2 已完成**：`docs/design/FS_CONTRACT.md` + `tools/fs_conform.sh`；
  同一张 ext2 盘上 32→64、64→32 双向读写全 PASS。
- **D1 已完成**：懒加载定为正式语义，`ext2_mount` 只作 `fs mount -f` 调试；
  重复调用幂等实测 `children=1/21/21`，见 `docs/design/MOUNT_STRATEGY.md`。
- **D3 基线已采到**：`cache=unsafe` + 写入中 kill，`e2fsck` 15 处可修、
  0 处修不回；journal 暂缓，先做有序写 + ATA FLUSH + 脏标志，见
  `docs/design/POWERLOSS_BASELINE.md`。

## 0.2 执行记录（2026-11-23，P0 门禁可信）

P0 已完成，`tools/gate.sh` 一条命令给出 `GATE: PASS`。实测 32 位连续启动 3 次，
每次 kunit 都是 `[KTEST] RESULT: ALL PASS`；VFS 写/读两阶段同时 PASS。

本轮定位并修复的真实缺陷：

- **kunit 跑在错误环境**：原来在 IF=0、PIT ticks=0 的窗口运行，`boot complete`、
  `interrupts active`、`datetime`、`sys_time` 类断言必然失败。现在运行前临时开中断，
  等 PIT 走满 1 秒，跑完恢复原 IF。
- **`kmalloc(0xFFFFFFFF)` 回绕**：32 位 `ALIGN(size,4)` 把 `0xFFFFFFFF+7` 截断成 0，
  超大请求被当成 0 字节成功。现在拒绝 `> 0xFFFFFFF8`，并按 8 字节对齐。
- **堆返回指针只有 4 字节对齐**：块头 20 字节改为 24 字节（显式 `reserved` 填充），
  返回数据区自然 8 字节对齐；`kmalloc_aligned` 增加总长回绕检查。
- **slab 对象区未 16 字节对齐**：位图后对象区偏移按 16 字节向上取整。
- **PAE 启动期建表写坏 BSS**：`pae_init` 在 legacy 分页下构造 PDPT/PD/PT，分配到的
  页与 kheap 统计变量所在 BSS 页重叠，`heap_total/heap_end` 被写成 `0 / 0x0024C007`。
  启动路径改为只做 `pae_detect()`（探测 CPU 与物理地址宽度，不建表），完整结构留给
  shell 手动触发。

门禁判定：32/64 位构建 0 error + VFS 跨启动 PASS + 32 位 kunit 连续 3 次 ALL PASS，
才输出 `GATE: PASS`；任一不满足即 `GATE: FAIL` 且退出码 1。

## 0.3 执行记录（2026-11-23，P1-1 元数据区保护）

P1 第一项落地，核心防线与目录一致性修复均通过门禁：

- `ext2_alloc.c` 增加 `ext2_is_metadata_block()`，分配前统一挡住超级块/GDT/
  块位图/inode 位图/inode 表（含 backup），组 0 `bit0` 旧防线保留。
- `tools/ext2_meta_guard.sh` 对抗测试：清掉组 0/1 共 32 个元数据块位后启动
  VFS 写阶段，`VFS-TEST: PASS`，`e2fsck` 未报 inode 表/GDT/超级块结构损坏，
  输出 `META-GUARD: PASS`。
- 由 `e2fsck` 暴露并修复：目录 `i_blocks` 漏记、父目录链接计数、`bg_used_dirs_count`、
  `rename` 目标已存在时残留目录项。
- 门禁复跑：`GATE: PASS`，32/64 构建 0 error，VFS 写读 PASS，kunit 连续 3 次 ALL PASS。

仍未关闭：`/data/clock/alarms.txt` 的 inode 32 在磁盘上为全 0，`e2fsck` 仍报
deleted/unused inode、filetype 与位图差异。已排除数据块覆盖 inode 表；根因在
`ide_ext2_writefile`/inode 表脏页落盘路径，待单独定位。详见
`docs/design/EXT2_METADATA_GUARD.md`。

## 0.4 执行记录（2026-11-23，P1 收口）

P1 三项验收证据已齐备：

- **掉电 5 次**：`tools/powerloss_5x.sh` 连续 5 次 `cache=unsafe` 写入中 `kill -9`；
  5/5 `fsck` 可修、0 处修不回，`pl.dat` 全部保留；第 2–5 次只剩块位图/空闲计数差异。
- **`fs mount -f`**：phase `f` 改走真实 `cmd_fs("mount -f")` 两次，
  `tools/mount_full_test.sh` 实测 `children=1/8/8`、`MOUNT-FULL: PASS`。
- **cpall 嵌套**：写阶段 17/17、读阶段 8/8 PASS；
  新增 `shell.cpall_nested` 与 `disk.cpall_nested`，覆盖三层目录跨启动。
- **门禁**：`GATE: PASS`，32/64 构建 0 error，VFS 写 17/17、读 8/8，kunit 连续 3 次 ALL PASS。

详见 `docs/design/P1_STORAGE_RELIABILITY.md`。`/data/clock/alarms.txt` 的 inode 32
记账问题不在本轮 5 次样本中复现，保留为已知债。

## 1. “有生产力”的验收场景（先定义，再排期）

不用百分比描述进度，用五个可复现场景：

- **S1 写读闭环**：在图形终端里创建文件 → 写入 → 保存 → 重启 → 内容还在
- **S2 掉电可恢复**：写入过程中强杀 QEMU → 重启 → `fsck -f` 能修复，已 `sync` 的数据不丢
- **S3 权限生效**：普通用户写系统目录被拒，返回 `EACCES`
- **S4 双架构一致**：同一套文件操作用例在 32/64 位都通过
- **S5 门禁确定**：同一镜像连续启动 3 次，测试结果完全一致；一条命令给 PASS/FAIL

S1+S2+S5 达成之前，不把系统称为“可日常使用”。

## 2. 阶段与验收

### P0 门禁可信（最高优先，先做）

问题：kunit 返回值随启动顺序漂移，回归靠肉眼看串口。门禁不可信，后面所有结论都打折。

任务：
1. 6 个非 0 套件各跑 3 次，记录每次返回值，分成“依赖运行时状态（跑太早）”“真 bug”“测试自身不稳定”三类
2. 给 KTEST 用例加依赖标注（RTC/中断/调度器就绪），允许“跳过”而不是“失败”
3. `verify_p1p3.sh` 增加机器可读汇总行（`GATE: PASS/FAIL`），不再依赖 grep 单条日志
4. 新增 `tools/gate.sh`：串起 32 位构建、VFS 验证、启动冒烟、kunit 判定

验收：同镜像连续 3 次启动，kunit 逐套件返回值完全一致；`gate.sh` 一条命令出结论。

### P1 存储可靠性

任务：
1. ext2 元数据区统一保留：块分配前校验“超级块 / GDT / 块位图 / inode 位图 / inode 表”不可分配（现在只有组 0 位图 bit0 一道防线）
2. 掉电一致性用例：写入中途 `kill -9` QEMU，重启后 `fsck -f`，比对已 `sync` 文件
3. `ext2_mount` 接线：接进启动流程，或正式宣布“懒加载”为最终方案并写进文档
4. cpall 多层嵌套目录用例（当前只覆盖浅层）

验收：强杀 5 次，`fsck` 全部能修；已 `sync` 文件 0 丢失；`verify` 增加断电用例并通过。

### P2 内存与并发地基

任务：
1. buddy 变成 pmm 的唯一后端（`mm_alloc_pages` 走 buddy），删掉“先分配再补记”的双账结构
2. debug 构建加断言：同一物理页被二次分配立即 panic（把“靠自觉”变成“靠机制”）
3. 页缓存回收压力测试、slab 全尺寸复用测试

验收：debug 构建跑 30 分钟分配/释放压力无断言触发；代码里只剩一套页分配入口。

### P3 安全最小闭环

任务：
1. VFS 变更操作（`mkdir/unlink/rmdir/rename/write`）统一做 capability 检查
2. 错误码统一：`ENOENT/EEXIST/EACCES/ENOSPC` 按 POSIX 返回，补用例表
3. fd→路径映射、`fs_do_open` 对不存在文件返回 `ENOENT`
4. 32 位账户子系统从桩变成 root + 1 普通用户的最小实现

验收：S3 场景通过；错误码用例表全过。

### P4 64 位对齐（需要先拍板，见 D2）

任务：
1. 二选一：抽取 32/64 共享的 VFS/ext2 层（推荐），或明确“双实现”并各自维护
2. 64 位镜像级回归脚本（`make_single_img64.sh` + 串口断言）
3. 64 位 `getdents` 支持非根目录

验收：S4 场景通过。

### P5 生产力闭环

任务：
1. 终端内文本编辑器（新建/打开/编辑/保存），或移植现有编辑器
2. 基础工具集：`ls/cat/cp/mv/rm` + 编辑 + 运行一个静态程序
3. 录制 S1 的完整证据（串口 + 截图）

验收：S1 场景端到端跑通并留证。

## 3. 决策点（需要拍板）

- **D1 `ext2_mount`**：启动时全量建树，还是永久保留懒加载？前者启动慢但语义完整，后者快但依赖每条路径都能懒解析。
- **D2 32/64 架构**：抽共享层（一次重构、长期省事）还是双实现（短期快、长期双倍维护）？
- **D3 崩溃一致性**：只做 `fsck` + 写屏障（轻），还是引入 ext3 式 journal（重）？

## 4. 明确不做（防止摊子铺大）

P0–P2 完成前，不碰：SMP、网络协议栈、GUI 美化、应用商店/包管理、更多文件系统。

## 5. 建议的前五个提交

1. `test(kunit): 6 个非 0 套件定性 + 依赖标注`
2. `tools: gate.sh 一键门禁`
3. `fs: ext2 元数据块统一保留 + 断电恢复用例`
4. `mm: buddy 作为 pmm 唯一后端 + 二次分配断言`
5. `security: VFS 变更操作 capability 检查`

每个提交都必须带实测输出，否则不推。
