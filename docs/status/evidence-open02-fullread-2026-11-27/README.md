# OPEN-02 边界④：usb-storage 全盘逐扇区读（2026-11-27）

## 结论

在 QEMU `piix3-usb-uhci` + `usb-storage` 的 64MB 测试盘上，64 位
`usb64.c` 路径完成了 **131072/131072 扇区逐扇区读 + 逐字节图案校验**：

```
[MSC-FULLREAD] blocks=131072 chunk=16 rounds=8192 sampled=131072 read_err=0 mismatch=0 checksum_sum=0xFE0001E6 checksum_xor=0x7A elapsed_ms=681714
```

宿主侧不采信内核自报，直接对 64MB raw 镜像逐字节复算：

```
[HOST] image_bytes=67108864 blocks=131072 bsize=512
[HOST] full-byte mismatch=0
[HOST] checksum_sum=0xFE0001E6 checksum_xor=0x7A
...
RESULT: PASS
```

即：**全盘 131072 个扇区都实际读过，读回内容与宿主机确定性图案逐字节一致**；
不是步长抽样，也不是只读首尾。

## 为什么是 16 扇区/条

全盘读探针最初按 64 扇区/条（32KB 数据阶段）发 READ10，QEMU 在第一条命令
就返回 `[USB64] data stage failed`，TD 状态 `0x400000`（`TD_STATUS_STALLED`）。
把块长缩到 32 扇区/条（16KB）后，前 888 条命令正常，到
`lba=0x6F00`（第 889 条）再次 STALL：

```
[MSC-FULLREAD] progress lba=0x67E0 sampled=26624
[USB64] data stage failed
[USB64] bulk TD err=0x400000
[MSC-FULLREAD] blocks=131072 chunk=32 rounds=889 sampled=28416 read_err=1 ...
first_bad lba=0x6F00 off=0xFFFFFFFE
```

16 扇区/条（8KB）连续跑完 8192 条 READ10，`read_err=0`、`mismatch=0`。
因此这里的 16 不是“随便选的小块”，而是实测能稳定覆盖全盘的块长；64/32
的失败现象已保留在 `chunk-size-selection.txt`。这只说明 QEMU + 当前 UHCI
TD 链/数据阶段组合的边界，**生产路径 `usb_storage_read_sectors()` 仍是单扇区**，
本探针不改变生产行为。

## 运行条件

- QEMU 6.2，UEFI（OVMF 4M），64 位内核 `v0.63.0`，文本模式菜单选 `3`
- `-device piix3-usb-uhci` + `-device usb-storage`，64MB 测试盘
- USB 后端 `cache=none`，`snapshot=on` 只用于系统盘，不用于 USB 盘
- 构建：`MSC_FULLREAD_TEST=1 HEPU_EV=/tmp/open02-fullread-chunk16 bash tools/regress/build_all.sh`
- 探针内核 `1,025,104 B`（带全盘读探针）

QEMU 命令（节选，完整路径见同轮终端记录）：

```sh
qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" -rtc base=localtime \
  -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
  -drive if=pflash,format=raw,file=/tmp/open02-fullread-chunk16/run/V.fd \
  -drive format=raw,file=/tmp/open02-fullread-chunk16/build/hepu.img,file.locking=off,snapshot=on \
  -drive if=none,id=usbdrv,file=/tmp/open02-fullread-chunk16/usb.img,file.locking=off,cache=none,format=raw \
  -device piix3-usb-uhci,id=uhci0 -device usb-storage,drive=usbdrv,id=msc0 \
  -m 1024 -smp 2 -display none -vga std \
  -monitor unix:/tmp/open02-fullread-chunk16/run/mon.sock,server,nowait \
  -serial file:/tmp/open02-fullread-chunk16/run/ser.log \
  -nic user,model=e1000 -daemonize -pidfile /tmp/open02-fullread-chunk16/run/qemu.pid
```

启动 7 秒后经 monitor 发 `sendkey 3` 选择 64 位文本模式；探针在全盘读完成后
自动进入 shell。

## 证据文件

- `serial-fullread.log`：完整串口（含每条 READ10 的 64 条进度行和最终判定行）
- `host_check_fullread.py`：宿主独立逐字节复核脚本
- `host-check-fullread.txt`：宿主复算输出，`RESULT: PASS`
- `chunk-size-selection.txt`：64/32 扇区失败的串口摘录与参数选择依据
- `sha256.txt`：串口日志与测试镜像的 SHA-256

## 边界与未做

- 只在 QEMU 6.2 + bochs/piix3 UHCI 仿真下验证；**未上真实 UHCI 主控/真机 U 盘**。
- 只验证 64 位 `usb64.c` 路径；32 位 `usb_msc.c` 路径未重跑全盘读。
- 本测试只读，不涉及写掉电；掉电持久化的“已完成写”证据见
  `../evidence-open02-powerloss-2026-11-27/`，写中途掉电与真实硬件未做。
- 全盘读耗时按 PIT tick 记录 681714ms；这是 TCG 仿真的时间，不是硬件性能结论。
