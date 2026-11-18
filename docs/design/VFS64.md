# VFS64 抽象层（P3-6）设计与落地记录

> 范围：赫普OS **64 位**树（`kernel/arch/x86_64/`）。32 位遗留 `kernel/fs/vfs.c`
> 不复用、不改动。本文记录 v1 已落地什么、哪些 syscall 已走 VFS、哪些仍直调、
> 验收证据、已知问题与后续拆解。状态口径见文末「进度结论」。

---

## 1. 契约对照（冻结来源：`docs/design/UNIX_LESSONS.md` 第一节）

### 1.1 ops 表签名逐条对齐

冻结契约（lessons §1）：

```c
struct vfs_file_ops {
    int  (*open)(struct vnode *v, struct file *f, uint32_t flags);
    int  (*close)(struct file *f);
    long (*read)(struct file *f, void *ubuf, uint64_t len, uint64_t *done);
    long (*write)(struct file *f, const void *ubuf, uint64_t len, uint64_t *done);
    long (*ioctl)(struct file *f, uint64_t cmd, void *uarg);
    int  (*stat)(struct vnode *v, struct vfs_stat *out);
};
```

本实现 `kernel/arch/x86_64/vfs64.h` 中的对应定义：

| 契约槽位 | 本实现签名 | 对齐情况 |
|---|---|---|
| open | `int (*open)(struct vfs_node *v, struct vfs_file *f, uint32_t flags)` | 一致（类型名换成本树命名） |
| close | `int (*close)(struct vfs_file *f)` | 一致 |
| read | `long (*read)(struct vfs_file *f, void *ubuf, uint64_t len, uint64_t *done)` | 一致 |
| write | `long (*write)(struct vfs_file *f, const void *ubuf, uint64_t len, uint64_t *done)` | 一致 |
| ioctl | `long (*ioctl)(struct vfs_file *f, uint64_t cmd, void *uarg)` | 一致 |
| stat | `int (*stat)(struct vfs_node *v, struct vfs_stat *out)` | 一致 |

### 1.2 四条铁律落地情况

| 铁律 | 落地方式 | 位置 |
|---|---|---|
| 1. ubuf 永远是用户态指针，内核负责校验与拷贝；ops 实现内禁止直接解引用 | vfs64 kernel-side 入口（`vfs64_read/write`）先把用户缓冲拷到内核缓冲，再调 ops；ops 收到的是内核缓冲。后端源码内不出现 `*(uint32_t*)ubuf` 这类解引用。静态自检见 §7 | `vfs64.c`、`vfs64_ext2.c`、`vfs64_dev.c` |
| 2. 短读写必须走 `*done`；返回值只表成功/失败 | read/write ops 把实际字节写 `*done`；入口按 `*done` 回写用户态并推进 fd 游标 | `vfs64.c: vfs64_read/write` |
| 3. 字段只增不改 | v1 结构体已冻结；后续新能力走 ioctl 或在结构体尾部追加函数指针，不改既有槽位 | `vfs64.h` |
| 4. ops 表编译期常量 | 两个后端的 ops 表均为 `static const struct vfs_file_ops`，不允许运行时替换 | `vfs64_ext2.c: vfs64_ext2_ops`、`vfs64_dev.c: vfs64_dev_ops` |

### 1.3 v1 拷贝边界的一处显式取舍（与字面契约的差异）

契约文字写「ops 里的 ubuf 永远是用户态指针」。v1 把**拷贝边界上移到 VFS 入口**：
ops 收到的 `ubuf` 形参实际是入口 `kmalloc` 出来的内核缓冲，形参名保留 `ubuf` 仅为
对齐契约文本。安全语义与铁律 1 等价——后端永远拿不到用户态地址、也无法直接解引用；
区别只是拷贝动作集中在入口一次，而不是每个后端各做一遍。若后续要严格回到「ops 收
用户指针、后端各自 `copy_from_user`」，需要改契约而不是改后端。

