# OPEN-02 边界① 满盘/分段遍历读写 —— 运行时证据（2026-11-26）

对应台账：`docs/status/OPEN_BUGS.md` OPEN-02 / BUG-60。
分支：`test/open02-boundary-2026-11-26`（基于 main 58d772d）。

## 1. 本轮闭环了哪一项

OPEN-02 主路径（32 位 `kernel/drivers/usb/`，证据见
`docs/status/evidence-open02-2026-11-26/`）已于此前关闭。本轮在 **64 位
`kernel/arch/x86_64/usb64.c`** 路径上闭环边界①：**usb-storage 分段遍历读校验 +
单扇区写读回环**，覆盖到盘的末端 LBA（`blocks-1`）。

判定由双证据支撑，不采信内核自报：

1. 串口原始行 `[MSC-SWEEP] ...`（计数/结论行）；
2. 宿主机独立重读 raw 镜像逐字节 diff + 复算校验和，并额外抽 32 个不在内核
   采样序列内的 LCG 随机 LBA 比对。

## 2. 构建与运行

### 2.1 工具链

```bash
source tools/regress/env.sh    # PATH 指向 .toolchain/pkg/root2，qemu-system-x86_64 可用
```

### 2.2 64 位内核（探针为编译期开关，默认发布构建不编入）

```bash
# 带探针的取证构建（本轮证据对应产物）
make -C kernel/arch/x86_64 MSC_SWEEP_TEST=1 -j2

# 不带探针的默认构建（回归发布口径）：make -C kernel/arch/x86_64 -j2
# 已复验：默认构建 RC=0，二进制内不含 "MSC-SWEEP" 字符串。
```

探针挂在 `usb64_msc_sweep_test()`，仅在 `HEPU_MSC_SWEEP_TEST` 宏下编译；
Makefile 开关 `MSC_SWEEP_TEST=1`，对齐既有 `ATA_HILBA_TEST` 模式。

### 2.3 系统镜像与 USB 测试盘

- 系统盘：`tools/build_uefi_dual.py` 打包（ESP 放本轮 `hepu-kernel64.bin`，
  256MB，快照只读挂载）。
- USB 测试盘：`host_gen_usb_image.py` 生成 64MB raw 镜像
  （`blocks=0x20000`，`bsize=0x200`），确定性图案：
  扇区 L 字节 `i` = `(L & 0xFF) ^ ((L >> 8) & 0xFF) ^ (i & 0xFF)`；
  LBA0 前 8 字节固定签名 `OPEN02-A`（`4F 50 45 4E 30 32 2D 41`）。

### 2.4 QEMU 命令行（piix3-usb-uhci + usb-storage）

```
qemu-system-x86_64 -L <datadir> -rtc base=localtime \
  -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=OVMF_VARS_4M.fd \
  -drive format=raw,file=hepu.img,file.locking=off,snapshot=on \
  -drive if=none,id=usbdrv,file=open02-usb.img,file.locking=off \
  -device piix3-usb-uhci,id=uhci0 \
  -device usb-storage,drive=usbdrv,id=msc0 \
  -m 1024 -smp 2 -vnc 127.0.0.1:5911 \
  -monitor unix:mon.sock,server,nowait \
  -serial file:serial.log \
  -nic user,model=e1000 -vga std -daemonize -pidfile qemu.pid
```

UEFI 菜单选 `3`（Protection Mode，`hepu.graphics=0` 文本终端）；USB 枚举在
文本路径 `usb64_init()` 内完成，探测到容量后跑遍历探针。QEMU 与构建分别走
`/tmp/hepu-qemu.lock`、`/tmp/hepu-build.lock`；收尾只按本实例 pidfile 杀进程。

## 3. 判定结果（最终干净跑）

串口 `usb64-sweep-serial.log` 关键行：

```
[USB64] INQUIRY: QEMU / QEMU.
[USB64] Capacity: 64 MB (131072 blocks).
[MSC-SWEEP] blocks=131072 bsize=512 stride=256 sampled=523 read_err=0 mismatch=0 wlba=0x1FFFF wrc=0x0 rdrc=0x0 wmism=0 checksum_sum=0x208F6E6 checksum_xor=0x7A elapsed_ms=44559.
```

宿主独立核对 `host-check.txt`（`host_check.py` 输出）：

