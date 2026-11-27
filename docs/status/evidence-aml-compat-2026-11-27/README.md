---
title: ACPI AML 解释器建库容错与缓冲拆包兼容性对比（2026-11-27）
status: active
sys_version: 0.63.0
updated: 2026-11-27
owner: status
source_of_truth: null
supersedes: null
---

# ACPI 语料兼容性 after 对比（工作树源码，2026-11-27）

## 0. 元信息与边界声明

- 对比对象：极简 AML 解释器 `kernel/arch/x86_64/aml64.c` / `aml64.h`，电池侧
  `kernel/arch/x86_64/acpi_battery.c`，EC 侧 `kernel/arch/x86_64/ec64.c`。
- 基线：仓库 `main`，HEAD `a9b3b594365c4abfd9ab21305dd3f8a1789eb4da`；before 指标取自
  审计线证据 `docs/status/evidence-aml-corpus-2026-11-27/probe-results.tsv`（HEAD 快照编译）。
- 本轮 after 探针按 `tools/regress/run_aml_compat_probe.sh` 编译，**链接工作树当前源码**
  （非 HEAD 快照导出），ASan + UBSan，`-DAML_HOST_TEST`；未覆盖审计线基线证据目录。
- 本轮 **未 commit / push**；`docs/` 下既有文件未改，本目录为新增。工作树前后状态见
  `logs/git-status-before.txt` / `logs/git-status-after.txt`。
- **语料性质声明**（与基线一致，不得混称“真机已验证”）：
  - `raw-binary`：公开 GitHub 仓库直接拿到的二进制 AML；
  - `reassembled`：从公开 acpidump 十六进制文本还原，**不是固件原始二进制**；
  - `firmware`：SeaBIOS 等模拟器固件内表；
  - `synthetic`：`tools/regress/make_corpus_fixtures.py` 离线生成的合成夹具，非真机固件。
- **QEMU 无 ACPI EC 设备**；本轮在 QEMU 跑了两张合成表取证（见 `qemu/`）：
  `create-dword` 拆包正路径 VALID 75%，`idxfield-ec` fail-closed 停在 DECLARED；
  **未在真机上验证**。语料探针与主机测试仍为主结论依据。

## 1. 本轮落地的解释器改动

1. **建库容错 + 诊断透出（P0）**：未识别外层对象若带合法 PkgLength，按长度跳过并记
   `degraded` / `unknown_skipped`，不再整表 `return -1`；无法定位长度才硬中止，偏移/原因
   经新接口 `aml_ns_diag(degraded, unknown_skipped, stop_off)` 透出（`aml_ns_build` 签名与
   调用方行为不变）。外层 `If(0xA0)/Else(0xA1)` 建库期递归登记 then/else 两分支声明
   （不求值条件）；`While(0xA2)` 跳过 body 并记降级。
2. **Create*Field 缓冲拆包（P0）**：`0x8A` CreateDWordField、`0x8B` CreateWordField、
   `0x8C` CreateByteField、`0x8D` CreateBitField、`0x8F` CreateQWordField、`0x5B13`
   CreateField 绑定为 Buffer 位段引用；读按位抽取，`Store` 回写 Buffer 对应位。越界、
   源非 Buffer、位宽 >64、负索引、非对齐跨 64 位窗口均返回明确错误码，不填猜测值。
3. **P1 占位**：`Stall(0x5B21)` / `Sleep(0x5B22)` 解析参数后作为有界占位返回 0，**不做
   真实墙钟等待**；`Wait(0x5B25)` / `Signal(0x5B24)` 仍显式 `AML_E_UNSUP`。
4. 跳过未知对象只扩大建库覆盖面，**不会让电池看起来成功**：缺字段仍 NOTFOUND →
   DECLARED/UNKNOWN，不产生假百分比。

## 2. before / after 汇总（34 张表：22 真实/还原 [16 raw-binary + 6 reassembled] + 4 固件 + 8 合成）

| 指标 | before（HEAD 快照） | after（工作树） |
| --- | ---: | ---: |
| 探针 build_rc=0/OK 表数 | 34 | 34 |
| 节点总数（nodes 求和） | 248 | 350 |
| 节点数 >0 的表数 | 16 | 29 |
| 枚举 PNP0C0A 总数 | 8（全部合成夹具） | 8（全部合成夹具） |
| 枚举 ACPI0003 总数 | 0 | 0 |
| 枚举 PNP0C09 总数 | 0 | 0 |

