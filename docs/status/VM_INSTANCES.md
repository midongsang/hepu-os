# QEMU 实例隔离与并发规范

> 2026-11-17 引入。起因：多个 agent 同时验证时，全部抢同一把 `device.lock`，
> 一次排队十几分钟；而排队本身不是 QEMU 的限制，是**共用可变文件**的结果。

## 结论先行

不要再用全局 `device.lock` 串行化 QEMU。改用两个脚本，它们自带实例隔离：

```bash
# 64 位桌面截图（等 'hs_window|frame=' 出现后再截）
bash shots/.shot.sh <TAG> [OUT.png]

# 引导菜单截图（只按固定秒数等待，因为此时内核还没起来）
bash shots/.bootmenu.sh <TAG> [WAIT_SEC] [OUT.png]
```

`TAG` 是这次任务的短名字，例如 `d64test`、`menu`、`kern-protect`。**同一个 TAG 重复跑会清理自己上一次的实例，不会碰别人的。**

## 原来为什么会卡

| 共用资源 | 原来的名字 | 现在的名字 |
|---|---|---|
| 磁盘镜像 | `run/shot.img`（一个文件，各任务 cp 覆盖） | 基础镜像只读，写进 QEMU 私有 overlay（`snapshot=on`） |
| 串口日志 | `run/ser_kill.log` | `run/inst/<TAG>/ser.log` |
| monitor socket | `run/mon_kill.sock` | `run/inst/<TAG>/mon.sock` |
| pid 文件 | `run/qemu_kill.pid` | `run/inst/<TAG>/qemu.pid` |
| OVMF vars | `run/V_shot.fd` | `run/inst/<TAG>/V.fd` |

四条一拆，锁就没必要了。`snapshot=on` 是关键：QEMU 把所有写操作放进自己的临时 overlay，**多个实例可以同时以只读方式打开同一份基础镜像**。

## 并发额度

脚本内部用 `flock` 的 slot 文件限流，额度 = `nproc / 2`，封顶 3，下限 1。

为什么除以 2：每个实例是 `-smp 2`，TCG 软件模拟下 vCPU 是并行翻译线程，**一个实例就吃满 2 个核**。这台机器 2 核、无 KVM，所以额度 1 —— 等价于串行，但不再需要全局锁，也不会误杀别人的进程。

想覆盖额度：`HEPU_SLOTS=2 bash shots/.shot.sh ...`（多核机器才建议调）。

## 三条硬禁止

1. **禁止 `pkill -9 -x qemu-system-x86_64`** —— 会杀掉所有 agent 的实例。要清就按自己的 `run/inst/<TAG>/qemu.pid` 精确杀。
2. **禁止 `pkill -f '<含自身命令行的字符串>'`** —— 会匹配到脚本自己（本项目已经踩过两次，表现为 exit 137 但 QEMU 反而活着）。
3. **禁止在脚本里 `cd` 到 `run/` 再写固定文件名** —— 那是老方案的形状，会重新引入冲突。

## 录屏

```bash
bash shots/.record.sh <TAG> [DUR_SEC] [FPS] [OUT.mp4]
```

QEMU 没有内建录屏，这台机也没有 X 装 vnc2mpg 一类工具。走 QMP 循环 `screendump`
抓帧再合成 —— 启动过程对帧率要求低（默认 4fps 够看），这条路比装 VNC 流工具稳。

合成用 `concat` + 每帧真实 `duration`，所以**视频时长 == 真实启动时长**，
不会因为 TCG 下抓帧不稳而变成快放。

### 一个必须知道的坑：`-display none` 会锁死 640×480

`-display none` 时 QEMU 的 VGA surface 停在固件默认的 640×480，
**即使 Guest 内部已经是 1920×1080**（引导器日志里明明写着
`[GFX64] Mode set: 1920x1080 fb=0x80000000`），`screendump` 出来的还是 640×480。

必须挂真实 display 后端（`-vnc 127.0.0.1:<port>`），surface 才会跟随 VGA 模式变化。
三个脚本（shot / bootmenu / record）都已改成 VNC，端口按 slot 分配（5900+slot）。

另外启动途中分辨率会从 640×480 跳到 1920×1080，所以录制时每帧都
**等比缩放后居中贴到 1280×720 画布**，两边留黑边不变形 ——
直接 thumbnail 会让不同尺寸的帧混进 concat，ffmpeg 要么报错要么退化到第一帧尺寸。

## 减少启动次数（比并行更有效）

一个 QEMU 启动约 20 秒级（TCG 无 KVM），改 5 处就起 5 次是浪费。判据能合并就合并：**一次启动同时验引导菜单 → 进内核 → 串口日志 → 桌面 NONBLACK → SELFTEST**。

32 位已有 `tools/regress32.sh` 是这种形态；64 位目前还是一次改动一次启动，属于待改项。

镜像的菜单停留时间由 `HEPBOOT.CFG` 的 `timeout` 决定，默认 5 秒窗口太窄，截图常抓不到菜单就进内核了。要截菜单先把 timeout 调到 15~30：

```bash
printf 'timeout=30\ndefault=0\nkernel_arch=64\ncmdline=\n' > /tmp/cfg30.cfg
mcopy -o -i /tmp/x_menu.img@@1M /tmp/cfg30.cfg ::/HEPBOOT.CFG
```

## 已知限制

- **没有 KVM**（`/dev/kvm` 不存在），全部走 TCG，所以 20 秒级启动是下限，不是配置问题。
- `snapshot=on` 下实例跑完**不落盘**。需要保留改动后的镜像就显式 `cp` 出来，别指望 QEMU 帮你保存。
- 磁盘镜像必须能被多个实例同时以只读打开；如果某个流程需要写镜像（`mkfs`/`mcopy`），**那一步仍应独占**，写完再交给截图脚本。
