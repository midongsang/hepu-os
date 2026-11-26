# 回归排查记录：64 位 UEFI 启动停在 IRQ 使能之后（2026-11-26）

## 环境
- 分支 `feat/service-supervisor-2026-11-26`，worktree `hepu-work-svc`，排查前 HEAD `5e4a1dc`。
- 64 位 UEFI（OVMF pflash），镜像 256MB 双内核，`snapshot=off`，串口观察窗 120 秒。
- 复现脚本：`ev-64/boot-repro.sh <label> <wait_sec>`（未入库暂存目录 `ev-64/`）。

## A：当前 HEAD（音频后端 `hs_audio_init()` 绑在服务 start，IRQ 使能前）
串口关键行（`regression64-A-audio-in-start.ser.txt`，1203 行）：

```
[SERVICES] start: audio
[SVC] audio service start (backend: hs_audio_init)
[AUDIO] Service initialized (AC97)
[SERVICES] started: audio (stub)
...
[SERVICES] boot services started: 5/5
...
[IRQ64] PIT+keyboard+UART handlers registered
[IRQ64] Interrupts enabled (PIC mode)
```

之后 120 秒无后续：`EXT2_64` 出现 0 次、`[EVENT]` 0 次、无桌面标记。与合并后 main `99b2621` 上报现象一致，本分支可复现。

## B：音频后端初始化移回桌面阶段（工作树未提交补丁）
改动：`svc_audio_start()` 不再调 `hs_audio_init()`（只记状态）；`hs_audio_init()` 调回
`hs_services_init()`（桌面阶段，IRQ/ext2 之后）；同步 `hsservices.h/.c` 头注释。

串口关键行（`regression64-B-audio-reverted.ser.log`，1188 行）：

```
[SERVICES] start: audio
[SVC] audio service start
[SERVICES] started: audio (stub)
...
[SERVICES] boot services started: 5/5
...
[IRQ64] PIT+keyboard+UART handlers registered
[IRQ64] Interrupts enabled (PIC mode)
```

**B 仍然停在同一位置**：`EXT2_64` 0 次、`[EVENT]` 0 次、无桌面。A/B 结论：
**音频后端初始化不是直接嫌疑**，移出 IRQ 前路径后回归依旧。

## 已排除 / 已核对
- `hs_audio_init()` 函数体只清零 `g_audio` 结构 + 打印，不直接写 AC97/PCI 寄存器
  （寄存器写入在 `set_volume/mute/unmute` 路径，未在早期调用）。
- 本路 diff 中早期路径新增的外部调用仅：`pit64_get_ticks()`（只读全局 tick 计数，良性）
  与 `hs_audio_init()`（B 已回退，仍卡死）。
- `kernel_main_64.c` 相对基线 `9c942dc` 无改动；64 位链接产物不含
  `microg_service.o` / `distributed_os.o`。
- 被禁改文件（`event_log.h/.c`、`klog*.c`、`shell64.c` 命令表、`shell_execute.c`）
  未出现在本次改动中。

## 未完成 / 下一步
- 根因尚未定位，**尚未提交修复**；工作树留有 B 方向未提交补丁（3 个文件）。
- 待继续排查：服务 start 在 IRQ 前路径里其余新逻辑（拓扑排序/健康状态/退避状态机、
  `event_log_emit` 早期调用、`crash_ctx_snapshot_for_restart` 空壳）对早期内存/状态的
  影响；必要时按提交 `9a48877` → `9168251` → `b3274bc` 做二分启动验证。
- 验证口径：需复跑到 `[EXT2_64]` 再到桌面 / `[EVENT] saved` 才算修复通过。
