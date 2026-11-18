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

## 复现命令

```bash
# 工具链环境（见 DIAG_AUDIT_RUN.md §2.1）
flock /tmp/hepu-build.lock -c 'make -j2'
qemu-system-i386 -L "$BIOS" \
  -kernel "$WT/build_v250/kernel.elf" -m 256 -smp 1 -display none \
  -serial file:<serial.log> -no-reboot
```

## 取证脚手架声明

两次日志均在临时脚手架下采集（自检序列前置 diag、内核侧临时 `cap_check`
探针、diag 临时打印原始 `n`、sys_exit 临时串口镜像）。这些改动**未进入最终
提交**，已 `git checkout --` 还原；`git diff --stat 391e1db` 为空。
从干净树直接复跑会在既有 `hepu_info()` 版本行卡住（见设计文档 §8.1），
需按 §7 临时绕过该行后才能到达审计段。
