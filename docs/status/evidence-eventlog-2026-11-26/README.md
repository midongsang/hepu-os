# 统一事件日志 / 服务层证据（2026-11-26）

分支：`feat/eventlog-system-2026-11-26`
基线：`58d772d`

## 修改范围

- `kernel/include/event_log.h`
- `kernel/shared/log/event_log.c`
- `kernel/arch/x86/klog.c`（32 位平台适配 + legacy 文本环同步写事件）
- `kernel/arch/x86/kernel.c`（启动初始化与落盘）
- `kernel/arch/x86_64/klog64.c`（64 位平台适配 + legacy 文本环同步写事件）
- `kernel/kernel_main_64.c`（启动初始化、early save、桌面 save）
- `kernel/sched/services.c`（service 事件、64 位 crash_ctx 桩）
- `kernel/sched/shell_cmd_system.c` / `shell_execute.c` / `shell_help.c`（`journal`/`log`/`service`）
- `kernel/arch/x86_64/shell64.c`（`journal`/`log`/`service`）
- `kernel/arch/x86_64/Makefile`（共享 event_log 与 services 编入 64 位）
- `kernel/kunit/kunit_event_log.c`、`kernel/kunit/kunit_system_manager.c`（新增套件）
- `docs/design/EVENT_LOG_AND_SERVICE_LAYER.md`、`docs/status/SYSTEM_TOOLS.md`

## 32 位无盘冒烟

命令：

```bash
cd <worktree>
HEPU_EV=/tmp/hepu-ev-eventlog3 bash tools/regress/smoke32.sh 70
```

结果见 `smoke32-summary.txt`：

```text
serial=1 initrd=1 net=1 desk=1 fault=0 lines=70011
ktest_result=PASS ktest=1 fail_suites=0 fail_cases=0 skip_cases=2
```

新增套件：

```text
[KUNIT] suite: event-log (0x00000002 tests)
[KUNIT] suite: system-manager (0x00000001 tests)
[KTEST] RESULT: ALL PASS
```

p3-security 两项仍按无 ext2 显式 SKIP，未改成 PASS：

```text
[KUNIT-SKIP] p3-security/s3_normal_user_denied (ext2 backend unavailable)
[KUNIT-SKIP] p3-security/posix_error_table (ext2 backend unavailable)
```

无盘时落盘明确失败：

```text
[EVENT] save skipped (no ext2)
```

## 32 位带 ext2 盘

命令（直接内核 + 带 ext2 根分区镜像，无 snapshot，确保文件真正落盘）：

```bash
qemu-system-i386 -L <datadir> -kernel build_v250/kernel.elf -m 256 -smp 1 \
  -display none -drive file=/tmp/hepu64_single.img,format=raw,if=ide,file.locking=off \
  -serial file:/tmp/event32_disk2.log -no-reboot
```

串口证据：

```text
[EXT2] selftest: mounted (ext2 fs on IDE disk)
[KUNIT] suite: event-log (0x00000002 tests)
[KUNIT] suite: system-manager (0x00000001 tests)
[KTEST] RESULT: ALL PASS
[EVENT] saved to /hepu_events.log
```

文件证据：把镜像 LBA13000 起的 ext2 分区抽出后：

```bash
dd if=/tmp/hepu64_single.img of=/tmp/hepu32_ext2_current.img bs=512 skip=13000 count=$((224*2048))
debugfs -R 'cat /hepu_events.log' /tmp/hepu32_ext2_current.img
```

`event32-hepu_events.log` 是实际文件内容（过滤掉 boot_gfx 屏幕快照中的替换字符行，保留结构事件）。
其中 `service` 注册/启动事件与 `kunit` 自检事件共存于同一序号空间。

## 64 位 UEFI

镜像由 `HEPU_EV=/tmp/hepu-ev-eventlog-build bash tools/regress/build_all.sh` 生成；
QEMU 用 OVMF pflash 启动、**不加 snapshot**，否则写入会被 QEMU 丢弃：

```bash
qemu-system-x86_64 -L <datadir> -rtc base=localtime \
  -drive if=pflash,format=raw,readonly=on,file=<OVMF_CODE_4M.fd> \
  -drive if=pflash,format=raw,file=/tmp/OVMF_VARS_eventlog2.fd \
  -drive format=raw,file=/tmp/hepu-ev-eventlog-build/build/hepu.img,file.locking=off \
  -m 1024 -smp 1 -display none -serial file:/tmp/event64_disk2.log -no-reboot
```

串口证据：

```text
=== HepuOS 64-bit Kernel booting ===
[SERVICES] service manager init done
[SERVICES] boot services started: 5/5
[EVENT] early save to /hepu_events.log
[EVENT] saved to /hepu_events.log
```

文件证据：ext2 分区从 LBA264192 起：

```bash
dd if=/tmp/hepu-ev-eventlog-build/build/hepu.img of=/tmp/hepu64_uefi_ext2c.img bs=512 skip=264192 count=245760
debugfs -R 'cat /hepu_events.log' /tmp/hepu64_uefi_ext2c.img
```

`event64-hepu_events.log` 显示 64 位同一序号空间内的 service 事件和 `kernel64` 启动事件。

## 构建

```text
make -j2                                  # 32 位通过
make -C kernel/arch/x86_64 -j2            # 64 位通过
python3 tools/check_docs.py               # warns=0 errors=0
```

## 边界（不当作已完成）

- `dmesg` 仍读 legacy klog/klog64，尚未切到 event_log；
- 落盘是每次覆盖 `/hepu_events.log`，没有追加、轮转和用户态 logd；
- `service start` 的内置 network/audio/bluetooth/wifi/gui 仍是空壳，成功只代表状态机接受；
- `mount/umount`、`chmod/chown`、`useradd/userdel` 等系统管理命令仍未实现；
- 事件环 256 条、消息 144 字节，异常上下文仍使用普通自旋锁。