逐表 before/after 节点数与枚举对比见 `before-after.tsv`；逐表探针原始日志见
`logs/probe-raw.log`，汇总表见 `probe-results.tsv`。

要点：
- 建库覆盖面扩大：13 张原本节点为 0 的表现在登记出 1–14 个节点（Surface 系列 DSDT/SSDT、
  T450s/X1C6 电池补丁 SSDT、dell-xps9350 SSDT、16afr10h SSDT1 等）。
- **真实/还原表仍未枚举出任何 PNP0C0A / ACPI0003 / PNP0C09**：设备声明仍落在本轮尚未
  覆盖的表达式/控制结构之后，枚举链路未打通；这不是“电池存在”的证据。
- 合成夹具 `syn-create-dword` 的 `_BST` 由 before `-3/UNSUP` 变为 after `0/OK`（DWord
  拆包链路打通）；主机端到端测试中该夹具电池采样进 VALID（7500/10000 → 75%）。

## 3. 仍未支持 / 未打通的结构（按语料出现频次，详见 `census-opcodes.tsv`）

> 注：`census-opcodes.tsv` 的 support 标签沿用 HEAD 基线支持矩阵的静态口径；Create*Field
> 在 after 解释器已支持，表内频次仅为语料出现次数，不代表当前仍判 UNSUP。

- 表达式/转换：`Concatenate(0x73)`、`Divide(0x78)`（余数/商目标分派未完整）、`Mid(0x9E)`、
  `ObjectType(0x8E)`、`RefOf(0x71)` 完整语义、`Match(0x89)`、`ToString(0x9C)`、
  `ToBuffer(0x96)`、`ToHexString(0x98)`、`FindSetLeft/RightBit(0x81/0x82)`。
- 控制/通知：`Notify(0x86)`、`Wait(0x5B25)`、`Signal(0x5B24)`、`CondRefOf(0x5B12)`
  仅部分实现（目标占位未写入）。
- 建库期未识别扩展：`Load(0x5B20)`、`DataRegion(0x5B88)`、`Fatal(0x5B32)`、
  `Timer(0x5B33)` 等按长度跳过或硬中止；个别表仍因 PkgLength 损坏/未知扩展中止在早期偏移。
- EC 写事务未实现：IndexField/BankField 的 index/data/bank 寄存器落在 EmbeddedControl 上
  仍 `AML_E_UNSUP`；FACS Global Lock 未实现（单线程占位）。

## 4. 复现方式

```bash
# 语料缺失时先重新拉取（网络可用）：
python3 tools/regress/prepare_aml_corpus.py
# 工作树源码版探针（不覆盖基线证据目录）：
bash tools/regress/run_aml_compat_probe.sh
```

产物：`probe-results.tsv`（after 逐表结果）、`before-after.tsv`（与基线逐表对比）、
`census-opcodes.tsv` / `census-fields.tsv` / `census-per-table.json`（离线结构普查）、
`logs/probe-raw.log`（探针原始输出）、`logs/probe-build.log`（编译日志）、
`logs/build-stop-analysis.tsv`（HEAD 口径静态首拒对象分析，仅供参照）。

## 5. 主机测试回归

`bash tools/host-tests/run_host_tests.sh`：19 通过 / 0 失败；本轮新增
`create_field`（24 checks），既有 18 个测试与断言未回退。`python3 tools/check_docs.py`
`warns=0 errors=0`。

## 6. QEMU 合成表取证（`qemu/`）

- `qemu/create-dword/`：CreateDWordField 拆包 `_BST`，串口 `pct=75 rem=7500 full=10000`，
  进 VALID；截图 `desktop_1/2.png`。
- `qemu/idxfield-ec/`：EC 上 IndexField，`bst failed code=-3`（UNSUP），保持 `declared`，
  无假百分比；截图 `desktop_1/2.png`。
- QEMU 无 ACPI EC 设备；以上均为合成表链路取证，**非真机验证**。详见 `qemu/README.md`。