| 字段 | 串口 | 宿主复算 | 结论 |
|---|---|---|---|
| blocks | 131072 | 131072 | OK |
| bsize | 512 | 512 | OK |
| stride | 256 | 256 | OK |
| sampled | 523 | 523 | OK |
| read_err | 0 | 0 | OK |
| mismatch | 0 | 0 | OK |
| wlba | 131071 | 131071 | OK |
| wrc / rdrc | 0 / 0 | 0 / 0 | OK |
| wmism | 0 | 0 | OK |
| checksum_sum | 0x208F6E6 | 0x0208F6E6 | OK |
| checksum_xor | 0x7A | 0x0000007A | OK |

- 额外独立抽样：32 个 LCG 随机 LBA（不在内核采样序列内），`mismatch=0`。
- 写读回环：末扇区 LBA `0x1FFFF` 写入 `(wlba*31 + i*13 + 0x5A) & 0xFF`
  图案后读回，`wmism=0`；宿主回读该扇区与回环图案一致。
- 可重复性：修复后连续 3 次干净跑，`checksum_sum/xor` 完全一致，
  `read_err=0 mismatch=0 wmism=0`；本轮归档串口为其中一次
  （`elapsed_ms=44559`），另两次 elapsed 分别为 52956 / 43760 ms。

## 4. 覆盖范围与口径限制（如实说明）

- **不是逐扇区全读**：采样序列 = 步长 256 扇区遍历全盘（0 … 130816）
  + 头部 1/2/3 + 尾部 8 扇区（131064 … 131071），共 523 / 131072 扇区。
  64 位路径按单扇区一条 BOT 命令循环，TCG 下约 85–100 ms/命令，逐扇区读全
  131072 扇区在单次启动内不可行；步长抽样是任务允许的“分段遍历”口径。
- 末端 LBA `131071` 已明确读到并参与写读回环；无越界访问、无超时挂死，
  遍历期间未出现二次枚举/控制器复位。
- 耗时与失败计数以串口 `elapsed_ms` / `read_err` / `mismatch` / `wmism`
  为准，失败计数真实打印，未吞错。
- 未覆盖：全盘逐扇区读、掉电持久化、STALL/NAK 错误恢复、真实硬件兼容性
  （见下节与台账 OPEN-02 残余边界）。

## 5. 本轮发现并修复的缺陷：BUG-60

64 位 `usb_storage_read_sectors` / `usb_storage_write_sectors` 的 SCSI
READ(10)/WRITE(10) 传输长度大端编码放错字节：旧实现 `cb[7]=1`，把长度编成
`0x0100`（256 块）；规范长度字段是 BE16，1 扇区应为 `0x0001`（`cb[7]=0`、
`cb[8]=1`）。中前段 LBA 设备按 BOT 数据长度截断传输尚可容忍，近末端 LBA
请求跨出容量即被设备拒绝。

- 修复前复现串口：`usb64-sweep-prefix-serial.log`
  （`read_err=8 first_bad lba=0x1FFF8`，WRITE `rc=0xFFFFFFFFFFFFFFFD`，
  `wrc/rdrc=0xFFFFFFFFFFFFFFFF`）。
- 修复后：见第 3 节，尾部 8 扇区读与末扇区写读回环全部通过。
- 位置：`kernel/arch/x86_64/usb64.c` READ10 / WRITE10 命令块构造处。

## 6. 残余边界（未闭环，不在本节宣称已修）

- **掉电持久化**：未做。未在写完成前后不同时间点 kill QEMU 抠镜像核对落盘
  扇区；本轮写读回环是通电状态下的内存/缓存内回读，不证明掉电持久。
- **STALL/NAK 错误恢复**：未做可控注入。探索阶段曾观察到一次长遍历中段出现
  批量 `bulk TD err=0x400000`（端点 STALL）连锁，复跑即过、未稳定复现；
  64 位批量路径在 TD STALL 后未见 CLEAR_FEATURE 端点 halt 恢复，列入残余
  观察，待可控注入夹具复验。
- **真实硬件兼容性**：未做。全部结论基于 QEMU 6.2 `piix3-usb-uhci` +
  `usb-storage` 模拟设备。
- **全盘逐扇区读**：本轮为步长抽样，未逐扇区覆盖 131072 个扇区。

## 7. 文件清单

| 文件 | 说明 |
|---|---|
| `usb64-sweep-serial.log` | 修复后最终干净跑串口原文（含 `[MSC-SWEEP]` 判定行） |
| `usb64-sweep-prefix-serial.log` | BUG-60 修复前复现串口（尾部 8 扇区读失败 + 写失败） |
| `host_check.py` | 宿主独立核对：采样序列逐字节 diff、校验和复算、回环扇区核对、32 个额外随机抽样、串口字段比对 |
| `host_gen_usb_image.py` | 确定性图案 64MB raw 测试盘生成器 |
| `host-check.txt` | `host_check.py` 最终输出（RESULT: PASS） |