---

## 2. 结构说明（node / ops / mount）

### 2.1 `struct vfs_node`（一次 lookup 的产物）

| 字段 | 含义 |
|---|---|
| `type` | `VFS_DIR / VFS_FILE / VFS_CHR / VFS_BLK`（对应 Linux S_IFDIR/REG/CHR/BLK 的 v1 子集） |
| `size` | 节点大小（字节） |
| `ino` | ext2 inode 号；伪设备为 0 |
| `path[256]` | 解析时的路径快照 |
| `ops` | 指向后端 `const vfs_file_ops` 表 |
| `be` | 回指所属后端（v1 未使用，预留） |
| `priv` | 后端私有指针：ext2 v1 不用；伪设备用它区分节点 |

### 2.2 `struct vfs_file`（打开后的句柄）

内嵌 `vfs_node node`，另带 `offset`（打开/调用时刻的读位置快照）、`flags`、`priv`。
**v1 的读位置权威仍在 syscall 层 fd 表**（`fs_sys.h` 的 `fs_fd_info_t{path, mode, offset}`），
VFS 句柄是一次调用的临时快照；这是 ext2 路径式 API 与 fd 句柄模型之间的主要折中，
见 §8 风险。

### 2.3 `struct vfs_backend` / `struct vfs_mount`

- 后端注册 `name`、`lookup(path, node)`，以及可选的**路径式变更方法**（S4 起追加
  `create`；S5/S6 追加 `iterate/mkdir/rmdir/unlink`）。这些方法不依附已打开句柄，
  属于后端对路径的操作，因此放在后端结构而不是冻结的 `vfs_file_ops` 表上；方法指针
  全部可选（NULL = 后端不支持，syscall 层回退直调）。后端不允许自己注册 syscall
  （lessons §1 落地步骤 4）。
- 挂载点表 `g_mounts[]`（上限 8），`vfs64_mount(prefix, be)` 登记。
- 路径解析 `vfs64_lookup(path, node)`：**最长前缀匹配**。`/dev` 匹配 `/dev` 与
  `/dev/nullread`，不匹配 `/devx`；`/` 兜底根后端。匹配到后端后调其 `lookup`。

### 2.4 kernel-side 入口（用户指针校验/拷贝的落实点）

| 入口 | 职责 |
|---|---|
| `vfs64_init()` | 幂等注册内置后端：`/` → ext2；`/dev` → 伪设备 |
| `vfs64_open(path, flags, f)` | lookup + ops->open |
| `vfs64_read(f, ubuf, len, done)` | 内核缓冲中转 → ops->read → copy_to_user 回写；`*done` 回报字节数 |
| `vfs64_write(f, ubuf, len, done)` | copy_from_user 拷入 → ops->write；`*done` 回报字节数 |
| `vfs64_close(f)` | ops->close |
| `vfs64_stat(path, out)` | lookup + ops->stat |
| `vfs64_read_path / write_path` | 按路径 + 指定偏移读/写（不回写 fd 游标，语义对齐 pread） |

---

## 3. ext2 后端映射表（`vfs64_ext2.c`）

| VFS ops / 动作 | 底层 `ext2_64` 函数 | 说明 |
|---|---|---|
| lookup | `ext2_64_is_mounted()` + `ext2_64_lookup(path)` + `ext2_64_stat_ino(path, &sz, &isdir, &ino)` | `ext2_64_lookup` 返回 **uint32 inode 号**（0 = 未命中），不是句柄；已核源码确认 |
| create（S4，后端方法） | `ext2_64_create_file(path)` | O_CREAT 新建普通文件；0 成功 / -1 失败 |
| open | 无状态 | ext2 API 按路径调用，v1 不持有打开句柄 |
| close | 无状态 | 同上 |
| read | `ext2_64_read_file_offset(path, buf, size, offset)` | 按 fd offset 定位；短读经 `*done` 回报 |
| write | `ext2_64_write_file_offset(path, buf, size, offset)` | 同上 |
| stat | `ext2_64_stat_ino` | 填 `vfs_stat`：dev=1、ino=真实 ino、mode 目录 0755/文件 0644、size、blksize=4096、blocks 按 size/512 上取整 |
| ioctl | v1 未实现，返回 -1 | deferred |

