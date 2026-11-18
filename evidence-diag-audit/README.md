# 证据目录：diag 权限审计段 32 位实跑

配套说明见 [`../docs/design/DIAG_AUDIT_RUN.md`](../docs/design/DIAG_AUDIT_RUN.md)。

## 元数据

- 分支：`test/diag-audit`，基点 `391e1db`
- QEMU：`qemu-system-i386` 6.2.0；`-m 256 -smp 1 -display none -no-reboot`，串口落文件
- 内核镜像：`build_v250/kernel.elf`（multiboot 直启），initrd：`build_v250/initrd.img`
- 启动窗口：150s / 次，按 pidfile 精确结束实例

## 文件清单

| 文件 | 内容 |
|---|---|
| `run-A-empty/serial.log` | 空环态完整串口日志（723 行） |
| `run-A-empty/diag-section.txt` | 空环态 diag 段摘录：`raw syscall n=0` → `(暂无记录)` |
| `run-B-denied/serial.log` | 真实拒绝态完整串口日志（722 行） |
| `run-B-denied/diag-section.txt` | 拒绝态摘录：探针两条 + diag 解码两行 + 退出码 `code=0x00000000` |
| `run-C-clean-diag/serial.log` | 版本行修复后复跑完整串口日志（719 行，带临时 `raw syscall n=` 打印） |
| `run-C-clean-diag/diag-section.txt` | 修复后摘录：`内核版本: 1.0.2`、`raw syscall n=0` → `(暂无记录)`、`[建议]`、`rc=0x00000000` |
| `run-C-clean-diag/diag-section-final-source.txt` | 去掉临时调试行后的最终提交源码复核摘录：`内核版本: 1.0.2`、`(暂无记录)`、`[建议]`、`rc=0x00000000` |

## 复现命令

```bash
# 工具链环境（见 DIAG_AUDIT_RUN.md §2.1）
flock /tmp/hepu-build.lock -c 'make -j2'
qemu-system-i386 -L "$BIOS" \
  -kernel "$WT/build_v250/kernel.elf" -m 256 -smp 1 -display none \
  -serial file:<serial.log> -no-reboot
```

## 取证脚手架声明

- A/B 两轮日志在临时脚手架下采集（自检序列前置 diag、内核侧临时 `cap_check`
  探针、diag 临时打印原始 `n`、sys_exit 临时串口镜像、临时绕过版本行）。
- C 轮：版本行卡死已在 `diag.c` 正式修复（见设计文档 §8.1）；复跑仍用临时
  hoist 把 diag 提到自检最前，`raw syscall n=` 为临时调试打印（最终源码已去掉）。
- 上述临时项**均未进入最终提交**，已 `git checkout --` 还原；
  `kernel/sched/shell.c` 相对基点 0 改动，`diag.c` 仅保留版本解码 hunk。
- 注意：`hello_c` / `sysinfo` / `top` 仍有同款 `%s` 误用（§8.1 清单），
  从干净树直接跑这三个应用仍会卡在版本行；diag 本身已可一路跑到 `[建议]`。
