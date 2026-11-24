# P3 安全最小闭环：VFS 权限、错误码与 32 位账户

## 1. 目标与边界

P3 不做完整的登录、OOBE、持久化账户库，只把“普通用户不能写系统目录”这条
路径打通，并让 VFS 对上层返回可判别的 POSIX 错误码：

- VFS 变更操作（open/write/mkdir/unlink/rmdir/rename/chmod/chown）统一过权限检查；
- 32 位账户从 `auth64_compat32.c` 的空桩变成 root + 1 个普通用户；
- `fd → 路径` 可查，方便权限复查与诊断；
- 64 位 `open` 未命中且无 `O_CREAT` 返回 `ENOENT`，不再先占 fd 再失败。

不在本闭环内：

- 账户持久化（`/account.conf`）与登录界面；
- 共享 `shared/capability` 的路径级能力句柄；
- ext2 磁盘 inode 的 uid/gid 写回（当前只更新 VFS 内存节点的 uid/gid）。

## 2. 32 位账户

`kernel/arch/x86/auth64_compat32.c` 接到已有的 `multi_user` 账户表与
`sys_user` 密码哈希表：

| 账户 | multi_user id | uid | 角色 | 权限 |
|---|---:|---:|---|---|
| root/机主 | 0 | 0 | developer(2) | 全部 VFS 权限 |
| 普通用户 | 1 | 1 | normal(1) | 读放行；写/建/删仅限可写根 |

`multi_user_init()` 现在固定创建这两个账户。`hepu_current_uid()` 是 VFS 的
唯一 uid 来源：`multi_user` 当前用户 id 即 uid，默认 0(root)。

密码校验复用 `sys_user.c` 的 SHA-256 哈希表；`auth64_create_account()` 只允许
创建普通用户，root 不通过该接口创建。未设密码时校验返回成功，与
`sys_verify_password` 的既有语义一致。

## 3. VFS 权限模型

`kernel/fs/vfs.c` 使用两层检查：

1. **VFS 能力剖面**：
   - uid 0(root)：全部放行；
   - 普通用户：读放行；写/建/删/改名只允许 `/data`、`/tmp`、`/app`、`/home`
     及其子路径。
2. **POSIX inode 权限位**：
   - 按 `node->uid == uid` 取 owner 位，否则按 `gid == uid` 取 group 位，
     最后取 other 位；
   - `write` 要 W，`mkdir/unlink/rmdir/rename` 要父目录 W+X。

检查顺序很重要：父目录不存在时，普通用户对 `/system/...` 的写请求先被能力
剖面拒绝，返回 `EACCES` 而不是先暴露 `ENOENT`。这是 S3 的验收语义。

`vfs_open()` 新增：

- 无 `O_CREAT` 且节点不存在 → `VFS_ENOENT`；
- `O_CREAT` 先检查父目录 W+X，再落 ext2；
- 已有文件按读/写标志检查 inode 权限；
- 目录以写方式打开 → `VFS_EISDIR`；
- fd 表满 → `VFS_EMFILE`。

`vfs_write()`/`vfs_read()` 对 fd>=3 再复查一次能力与 inode 权限，防止
“open 后切换用户”绕过；0/1/2 标准流不按路径检查。

## 4. 错误码

`kernel/include/vfs.h` 新增 VFS 对外错误码：

| 名称 | 数值 | 场景 |
|---|---:|---|
| `VFS_ENOENT` | -2 | 节点/父目录不存在 |
| `VFS_EIO` | -5 | 后端失败且无法细分 |
| `VFS_EBADF` | -9 | 非法 fd |
| `VFS_ENOMEM` | -12 | 节点分配失败 |
| `VFS_EACCES` | -13 | 能力/权限拒绝 |
| `VFS_EBUSY` | -16 | 删除根目录 |
| `VFS_EEXIST` | -17 | mkdir 目标已存在 |
| `VFS_ENOTDIR` | -20 | 父路径不是目录 |
| `VFS_EISDIR` | -21 | 对目录做 unlink/写打开 |
| `VFS_EINVAL` | -22 | 空路径/非法参数 |
| `VFS_EMFILE` | -24 | fd 表满 |
| `VFS_ENOSPC` | -28 | inode/块分配失败 |
| `VFS_ENOTEMPTY` | -39 | rmdir 非空目录 |

ext2 建文件/建目录在 inode 表或块分配失败时返回 `-28`，VFS 原样上抛；
其余后端 `-1` 统一映射 `VFS_EIO`。`vfs_test_inject_enospc()` 是
`HEPU_BOOT_DEBUG` 下的测试缝，只用于验证 ENOSPC 返回路径，不伪造磁盘写满。

## 5. fd→路径

`vfs_fd_t` 增加 `path[VFS_MAX_PATH]`，`vfs_open()` 写入完整路径，
`vfs_close()` 清空；新增 `vfs_fd_get_path(fd, out, outsz)`。
`vfs_dup/dup2` 按结构体复制，路径随 fd 复制。

## 6. 64 位 open ENOENT

`kernel/arch/x86_64/fs_open_sys.c` 的 `fs_do_open()` 在分配 fd 之前先
`vfs64_lookup()`：

- 未命中且无 `O_CREAT` → 返回 `-2`(ENOENT)；
- 未命中且有 `O_CREAT` → 先 create，失败释放 fd 并返回 `-5`(EIO)；
- 已存在且有 `O_CREAT` → 直接打开已有文件，不再把 create 的 EEXIST 当失败。

## 7. 测试与证据

新增 `kernel/kunit/kunit_p3_security.c`，注册 `p3-security` 套件（2 个用例）：

- `s3_normal_user_denied`：
  - 校验 root uid=0/role=2、普通用户 uid=1/role=1；
  - root 建 `/data/p3sec` 并 chmod/chown 给 uid1；
  - 切普通用户后 `vfs_mkdir("/system/p3denied")` 返回 `-13(EACCES)`；
  - `vfs_open("/system/p3denied", O_CREAT|O_WRONLY)` 返回 `-13`；
  - `/data/p3sec` 下建/写/`vfs_fd_get_path`/删均成功；
  - 恢复 root 并清理。
- `posix_error_table`：
  - 覆盖 `EEXIST/ENOENT/EISDIR/ENOTEMPTY/ENOSPC`；
  - `vfs_fd_get_path` 值校验；
  - ENOSPC 走测试缝。

`tools/gate.sh` 实测：

```text
[1/5] 32 位构建 0 error
[2/5] 64 位构建 0 error
[3/5] VFS 跨启动（写阶段 PASS + 读阶段 PASS）
[4/5] kunit 连续 3 次 ALL PASS
[5/5] GATE: PASS
```

`p3-security` 套件每次启动均注册并全过；VFS 写阶段首启 18/18 PASS
（含 `P3.mkdir`、`shell.mv`、`term.edit`）。

## 8. 已知边界

- 普通用户没有登录界面，测试通过 `mu_user_switch(1)` 切换身份；真实桌面默认
  仍以 root 运行。
- 账户密码与角色不落盘，重启回到内置 root+user。
- VFS `chmod/chown` 只改内存节点，未写回 ext2 inode；跨重启后 uid/gid 会回到
  盘上原值（当前建文件为 root）。
- `vfs_cap_check` 是 VFS 路径白名单，不是 `shared/capability` 的资源句柄；
  后续做真正的路径能力对象时替换这一层即可，POSIX 位检查保留。
- 没有实现粘滞位(sticky)、setuid、ACL、umask。