### 3.1 stat 字段填法与「拿不到就置 0」清单

| `vfs_stat` 字段 | 取值 | 备注 |
|---|---|---|
| dev | 1 | v1 固定值 |
| ino | ext2 真实 inode | 不能给 0，否则 ld.so 会把不同对象认成同一个 |
| nlink | 1 | ext2 链接计数 v1 未取 |
| mode | 目录 0x41ED（0755）/ 文件 0x81A4（0644） | 由 `isdir` 判定 |
| uid / gid | 0 | 系统无 uid/gid 概念 |
| size | ext2 inode 大小 | 直取 |
| blksize | 4096 | 固定 |
| blocks | `(size + 511) / 512` | 按 512B 块估算 |
| mtime | **0** | ext2 inode 时间戳 v1 未解析；置 0 表示「无此信息」，不伪造 epoch 时间戳 |

---

## 4. 已走 VFS / 未走 VFS 的 syscall 清单

### 4.1 已接入 VFS 路由（未命中挂载后端时回退 ext2 直调）

| syscall / 内部函数 | 路由方式 | 对外语义保持 |
|---|---|---|
| `fs_do_read`（`fs_rw_sys.c`） | 先 `vfs64_lookup`；命中走 `vfs64_read_path`，未命中回退 `ext2_64_read_file_offset`；回写 fd 游标口径不变 | 返回字节数、EOF=0、失败口径不变；fd 号分配与 stdio 0/1/2 槽位未动 |
| `fs_do_write`（`fs_rw_sys.c`） | 同上，走 `vfs64_write_path`；未命中回退 `ext2_64_write_file_offset` | 只读打开拒写、offset 推进口径不变 |
| `fs_do_pread`（`fs_rw_sys.c`） | 同上，走 `vfs64_read_path`；不回写 fd 游标（与原 pread 语义一致） | 同上 |
| `sys_stat` / `sys_fstat`（`fs_stat_sys.c`） | 新增 `resolve_stat`：先 `vfs64_stat`，未命中回退 `ext2_64_stat_ino`；再由既有 `fill_stat` 按 Linux x86_64 偏移填充用户态缓冲 | stat 布局、错误码（EFAULT/ENOENT）不变 |
| `fs_do_open`（`fs_open_sys.c`，S4） | 非 O_CREAT：`vfs64_open` 走后端 open 钩子；O_CREAT：`vfs64_create` 分派后端 create，未挂载/后端无 create 回退 `ext2_64_create_file`。**fd 分配顺序不变**：先 `fs_fd_alloc` 占槽，再建文件 | fd 号 ≥3、stdio 0/1/2 保留槽位不变；「先占 fd 再建文件」顺序保留（建文件失败 fd 仍占着，已知缺口，fdstress 不测）；open 不存在文件**不做存在性检查、仍分配 fd**（旧行为，未引入 ENOENT，待后续补） |

### 4.2 尚未走 VFS（保持原 ext2 直调，未破坏现有功能）

