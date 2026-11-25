# P3 KUnit 无盘冒烟两项失败修复（2026-11-26）

## 结论

`s3_normal_user_denied` 与 `posix_error_table` 的两条失败断言，都是测试内
`vfs_open(path, VFS_O_CREAT | VFS_O_WRONLY)` 返回负值导致的 `KUNIT_ASSERT create ...`。
失败并非权限位或错误码断言失效，而是 **无 ext2 后端** 时 `vfs_open` 的建文件路径直接
返回 `VFS_EIO`：

```c
/* kernel/fs/vfs.c: vfs_open() */
if (!ext2_mounted) return VFS_EIO;
```

32 位启动冒烟是 multiboot 直启内核，没有挂 IDE 系统盘，串口明确记录：

```text
[EXT2] selftest: no disk, skip
[VVRAM] auto-mount: failed to read MBR (IDE not ready)
```

因此测试前面的 `mkdir /data/p3sec`、`chmod 0777`、`chown uid1`、切普通用户都通过，
到 `O_CREAT` 才失败。

## 修复

1. `kernel/kunit/kunit_p3_security.c`：两个用例在清理后检查 `ext2_mounted`；为 0 时
   返回 `KUNIT_SKIP`，并在串口输出 `[KUNIT-SKIP] p3-security/<case>`。没有删除任何
   安全断言，也没有把 SKIP 计成 PASS。
2. `tools/regress/smoke32.sh`：把 `[KTEST] RESULT` 纳入冒烟 PASS 条件，并统计
   `skip_cases`。原脚本只打印 KTEST 结果、不参与退出码判定，导致 KUnit 失败时
   “六项通过”仍显示冒烟 PASS。

## 验证

### 无盘 32 位启动冒烟（官方入口）

命令：`bash tools/regress/smoke32.sh 70`

```text
serial=1 initrd=1 net=1 desk=1 fault=0 lines=110969
ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=2
[KUNIT-SKIP] p3-security/s3_normal_user_denied (ext2 backend unavailable)
[KUNIT-SKIP] p3-security/posix_error_table (ext2 backend unavailable)
[KTEST] RESULT: ALL PASS
```

证据：`smoke32-diskless-excerpt.log`、`smoke32-diskless-summary.txt`。

### 带 ext2 分区的 32 位运行（证明两项用例本身可通过）

命令要点：

```text
bash tools/make_single_img64.sh
qemu-system-i386 -kernel build_v250/kernel.elf -m 256 -display none \
  -drive file=/tmp/hepu64_single.img,format=raw,if=ide -serial file:.p3_disk2.log
```

```text
[EXT2] selftest: mounted (ext2 fs on IDE disk)
[KUNIT] suite: p3-security (0x00000002 tests)
s3_normal_user_denied ... PASS
posix_error_table ... PASS
[KUNIT] result: 2 passed, 0 failed, 0 skipped
[KTEST] RESULT: ALL PASS
```

证据：`kunit-ext2-excerpt.log`。

## 边界

- 无盘冒烟中两项显示 **SKIP**，不是安全断言通过；其覆盖由带 ext2 盘的 32 位运行补齐。
- `smoke32.sh` 现在会因 KUnit 失败而失败；其余 KUnit 用例若在无盘环境失败，会直接暴露。
- `p3_cleanup` 仍只按 VFS 可见节点清理；ext2 中残留但 VFS 不可见的目录项不在本次修复范围。
