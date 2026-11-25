# OPEN-02 UHCI/usb-storage 运行时证据（2026-11-26）

## 环境

- 32 位 `build_v250/kernel.elf`
- QEMU 6.2.0 + `piix3-usb-uhci` + `usb-storage`
- 64MB raw 测试盘；LBA 0 预置 `OPEN02-A` 作为可识别内容，最后 8 扇区用于写回读

## 结果

- 控制阶段全部 `[UHCI-CTRL-OK]`。
- MSC INQUIRY 得到 `QEMU HARDDISK 2.5+`，READ CAPACITY 得到 `0x20000` 块、`0x200` 字节/块。
- LBA 0 真实读回 `4F 50 45 4E 30 32 2D 41`（`OPEN02-A`），证明 bulk IN 数据路径可用。
- 最后 8 扇区（4KB）执行 WRITE(10) 后 READ(10)：
  - `[MSC-WRITE] ... wr=0x00000000 rd=0x00000000 mismatch=0xFFFFFFFF`
  - `mismatch=0xFFFFFFFF` 是 `-1` 哨兵，表示逐字节比对未发现差异。
  - 宿主机直接核对镜像同一 LBA：`pattern_match=true`（见 `host-lba-check.txt`）。

## 文件

- `uhci-final-serial.log`：串口日志（含 MSC 探测、LBA0 读、4KB 写回读结果）。
- `uhci-final-msd-trace.log`：QEMU `usb_msd_*` trace，最后 `cmd_complete status 0`、`send_status status 0`。
- `host-lba-check.txt`：宿主机对测试盘写入区域的独立核对结果。

## 说明

- 这一轮验证使用临时 probe 触发扇区读写；probe 已在提交前移除，生产代码保留的是控制/批量传输、QH 调度、描述符解析和 MSC 分块逻辑。
- 本轮未做满盘遍历、掉电持久化和真实硬件兼容性验证。