| 路径 | 现状 | 原因 / 归属 |
|---|---|---|
| 目录枚举 readdir/getdents | 终端 `ls` 与文件管理器在 `hs_apps.c` 直调 `ext2_64_list_dir_entries(_ex)`；64 位树尚无 syscall getdents 路径 | 枚举后端接口（dirent 布局）尚未在 ops 表定义；deferred（S5） |
| `sys_unlink / sys_mkdir / sys_rmdir`（`fs_dir_sys.c`） | 直调 ext2 删除/建目录 | 同上，目录变更接口未纳入 v1 ops |
| `ioctl` | ops 表有槽位，ext2 后端返回 -1，伪设备留 NULL | deferred |
| `mmap` 文件映射 | 未接入 | deferred，依赖页缓存与映射语义设计 |
| 旧 `devfs64.c`（null/zero/console，旧签名） | 未接进 open 路径，与本层互不替代 | 旧签名迁移是 deferred 项；新伪设备见 §5 |

> 说明：v1 不把「半接线」写成全部接完。上表 4.2 任何一条被调用时，行为与改动前一致。

---

## 5. 只读伪设备后端（解耦验收点）

### 5.1 节点定义（`vfs64_dev.c`，挂 `/dev`）

| 节点 | 行为 | 实现的 ops |
|---|---|---|
| `/dev/nullread` | read 恒返回 EOF（`*done = 0`） | open / read / close |
| `/dev/vfsdemo` | read 返回固定内容 `"HEPU-VFS-DEMO\n"`（14 字节） | open / read / close |

`write / ioctl / stat` 置 NULL：v1 不提供，入口有空指针保护。

### 5.2 验收标准对照

lessons §1 验收：**新增一个只读设备，只需实现 open/read/close 三个函数，不改任何调用方
代码。** 本后端的落地方式：

- 后端文件 `vfs64_dev.c` 自包含，没有改任何 syscall 分发代码；
- 探针 `user/apps/vfsprobe.c` 走标准 `open/read/close` 系统调用打开这两个节点，
  不经任何 VFS 专属入口；
- 注册只是 `vfs64_init()` 里一行 `vfs64_mount("/dev", &vfs64_be_dev)`。

### 5.3 运行时输出（本轮 QEMU 串口取证，见 §9）

```
[VFSP] vfs64 pseudo-device probe
[VFSP] vfsdemo read=14 bytes: HEPU-VFS-DEMO
[VFSP] nullread read=0 bytes (expect 0)
[VFSP] RESULT: PASS
```

---

## 6. 与旧 devfs64 / 32 位 vfs.c 的关系

- `kernel/arch/x86_64/devfs64.c`：旧 `devfs_ops_t` 用 `uint32 offset / void* buf / uint32 size`
  旧签名，且未接进 open 路径。v1 **不替代**它；迁移到新 ops 表列入 deferred。
- `kernel/fs/vfs.c`：32 位遗留，不在 64 位 `C_SOURCES` 显式列表内；不复用、不改动。

---

## 7. 静态自检：后端内 0 处直接解引用 ubuf

### 7.1 检查命令

在仓库根执行：

```bash
# 列出两个后端文件里所有 ubuf 出现位置，人工核对是否为直接解引用
grep -n 'ubuf' kernel/arch/x86_64/vfs64_ext2.c kernel/arch/x86_64/vfs64_dev.c

# 直接解引用模式扫描（*ubuf / ubuf[ / 强转后取成员）；排除函数形参声明行
grep -nE '\*ubuf|ubuf\[|\([^)]*\)\s*ubuf\s*->' \
  kernel/arch/x86_64/vfs64_ext2.c kernel/arch/x86_64/vfs64_dev.c \
  | grep -vE 'void \*ubuf|const void \*ubuf'
```

### 7.2 结果

- 解引用模式扫描（排除形参声明）：**0 处**。
- 人工复核全部 `ubuf` 出现点：
  - `vfs64_ext2.c`：`ubuf` 仅被强转成 `uint8_t*` / `const uint8_t*` 后传给
    `ext2_64_read/write_file_offset`；后端自身不解引用。
  - `vfs64_dev.c`：`memcpy(ubuf, DEMO_MSG, n)` 整缓冲拷贝；源码内无下标/指针解引用。
- 用户态缓冲的 `copy_to_user / copy_from_user` 只出现在 `vfs64.c` 入口层，后端不接触。

