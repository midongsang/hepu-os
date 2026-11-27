# OPEN-02 边界④附：写完成后掉电（QEMU）持久化证据（2026-11-27）

## 结论

在 QEMU `piix3-usb-uhci` + `usb-storage` 的 64MB 测试盘上，先用
`MSC_SWEEP_TEST=1` 探针完成末扇区 `0x1FFFF` 的 WRITE10 + 读回环：

```
[MSC-SWEEP] blocks=131072 bsize=512 stride=256 sampled=523 read_err=0 mismatch=0 wlba=0x1FFFF wrc=0x0 rdrc=0x0 wmism=0 checksum_sum=0x208F6E6 checksum_xor=0x7A elapsed_ms=46174
```

串口出现该判定行后立即对 QEMU 进程 `kill -9`（模拟客人侧突然掉电），
再在宿主侧独立核对 USB raw 镜像：

- 末扇区写回图案 `(lba*31 + off*13 + 0x5A) & 0xFF`：`wmism=0`
- 宿主原采样序列（步长 256 + 头 1..3 + 尾 8）逐字节：`mismatch=0`
- 宿主额外 32 个随机 LBA：`mismatch=0`
- 宿主脚本最终：`RESULT: PASS`

即：**在 QEMU `cache=none` 后端上，BOT WRITE10 返回成功、且客人已读到
写回数据之后，立即杀死 QEMU，宿主镜像仍保留该扇区的完整写入**。这证明
“已完成写”的持久化链路在 QEMU 仿真路径上成立。

## 证据链

| 项 | 值 |
|---|---|
| 测试盘生成 | `host_gen_usb_image.py`，64MB，131072×512B |
| 初始镜像 SHA-256 | `b52149b9b356ba75797b5e6f55ae83ba018bbd4d60a040bdf8a57b02bf76b0c0`（生成时记录） |
| 掉电后镜像 SHA-256 | `5a7c63a3bfa969890e655dd6a3e15abd79ce53e5d6fd2c55a4d8444bc8c6cb34` |
| 掉电后末扇区 | `last-sector-after-kill.bin`，`wmism=0` |
| 串口 | `serial-powerloss.log` |
| 宿主核对 | `host-check-powerloss.txt`，`RESULT: PASS` |

## 边界（不能从这条证据外推的部分）

- 这是 QEMU 仿真 + 宿主文件，不是真实 U 盘/真实 USB 主控；`kill -9`
  终止的是 QEMU 进程，不是宿主断电。`cache=none` 让写入直落宿主后端，
  但宿主页缓存/磁盘刷写语义仍与真实设备掉电不同。
- 只覆盖“写完成之后”的时间点；**写中途掉电、SCSI 命令执行中的部分写、
  多扇区写整体原子性均未测**，不能宣称“掉电后绝无部分写”。
- `usb_storage_write_sectors()` 的生产路径仍是逐扇区 WRITE10；多扇区
  WRITE10 只在探针里用于读路径，写路径未做多扇区掉电夹具。
- 未做 `e2fsck`/ext2 文件级掉电一致性；本轮只核对 raw 扇区图案。
- 真实硬件兼容性（U 盘、UEFI 真机、不同主控）仍未验证。

## 运行条件

- QEMU 6.2，UEFI OVMF 4M，64 位内核 v0.63.0，文本菜单 `3`
- `-device piix3-usb-uhci` + `-device usb-storage`，USB 后端 `cache=none`
- 构建：`MSC_SWEEP_TEST=1 HEPU_EV=/tmp/open02-powerloss bash tools/regress/build_all.sh`
- 先 `make -C kernel/arch/x86_64 clean`，避免上一次 `MSC_FULLREAD_TEST`
  的目标文件残留（构建脚本不跟踪 CFLAGS 变化，这是本轮实际踩到的坑）
- 探针跑完后 `kill -9 $(cat /tmp/open02-powerloss/run/qemu.pid)`
- 宿主核对：`python3 host_check.py /tmp/open02-powerloss/usb.img /tmp/open02-powerloss/run/ser.log`
