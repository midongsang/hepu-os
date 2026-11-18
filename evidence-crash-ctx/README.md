# crash_ctx 验收证据（feat/crash-ctx）

- `real-crash-final.log`：32 位 QEMU 实跑完整串口日志；真实用户态 `#DE` 崩溃 1 次端到端。
  - `real-crash-snippet.txt`：关键片段。
  - 结果：`state=dead restarts=0x00000001`，`pc=0x1a47be`、`syscall=6`、
    `msg=user #Division By Zero at 0x1a47be esp=0x4000fff0`。
- `probe3.log`：内核侧临时探针连打同一槽位 3 次快照后的完整串口日志。
  - `probe3-snippet.txt`：关键片段。
  - 结果：`restarts=0x00000003`，`last_crash pc=0xC0FFEE03 syscall=0x2B
    msg=probe crash #3`（最近一次覆盖，不是第 1 次）。

探针为验收临时入口，取证后已从工作树与提交中移除；本目录只保留日志证据。
注意：`probe3` 证明的是计数累计与 `last_crash` 覆盖机制，不等于服务被 supervisor
自动重启了三次；真实重启闭环仍属未完成项（见 `docs/design/CRASH_CTX_LANDING.md` §6.3）。