> 备注：`memcpy` 是批量拷贝原语，不是 `*(uint32_t*)ubuf` 这类按类型直接解引用；
> 契约禁止的是后者（绕过校验按用户类型强解），整缓冲拷贝在入口层已完成校验后由后端使用。

---

## 8. 已知风险与未接线范围

### 8.1 v1 残留折中

1. **fd 表仍存路径字符串 + 偏移**：VFS 没有真正的打开句柄对象，每次 read/write
   都重新 lookup。并发场景下同一 fd 的路径在 close 后被复用是既有 fd 表语义，VFS
   不放大也不缩小该风险。
2. **回退双路径**：未命中 VFS 后端的请求仍走 ext2 直调。好处是接线期不破坏旧功能；
   代价是同一条 read 存在两条代码路径，排查时要先判断走了哪条。
3. **无后端句柄持有**：ext2 后端 open/close 为空操作；后续若后端需要在 open 时
   分配上下文（如缓存句柄），`vfs_file.priv` 已预留。

### 8.2 明确不属本次改动、不在本次归因闭环的问题

本轮取证在全部探针通过后，出现一次内核异常：

```
[EXCEPTION] vector=0xE error=0x0 rip=0x1142E5
  CR2=0x81083810 (not present) read
*** HEPU PANIC: kernel-mode exception ***
```

- 反汇编定位：`rip=0x1142E5` 落在 `e1000_send`（`kernel/arch/x86_64/e1000_64.c`），
  故障指令解引用网卡 MMIO 寄存器；异常前串口为 `[DHCP64] Sending Discover...`。
- 触发链：DHCP 定时发包 → 网络 HAL → `e1000_send` → 访问未恒等映射的 MMIO 页。
- 归属判断：本次 P3-6 改动文件（`vfs64.*`、`vfs64_ext2.c`、`vfs64_dev.c`、
  `fs_rw_sys.c`、`fs_stat_sys.c`、`syscall64_full.c` 初始化调用）与网卡驱动/
  网络栈无数据共享；异常地址是 MMIO 物理地址区间，不是 VFS 堆缓冲。**未在干净基线上
  复跑对照**（并行 agent 仍在改动桌面/认证相关文件，无法在此刻冻结基线），因此本文
  不把它判为 VFS 引入，也不判为已排除；登记给网络/驱动侧跟进。复跑对照与修复不在
  P3-6 范围内。

### 8.3 deferred 项（按优先级）

1. **其他 FS 后端**：ext4 / exFAT / NTFS 接入 64 位主线（当前仅有 32 位遗留源码）。
2. **目录枚举 ops**：在 ops 表定义 dirent 枚举接口，把 `hs_apps` 的直调与未来
   syscall getdents 收进来。
3. **目录变更**：unlink/mkdir/rmdir 走后端 ops。
4. **ioctl 转发**：设备特有控制命令的统一通道。
5. **mmap 文件映射**：页缓存 + 映射语义设计。
6. **写回时序与一致性**：分段写的缓存刷盘时机、崩溃一致性。
7. **并发锁粒度**：v1 读位置维护沿用 syscall 层既有口径；VFS 层尚无独立锁，
   多后端并发访问时需要明确锁层级。
8. **旧 devfs64 迁移**：把 null/zero/console 迁到新 ops 表，替换旧签名。

---

## 9. 验收证据

### 9.1 编译（64 位，`make -C kernel/arch/x86_64`）

- 三个 VFS 目标以 `-Wall -Wextra` 重编：**0 warning / 0 error**，链接 rc=0。
- 产物大小（S4 本轮实测）：
  - `build_x86_64/hepu-kernel64.bin`：**982 256 字节**
  - `build_x86_64/hepu-kernel64.elf`：**1 142 152 字节**
  - 目标文件：`vfs64.o` 9 008 B、`vfs64_ext2.o` 5 944 B、`vfs64_dev.o` 4 088 B、`fs_open_sys.o` 2 376 B
