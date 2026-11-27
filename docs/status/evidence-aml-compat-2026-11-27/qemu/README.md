# QEMU 合成表取证（2026-11-27）

## 环境与口径

- 镜像：`tools/regress/build_all.sh` 重建的双内核镜像（嵌入本轮 64 位 `kernel64.bin`，
  1,062,608 B）；QEMU/OVMF 见 `tools/regress/env.sh` 引用的 `.toolchain` 目录。
- 探针：`tools/regress/battery_state_probe.sh <输出目录> <ssdt>`，内部 `flock` 串行，
  两个夹具先后运行，未并发。
- **现实约束（必须如实标注）**：QEMU/OVMF 环境**没有 ACPI EC 硬件设备**；以下 `VALID`
  与 `--` 结论均来自合成 SSDT 在 QEMU 里的解析/UI 链路，**不是真机验证**。
- 合成表由 `tools/regress/make_battery_ssdt.py` 离线编码 AML（环境无 iasl）。

## 夹具 1：`create-dword/`（CreateDWordField 缓冲拆包正路径）

- 表：`battery-create-dword.ssdt`（155 B，mode `create-dword`，75%）。
- 语义：`_BST` 用 `CreateDWordField` 从 4 字节 Buffer 拆包剩余容量 7500；`_BIF`
  full=10000；`_BXX` 演示 `CreateByteField` + `Store 0x5A` 回写往返。
- 串口 `[BAT]` 关键行（`bat-lines.txt`）：

  ```
  [BAT] aml sta=0xF bst=1 bif=1 pct=75 chg=1 rem=7500 full=10000 unit=0 rate=0 health=100 min=-1 min_full=-1 slots=1
  ```

- 结论：缓冲拆包链路在 64 位桌面 QEMU 跑通，电池采样进 VALID（7500/10000 → 75%）。
- 截图：`desktop_1.png` / `desktop_2.png`（顶栏电池图标；百分比以串口 `pct=75` 为准）。

## 夹具 2：`idxfield-ec/`（EC 上 IndexField，fail-closed）

- 表：`battery-idxfield-ec.ssdt`（275 B，mode `idxfield-ec`）。
- 语义：电池设备 `ECI0` 的 `_BST` 走 EmbeddedControl 上的 IndexField；EC 设备 `EC00`
  （PNP0C09）由合成表声明。
- 串口 `[BAT]` 关键行（`bat-lines.txt`）：

  ```
  [BAT] aml dev=ECI0
  [BAT] ec=0x1 dev=EC00
  [BAT] ec glk=0
  [BAT] ec reg=0
  [BAT] aml bst failed code=-3
  ```

- 结论：EC 上 index/data 寄存器写路径未实现 → `AML_E_UNSUP(-3)`，电池保持
  `state=declared`，**不出现假百分比**；顶栏电池为空轮廓（见 `desktop_1.png`）。
- 注意：`ec=0x1` 仅表示命名空间命中合成表声明的 PNP0C09；QEMU 无真实 EC 端口，
  这不是 EC 正路径验证。

## 未覆盖

- 未在真笔记本/真台式机/UPS 上取证；未验证真机 EC 端口、`_REG` 重放副作用。
- 未对 SystemIO/SystemMemory 的 IndexField/BankField 单独跑 QEMU 夹具（该正路径由主机
  `idx_bank_field` 31 checks 覆盖）。
