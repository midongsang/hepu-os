# OPEN-05 emergency 环并入 journal —— 探针边界取证（2026-11-26）

分支：`feat/open05-06-2026-11-26`
基线：`b43a467`

本目录第二份证据：证明 emergency 异常环条目已并入 `journal` 查询输出，并界定探针的
编译期/运行期边界。默认桌面 logd 链路、dated 文件与游标、门禁回归见
[README.md](README.md)。

## 1. 结论

- `event_log_dump()` 在普通环条目之后追加 `---------- emergency ring ----------` 段，
  每条 emergency 条目以 `[EMERG]` 前缀输出；`journal`（`dmesg`）走同一 dump 路径，
  32/64 两侧共用 `kernel/shared/log/event_log.c`。
- 探针条目 `FATAL open05-probe code=99 OPEN-05 emergency journal probe` 仅在
  `EMERG_PROBE=1` 编译的取证内核中编入；默认构建不含该条目。
- 探针写入点位于启动序列公共段（`services_start_boot()` 之后，图形桌面与文本 shell
  两条路径都经过），因此 `hepu.graphics=0` 文本 shell 路径也能取到条目。

## 2. 复现命令

```bash
source tools/regress/env.sh

# 1. 编探针内核（仅取证构建，不覆盖默认镜像）
touch kernel/kernel_main_64.c
make -C kernel/arch/x86_64 EMERG_PROBE=1 -j2

# 2. 单独打包探针镜像（HEPU_IMG 指向本目录 build/probe.img）
HEPU_IMG=<本目录>/build/probe.img python3 tools/build_uefi_dual.py

# 3. 注入账户/系统/显示配置后，hepu.graphics=0 文本 shell 启动取证
bash docs/status/evidence-open05-2026-11-26/boot-probe.sh
```

取证结束后必须 clean 64 位内核并重跑默认全量构建，恢复默认产物。

## 3. 实测原文

原始串口：`serial64-probe/serial-64-probe-raw.txt`（探针镜像，`hepu.graphics=0`
文本 shell，`[INIT] Falling back to text shell`）。

第一次 `journal`（行 457 起）：

```
hepu@os:~$ journal
========== event log (journal) ==========
00000001 ... INFO  boot64  pid=0 code=1 64-bit kernel entry
...
00000016 ... INFO  kernel64 pid=0 code=0 [ACCOUNT] configured, skip OOBE
---------- emergency ring ----------
[EMERG] 00000001 0000000000000000 FATAL open05-probe pid=0 code=99 OPEN-05 emergency journal probe
========== end of event log ==========
```

第二次 `journal -l 0`（行 479 起，级别过滤放开）：

```
hepu@os:~$ journal -l 0

========== event log (journal) ==========
00000001 ... INFO  boot64  pid=0 code=1 64-bit kernel entry
...
00000016 ... INFO  kernel64 pid=0 code=0 [ACCOUNT] configured, skip OOBE
---------- emergency ring ----------
[EMERG] 00000001 0000000000000000 FATAL open05-probe pid=0 code=99 OPEN-05 emergency journal probe
========== end of event log ==========
```

判据：普通环 16 条之后出现独立的 emergency 段分隔行；探针条目以 `[EMERG]` 前缀、
`FATAL` 级别、`open05-probe` 源、`code=99` 输出；两次调用（`journal` 与
`journal -l 0`）段内容一致。

## 4. 探针边界

- **编译期边界**：探针写入块由 `#ifdef HEPU_EMERG_PROBE` 包裹；`EMERG_PROBE=1`
  make 变量在 `kernel/arch/x86_64/Makefile` 转成 `-DHEPU_EMERG_PROBE`。默认构建不带
  该宏，探针块不编入。
- **默认构建无探针条目**：默认内核 bin 的 `strings` 中 `open05-probe` 计数 0
  （`build/default-build-checks.txt`）；默认桌面串口（`serial64/serial-64-default-raw.txt`）
  无 `open05-probe` / `[EMERG]` 条目。
- **不是产品路径**：探针不进统一服务表，不走 `services_start()`；条目仅用于验证
  emergency 环并入 `journal` 的链路，不代表具备真实崩溃现场记录能力。
- **注入点位置**：`kernel/kernel_main_64.c` 公共启动段，原在图形桌面就绪块内（文本
  shell 路径走不到），本阶段上移到图形/文本两路径都经过的位置。

## 5. 未闭环残余

1. emergency 环目前只在 `journal` 输出尾部追加快照段，未做与普通环的 seq 对齐/合并排序。
2. 探针条目为静态写入的固定 FATAL 样本；真实异常/崩溃上下文的现场（vector、error、
   rip、栈回溯）进 emergency 环的端到端取证不在本轮范围。
3. 32 位 emergency 并入 `journal` 仅由共用 `event_log_dump()` 代码路径保证，本轮未在
   32 位启动镜像上单独抓 emergency 段原文。