- 探针：`user/build/vfsprobe.elf` **7 408 字节**。
- 编译告警：64 位 `-Wall -Wextra` 重编 **0 warning / 0 error**，链接 rc=0。

### 9.2 64 位 QEMU 运行串口摘要（本轮取证实例）

启动链无早期崩溃：

```
[BOOT] step1 done
[Hepu Desktop] booting to lock screen
[DESKTOP64] Desktop running with mouse support
```

既有回归程序执行痕迹：

```
[USER64] exec: /bin/hello.elf
[USER64] exec: read 6904 bytes
  Hello World from ELF64 user space!
[UPROC] exec path=/bin/hello.elf rc=100
SEC-TEST DONE
[UPROC] exec path=/bin/comptest.elf rc=100
```

本轮探针结果：

```
[STATP] struct stat layout probe
[STATP] RESULT: PASS                       ← sys_stat/fstat 走 VFS 路由 + 回退路径正常

[VFSP] vfs64 pseudo-device probe
[VFSP] vfsdemo read=14 bytes: HEPU-VFS-DEMO
[VFSP] nullread read=0 bytes (expect 0)
[VFSP] RESULT: PASS                        ← 伪设备 open/read/close 三 ops 验收

[FDSTRESS] threads done, rounds/thread=400
[FDSTRESS] open_fail=0
[FDSTRESS] close_fail=0
[FDSTRESS] bad_range=0
[FDSTRESS] conflict=0
[FDSTRESS] leak_probe_fd=3  (no leak)
[FDSTRESS] RESULT: PASS                    ← fd 表分配/释放/保留槽位未被破坏
```

S4 本轮追加探针（`[VFSP4]`，QEMU 串口实测）：

```
[VFSP4] open routing probe
[VFSP4] open(/bin/hello.elf) fd=3          ← 既有文件打开，fd≥3（stdio 保留）
[VFSP4] hello read=64 bytes
[VFSP4] open(O_CREAT|WR /vfs4_new.txt) fd=3
[VFSP4] write=12 bytes
[VFSP4] reopen fd=3
[VFSP4] read back=12 bytes                 ← 新建文件写后读回逐字节一致
[VFSP4] open(missing) fd=3 (old impl: allocated, no ENOENT)
[VFSP4] reuse open fd=3 (expect 3)         ← close 后 fd 可复用，无泄漏
[VFSP] RESULT: PASS
```

同轮既有回归：`[STATP] RESULT: PASS`、`[FDSTRESS] open_fail=0 close_fail=0 bad_range=0
conflict=0 leak_probe_fd=3 RESULT: PASS`；`hello.elf` read 6904 B、`comptest.elf` rc=100、
`SEC-TEST DONE`；桌面 `[DESKTOP64] Hepu Desktop started`，全程无 EXCEPTION/PANIC。

> 注：`ls /` 与文件管理器列表走的是 §4.2 未接线的 readdir 直调路径；本轮停机后
> 帧缓冲已黑屏，目录列表的窗口截图未留存。该路径代码本次未改，行为同基线。

### 9.3 32 位回归（`bash tools/regress32.sh`）

- 前置：`git log` 中已有 32 位链接修复提交（`c28d4d9 fix(regress32): 修复 32 位内核
  链接失败（auth64/rtc64 兼容层）`），基线已就绪。
- 结果：**PASS**，判据 `serial=1 initrd=1 net=1 desk=1 fault=0 bound=1 lines=632`（S4 复跑：`serial=1 initrd=1 net=1 desk=1 fault=0 bound=1 lines=893`）：

```
[SERIAL] COM1 16550 UART ready (115200 8N1)
[INITRD] base=0x0044E55D size=0x000490E8 head=0x55504548 tail=0x00000000
[NET-TEST] === Self-Test Complete ===
[DESKTOP] elastic free desktop init done
```

