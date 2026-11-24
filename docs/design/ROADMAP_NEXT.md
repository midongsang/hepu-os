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

## 0.5 执行记录（2026-11-24，S1 最小生产力）

S1 的图形终端写读闭环已经在 32 位 hpdesk 上跑通并留证：

- **首次启动**：终端里 `edit /s1_gui.txt` → 输入 `HEPU_S1_GUI` / `SECOND LINE`
  → `.save` → `.quit` → `cat`；截图 `docs/assets/s1/s1-first-boot.png`。
- **同一张盘重启**：autostart 改成 `cat /s1_gui.txt`，重启后仍显示两行；
  截图 `docs/assets/s1/s1-restart.png`。
- **基础文件工具**：`ls/cat/cp/mv/rm/mkdir` 全部走 32 位 VFS；
  截图 `docs/assets/s1/s1-file-tools.png`。
- **真实键盘输入**：F12 打开终端后逐行输入 `help`、`edit /s1dyn.txt`、
  `HEPU_DYNAMIC_OK`、`.save`、`.quit`、`cat`，屏幕逐行刷新；
  截图 `docs/assets/s1/s1-terminal-dynamic.png`。
- **真实键盘写入后重启**：同一张盘重启，F12 + `cat /s1dyn.txt` 仍显示
  `HEPU_DYNAMIC_OK`；截图 `docs/assets/s1/s1-terminal-reboot.png`。
- **静态程序**：图形终端里 `run linuxhello`，串口输出
  `Hello from Linux ELF on HEPU!`，见 `docs/assets/s1/s1-run-linuxhello.log`。
- **VFS 自测**：写阶段 18/18 PASS（含 `term.edit`）、读阶段 9/9 PASS
  （含 `disk.s1_edit`）。

本轮同时修复四个真实缺陷：

- `ext2_alloc_block()` 先置位/减计数、后判元数据块，命中元数据时不回滚，
  会把空闲块计数吃光；改为先判元数据再置位。
- `cpall` 递归复制 `/proc`、`/dev`、`/sys`、`/tmp` 伪文件系统，产生约
  5 万次块分配写满 58 MB 盘；改为跳过这四个目录。
- 32 位 hpdesk 的 `gfx_width/height/bpp/pitch` 仍停在 320x200@8bpp，
  `gfx64_compat` 的填充/像素接口按旧全局裁剪，终端动态内容全部落在
  屏幕外；改为每帧校准到桌面真实分辨率。
- 32 位窗口 surface 没有真实离屏目标，内容画到主缓冲后又被全零 surface
  贴回，Settings 与时间胶囊整窗透明；改为 32 位窗口一律直绘主缓冲。
  截图 `docs/assets/s1/s1-settings-visible.png`、
  `docs/assets/s1/s1-time-capsule.png`。

详见 `docs/design/S1_MINIMAL_PRODUCTIVITY.md`。

S1 已闭环：真实键盘输入、动态逐行上屏、`.save` 落盘与重启后读回均
通过；autostart 首帧、文件工具与静态程序证据保持有效。

## 0.6 执行记录（2026-11-24，P2 内存单一后端）

P2 的代码项已落地，`tools/gate.sh` 连续 `GATE: PASS`：

- **buddy 成为 pmm 唯一后端**：`kernel/mm/buddy.c` 删除 `buddy_free_bitmap`，
  只保留总页数/扫描提示/初始化标志；分配按 `2^order` 块对齐，先用
  `mm_page_used()` 整块预检，再逐页 `mm_alloc_page_at()` 记账；释放只逐页
  `mm_free_page()`。`mm_alloc_page()` / `mm_alloc_pages()` 在 `buddy_init()`
  后统一转给 buddy，早期引导阶段仍走 pmm 线性位图。
- **二次分配断言**：`HEPU_BOOT_DEBUG` 下 `mm_alloc_page_at()` 发现目标页位已置位
  即打印物理地址并 `assert_fail()`。临时探针实测输出
  `[MM] FATAL: double alloc page ...` + `[ASSERT] FAILED`，探针已撤回。
- **新增 3 个 kunit 压力用例**：buddy 连续页 1/2/4/8/16 页五轮、slab 六档
  32–1024B 六轮、页缓存 4096 页触顶 + 第 4097 页 LRU 回收。stress 套件
  从 6 个用例变为 9 个，门禁 3 次启动均为 9/9 pass。
- **非 2 的幂释放修正**：`buddy_free_pages()` 现在按分配时的 `2^order` 块大小
  释放，避免 DMA 等 3 页请求漏掉尾部页；最大 order 提到 20，覆盖 swap
  2048 页和 GPU 帧缓冲。

详见 `docs/design/P2_MEMORY_BACKEND.md`。30 分钟长稳压测尚未跑，作为 P2 的
剩余验收项保留。

## 0.7 执行记录（2026-11-24，P3 安全最小闭环）

P3 的代码项已落地，`tools/gate.sh` 为 `GATE: PASS`：

- **VFS 统一权限检查**：`vfs_open/write/mkdir/unlink/rmdir/rename/chmod/chown`
  先过 VFS 能力剖面（root 全放行；普通用户写/建/删仅限 `/data` `/tmp`
  `/app` `/home`），再叠加 inode 的 POSIX 权限位。父目录不存在时普通用户
  写 `/system/...` 优先返回 `EACCES`。
- **POSIX 错误码**：`vfs.h` 新增 `VFS_ENOENT/EACCES/EEXIST/EISDIR/ENOTEMPTY/
  ENOSPC` 等；ext2 inode/块分配失败返回 `-28`，VFS 原样上抛。
- **fd→路径**：`vfs_fd_t` 增加 `path`，新增 `vfs_fd_get_path()`；读写对
  fd>=3 再复查一次权限。
- **32 位账户**：`auth64_compat32.c` 从空桩改为 multi_user 适配层，
  root(uid 0/role 2) + 普通用户(uid 1/role 1)；`sys_user` 暴露按 uid
  设置/校验密码的入口。
- **64 位 open**：`fs_do_open` 先 lookup，未命中且无 `O_CREAT` 返回 `ENOENT`，
  不再先分配 fd；`O_CREAT` 已存在按成功打开处理。
- **新增 `p3-security` kunit 套件（2 个用例）**：S3 普通用户写系统目录被拒
  `EACCES`、自己的可写根下可建/写/删；错误码表覆盖 EEXIST/ENOENT/EISDIR/
  ENOTEMPTY/ENOSPC。

详见 `docs/design/P3_SECURITY_CLOSURE.md`。P3 仍是“最小闭环”：账户不落盘、
无登录界面、chmod/chown 未写回 ext2 inode，作为后续 P4/账户持久化的输入。

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