- 本次未改根 Makefile、未改 32 位树；32 位回归通过与本次 VFS 改动互不影响。

---

## 10. 分步拆解（若继续推进，每步验收什么、风险是什么）

| 步骤 | 内容 | 验收标准 | 风险 |
|---|---|---|---|
| S1（已完成） | VFS 骨架：node/ops/mount/最长前缀解析 + kernel 入口拷贝边界 | 可编译；lookup/read_path/write_path/stat 链路可调用；静态自检 0 解引用 | 句柄模型与路径式 API 的折中（§8.1） |
| S2（已完成） | ext2 后端封装 + read/write/stat 路由带回退 | hello/statprobe/fdstress 回归通过；stat ino 真实 | 双路径排查成本；回退分支长期滞留 |
| S3（已完成） | 只读伪设备三 ops 样例 + 探针 | 不改 syscall 代码即可挂设备；vfsprobe PASS | 无（样例后端，不承载真实设备） |
| S4（已完成） | open 路由：后端 create/open 语义入分派；fd 表与 VFS 句柄对齐 | fdstress 全绿；既有文件读回、O_CREAT 新建写读回、不存在文件旧行为、close 后 fd 复用均对齐；regress32 fault=0 | 「先占 fd 再建文件」顺序保留（失败占槽已知缺口）；open 缺失文件尚未返回 ENOENT |
| S5 | 目录枚举 ops + getdents syscall | 终端 ls / 文件管理器列表改走 VFS；目录项布局与 ext2 直调结果一致 | dirent 结构跨后端统一；树状遍历的路径快照生命周期 |
| S6 | 目录变更 ops（unlink/mkdir/rmdir） | 目录三件套走后端；失败回滚语义明确 | 写操作一致性、权限校验挂接点（接 P3-2 权限收口） |
| S7 | ioctl / mmap / 页缓存 | 设备控制命令有统一通道；文件映射可工作 | mmap 与缓存一致性是大改动，需独立设计评审 |
| S8 | 其他 FS 后端（ext4/exFAT/NTFS） | 新后端只注册 ops 表即可被 open/read 复用 | 各 FS 语义差异、元数据缓存 |

---

## 11. 进度结论

### S4 收尾（open 路由含 O_CREAT）

- **本阶段切了哪些 syscall**：`fs_do_open`（非创建走后端 open 钩子；O_CREAT 走后端
  create 分派，带回退）。read/write/pread/stat 维持 S2 路由不变。
- **探针断言**：`[VFSP4]` 5 组 —— 既有文件 fd≥3 且读回 >0；O_CREAT|WR 新建写 12 B、
  重开读回 12 B 逐字节一致；不存在文件按旧行为分配 fd（无 ENOENT）；close 后首槽
  复用为 3。同轮 `[STATP]` PASS、`[FDSTRESS]` open_fail=0/close_fail=0/leak_probe_fd=3。
- **regress32 判据行**：`serial=1 initrd=1 net=1 desk=1 fault=0 bound=1 lines=893`。
- **未做项与风险**：open 缺失文件仍不返回 ENOENT（对齐旧实现，待后续补）；「先占 fd
  再建文件」建失败占槽是已知缺口；readdir/getdents、目录三件套、ioctl、mmap 仍直调。

### 累计状态

- **已落地**：vfs64 骨架、ext2 后端、只读伪设备后端；read/write/pread/stat/open 经
  VFS 路由（未命中回退 ext2 直调）；探针与既有回归在 64 位 QEMU 通过；32 位 regress32 PASS。
- **未全线切换**：readdir/getdents、目录三件套（unlink/mkdir/rmdir）、ioctl、mmap
  仍保持原直调路径（§4.2）。
- 对应 ROADMAP 状态：**进行中（open 已走 VFS，枚举/目录变更待续）**，不标已完成。
