# ACPI 语料兼容性审计报告（2026-11-27）

## 0. 元信息与边界声明

- 审计对象：极简 AML 解释器 `kernel/arch/x86_64/aml64.c`（HEAD 快照），电池侧
  `kernel/arch/x86_64/acpi_battery.c`（PNP0C0A 枚举 + `_STA/_BIF/_BIX/_BST`）。
- 审计基线：仓库 `main` 分支，HEAD `a9b3b594365c4abfd9ab21305dd3f8a1789eb4da`。
- 探针编译使用 **HEAD 快照**（`git show HEAD:kernel/arch/x86_64/aml64.c/.h`、
  `ec64.c/.h` 导出到临时目录），未使用工作树中并行改动的版本。快照源文件 SHA256：
  - `aml64.c`：`83593d04dc201fba8c3d097bb1e0c59c7eef5cfd46998a6321c108679941d7ff`
  - `aml64.h`：`d5f8ac0a4e5274e60b11fff9c82c75637c25d1d5a6fc7ff4967138d05a3492b4`
  - `ec64.c`：`2e7cc40ea14f0d7781e6d3762bfabd8246a346592694836f8800cb0ad7644b70`
- 本次审计 **未修改** `kernel/` 下任何源码，**未 commit / push**；证据目录内只新增
  文件。工作树前后状态见 `logs/git-status-before.txt` / `logs/git-status-after.txt`
  （其中 `kernel/arch/x86_64/aml64.c` 等的 `M` 标记为并行任务改动，非本审计产生）。
- **语料性质声明**：本报告结论分三类来源，不得混称“真机已验证”：
  - `raw-binary`：公开 GitHub 仓库中直接拿到的笔记本/机型二进制 AML（来源 URL +
    SHA256 见清单）；
  - `reassembled`：从公开 acpidump 十六进制文本（`.out`）还原出的二进制表，
    **不是固件原始二进制**；
  - `firmware`：本地 QEMU 固件镜像（OVMF / SeaBIOS）内提取的表模板，
    **不是笔记本电池固件**，不含电池设备；
  - `synthetic`：本仓库脚本生成的贴近真实写法的合成夹具，**不是任何真机固件**。
- 环境无 `iasl`，只处理二进制 AML；公开仓库中的 `.dsl/.asl` 反编译文本未纳入语料。

## 1. 结论摘要（TL;DR）

1. **真实 DSDT/SSDT 在建库阶段即大面积中止**：22 张真实/还原表 + 4 张固件表中，
   命名空间只注册到 0–97 个节点（真机 DSDT 通常含数百至上千节点），
   **没有一张表枚举出 PNP0C0A / ACPI0003 / PNP0C09 设备**，电池方法路径在真实
   语料上不可达。证据：`probe-results.tsv`、`logs/build-stop-analysis.tsv`。
2. 除已知 IndexField/BankField 跳过外，当前解释器在真实语料上的主要阻断点为：
   - **建库期**：外层控制对象（`If`/`While` 等）与若干未支持扩展对象
     （`CreateField 0x5B13`、`Load 0x5B20`、`DataRegion 0x5B88`、`Fatal 0x5B32`、
     `Timer 0x5B33` 等）未进入分发；且建库失败码被上层吞掉，调用方无法区分
     “空表”与“中途中止”。
   - **求值期**：`CreateDWord/Word/Byte/Bit/QWordField`（0x8A–0x8D/0x8F）与
     `CreateField 0x5B13` 缓冲拆包未实现——这是 `_BST/_BIX` 从状态缓冲取字段的
     常见写法；EC 轮询原语 `Stall/Sleep/Wait/Signal` 未实现；字符串/容器操作
     （`Concatenate/Divide/RefOf/Notify/Match/ToString/Mid/Continue/ObjectType` 等）
     未实现。
3. 合成夹具复现了三类明确失败：缓冲拆包 `_BST → UNSUP`；Bank/Index/ExtField
   字段未注册 → `NOTFOUND`；`CondRefOf` 守卫路径 → `BADCODE`。基线夹具
   （`syn-baseline.aml`）`_STA/_BIF/_BST` 求值 OK，证明解释器现有子集本身可工作。
4. 即使求值返回 OK，`_OSI` 恒返回 0、Mutex 为单线程占位、宿主桩无真实 EC，
   这些结果只能算“路径可走通”，**数据可信度未在真机验证**。

## 2. 语料清单与来源

完整机器可读清单：`corpus-manifest.tsv`（列：name, kind, status, sig, size,
sha256, battery, ac, ec, note, source_url），同名 `.json` 为 JSON 版。

### 2.1 公开二进制 / 还原表（真实机型转储，非本仓库生成）

| 类别 | 表数量 | 来源说明 |
|---|---|---|
| `raw-binary` | 16 | 公开 GitHub 仓库中的 `.dat`（acpidump -b 输出）与 `.aml`（Clover/OpenCore origin 转储及补丁 SSDT） |
| `reassembled` | 6 | linux-surface/acpidumps 的 `.out` 十六进制文本按 ACPI 头长度还原 |
| `firmware` | 4 | 本地 SeaBIOS `bios.bin` 内嵌静态表模板（OVMF_CODE/VARS 镜像内未定位到合法 DSDT/SSDT 头） |
| `synthetic` | 8 | `tools/regress/make_corpus_fixtures.py` 生成，见 `fixtures/` |

来源仓库与逐表 URL、SHA256、字节数、是否含电池/适配器/EC 标识，均以
`corpus-manifest.tsv` 为准。注意：

- 4 张 DSDT 的字节扫描命中 `ACPI0003`（交流适配器）标识，但因建库中止，命名空间
  枚举数仍为 0（见 `probe-results.tsv` 的 `ac` 列）。
- 真实语料中未出现 ASCII `PNP0C0A` 字符串；电池设备可能位于未采集到的 SSDT 中
  （见第 9 节“未验证项”）。
- 第三方二进制 **未进入仓库**：下载件位于会话临时目录，证据目录只保留 URL、
  SHA256 与小体量合成夹具。

### 2.2 固件语料说明（非笔记本电池固件）

- `OVMF_CODE_4M.fd` / `OVMF_VARS_4M.fd`：扫描 `DSDT`/`SSDT` 签名 + 长度 + 校验和，
  未发现合法表头（OVMF 的 ACPI 表在运行时由 DXE 生成，固件镜像内不含静态表）。
- SeaBIOS `bios.bin`：提取到 1 张 DSDT + 3 张 SSDT 模板（校验和为 0、长度合法），
  内容为 QEMU 虚拟平台（ISA/PCI 定时器等），**不含 PNP0C0A/ACPI0003/PNP0C09**。
- 这 4 张表仅用于验证探针在非电池固件上的行为，不能代表笔记本 ACPI 环境。

## 3. 探针方法（只读）

- 探针源码：`tools/host-tests/aml_corpus_probe.c`；运行脚本：
  `tools/regress/run_aml_corpus_probe.sh`；普查脚本：`tools/regress/aml_census.py`；
  建库中止定位：`tools/regress/build_stop_check.py`；结果汇总：
  `tools/regress/summarize_probe.py`；语料准备：`tools/regress/prepare_aml_corpus.py`；
  夹具生成：`tools/regress/make_corpus_fixtures.py`。
- 逐表流程：读入二进制 → 校验 ACPI 头签名/长度 → `aml_ns_reset()` →
  `aml_ns_build()` → 记录返回码、`aml_ns_nodes()`、`aml_ns_ready()` →
  按 EISA/字符串枚举 PNP0C0A / ACPI0003 / PNP0C09 → 对电池设备求值
  `_STA/_BIF/_BIX/_BST`，记录返回码（OK/NOTFOUND/BADCODE/UNSUP/LIMIT/ARG）与值形态。
- 已知接口限制：HEAD 公开 API 未暴露“建库失败偏移”与 degraded 计数；探针记录
  接口可取字段，首个未支持对象的偏移由离线检查器/普查脚本另行给出，两者口径在
  第 7 节分开标注。
- 编译：`gcc -O1 -g -fsanitize=address,undefined -DAML_HOST_TEST`，无告警
  （`logs/probe-build.log`）。

## 4. 逐表结果汇总

完整表：`probe-results.tsv`（34 行）。要点：

- **真实/还原表（22 张）**：`build_rc` 均为 `0/OK`（注意：`aml_ns_build` 内部吞掉了
  `parse_termlist` 的中止码，返回 0 不代表整表解析成功）；节点数 0–97；设备枚举
  `bat/ac/ec` 全 0；无电池方法被求值（`_STA/_BIF/_BIX/_BST` 列 `-`）。
- **固件表（4 张）**：节点数 0，无设备枚举。
- **合成夹具（8 张）**：

| 夹具 | 覆盖写法 | 求值结果（摘录） |
|---|---|---|
| `syn-baseline.aml` | 现有子集基线：`_STA/_BIF/_BST` 纯 Package | `_STA OK=15`，`_BIF OK pkg5`，`_BST OK pkg4` |
| `syn-ec-region.aml` | EC OperationRegion(space 3) + 16 位 Field | `_BST UNSUP`（宿主桩无真实 EC，EC 读 fail-closed） |
| `syn-bankfield.aml` | BankField(0x5B87) 包 EC 字段 | 字段未注册，`_BIF/_BST NOTFOUND` |
| `syn-indexfield.aml` | IndexField(0x5B86) | 字段未注册，`_BIF/_BST NOTFOUND` |
| `syn-extfield.aml` | ExtField/DataRegion(0x5B88) | 外层未支持，`_BIF/_BST NOTFOUND` |
| `syn-create-dword.aml` | 方法内 `CreateDWordField(0x8A)` 拆包 | `_BST UNSUP` |
| `syn-osi-branch.aml` | `If(_OSI(...))` 双分支 | `_STA OK=15`（走 Else；`_OSI` 恒 0） |
| `syn-condrefof.aml` | `CondRefOf(0x5B12)` 守卫可选方法 | `_STA BADCODE` |

原始逐表日志：`logs/probe-raw.log`；建库中止定位：`logs/build-stop-analysis.tsv`。

## 5. 失败原因归类

### 5.1 建库失败（命名空间不完整，设备不可达）

- **外层未识别对象即中止扫描**：`parse_termlist` 分发只接受 `Scope/Method/Name/
  Alias/External` 与已实现扩展；其余外层操作码直接 `return -1`，本表扫描停止，
  已解析部分保留。`build-stop-analysis.tsv` 中多张表首个被拒为外层 `If(0xA0)`
  （如 surface 系列、dell-xps、SeaBIOS 模板、部分联想 SSDT）。
- **未支持的外层扩展对象**：`CreateField 0x5B13`（2 张表外层命中）、
  `Load 0x5B20`、`DataRegion 0x5B88`、`Fatal 0x5B32`、`Timer 0x5B33`、
  `Signal 0x5B24`、`Reset 0x5B26`、`FromBcd/ToBcd 0x5B28/29`、`Unload 0x5B2A`
  （后几项主要来自启发式扫描线索，见第 7 节）。
- **失败码被吞**：`aml_ns_build` 不向上回报 `parse_termlist` 的 `-1`，上层无法
  区分“表本来就空”和“解析到一半失败”；这也是探针 `build_rc` 恒为 0 的原因。
- **IndexField/BankField 整段跳过**：字段节点未注册，后续按名引用必然
  NOTFOUND（已知缺口，合成夹具复现）。
- **Field 位宽 >64** 会计入 `g_degraded`（当前 API 未暴露计数）。

### 5.2 求值失败（命名空间有节点，但方法跑不通）

- **缓冲拆包未实现**：`CreateDWordField 0x8A` / `CreateWordField 0x8B` /
  `CreateByteField 0x8C` / `CreateBitField 0x8D` / `CreateQWordField 0x8F` /
  `CreateField 0x5B13`。真实 `_BST/_BIX` 常把 EC/缓冲数据按字段拆包，合成夹具
  `syn-create-dword.aml` 复现 `_BST → UNSUP`。
- **EC 轮询原语未实现**：`Stall 0x5B21`、`Sleep 0x5B22`、`Wait 0x5B25`、
  `Signal 0x5B24`；EC 忙等循环无法执行。
- **字符串/容器/转换操作未实现**：`Concatenate 0x73`、`Divide 0x78`、
  `RefOf 0x71`、`Notify 0x86`、`Match 0x89`、`ToBuffer 0x96`、
  `ToDecimalString 0x97`、`ToHexString 0x98`、`ToString 0x9C`、`Mid 0x9E`、
  `Continue 0x9F`、`ObjectType 0x8E`。
- **CondRefOf 仅部分实现**：现有代码只回存在性布尔，目标占位语义不完整，
  `syn-condrefof.aml` 的 `_STA → BADCODE`。
- **EC 字段读**：宿主测试桩无真实 EC（IBF 恒置位 → fail-closed），
  `syn-ec-region.aml` 的 `_BST → UNSUP` 是桩环境行为，不能等同于真机 EC 路径结论。

### 5.3 数据不可信（路径通了，但结果不能当真值）

- `_OSI` 恒返回 0：固件可能选择非 Windows 分支；即便求值成功，分支结果与真机
  固件预期可能不同。
- Mutex 为单线程占位，`Acquire` 不做真实墙钟等待。
- 未在真实 EC / 真机固件上跑过枚举与采样；宿主桩下的 OK 只证明语法路径可走通。

## 6. 修复优先级建议（P0/P1/P2）

| 优先级 | 缺口 | 证据 | 理由与影响 |
|---|---|---|---|
| **P0-1** | 建库期容错：未支持外层对象按 `pkg_length` 跳过并记录降级，而不是整表中止；把 `parse_termlist` 失败码透出到建库结果（节点数/中止偏移/原因） | `logs/build-stop-analysis.tsv`；`probe-results.tsv` 节点数 0–97 | 真实表当前在首个未支持对象处即截断，电池/适配器设备完全不可达；不解决这一层，后续求值改进无法在真实语料上验证 |
| **P0-2** | 外层控制对象进入分发：`If 0xA0` / `While 0xA2` 出现在外层作用域时正确跳过/递归其包体；补齐外层扩展白名单（`CreateField 0x5B13`、`Load 0x5B20`、`DataRegion 0x5B88`、`Fatal 0x5B32`、`Timer 0x5B33` 等） | `census-summary.md` A 节；`build-stop-analysis.tsv` | 多张真实表首拒对象即外层 `If`；这是命名空间大面积缺失的直接原因 |
| **P0-3** | 求值期实现 `CreateDWord/Word/Byte/Bit/QWordField` 与 `CreateField 0x5B13`（缓冲/字段偏移/字段名注册与读写） | `syn-create-dword.aml`：`_BST UNSUP`；`census-opcodes.tsv` eval-term 段 | `_BST/_BIX` 从状态缓冲取电量/电压/电流字段的核心写法；不实现则电池状态无法拆包 |
| **P1-1** | 真正解析并注册 `IndexField` / `BankField` 从属字段（替代整段跳过）；`ExtField/DataRegion 0x5B88` 至少可跳过并登记 | `syn-bankfield` / `syn-indexfield` / `syn-extfield`：字段 NOTFOUND | EC 字段经索引/银行寄存器访问是笔记本 EC 常见布局；当前跳过导致引用全部落空 |
| **P1-2** | EC 轮询原语：`Stall 0x5B21`、`Sleep 0x5B22`、`Wait 0x5B25`、`Signal 0x5B24`（至少占位为可推进的短延时/事件语义） | `census-summary.md` B 节（启发式线索：Sleep/Stall/Wait 涉及多张表） | EC 忙等与事件等待是电池读取循环的一部分；缺它们方法会死循环或报错 |
| **P1-3** | 补齐常用表达式：`Concatenate 0x73`、`Divide 0x78`、`RefOf 0x71`、`Notify 0x86`、`Match 0x89`、`ToString/ToHexString/ToDecimalString/ToBuffer`、`Mid 0x9E`、`Continue 0x9F`、`ObjectType 0x8E` | `census-opcodes.tsv` eval-term 段 | 电池/适配器辅助方法（字符串拼接、查找、类型判断、循环继续）高频使用；按真机语料出现频次逐个补 |
| **P1-4** | 完善 `CondRefOf 0x5B12` 语义（目标占位写入、存在性判断一致） | `syn-condrefof.aml`：`_STA BADCODE` | 可选设备/方法守卫在 SSDT 补丁与多机型固件中常见 |
| **P2-1** | 边缘扩展：废弃 `ShiftLeft/RightBit 0x5B10/11`、`LoadTable 0x5B1F`、`Unload 0x5B2A`、`FromBcd/ToBcd 0x5B28/29`、`Fatal 0x5B32`、`Timer 0x5B33` | `census-opcodes.tsv` 低频次行 | 语料中频次低或仅为线索；按真机复现情况再排期 |
| **P2-2** | 健壮性：Field 位宽 >64 降级路径、`VarPackage` 求值、`BreakPoint 0xCC`、建库/求值错误码细化 | `aml64.h` 现有错误码定义 | 防御性补齐，不阻断主链路 |

## 7. 结构普查口径与线索

- 明细表：`census-opcodes.tsv`（类别 / 操作码 / 名称 / 支持口径 / 次数 / 涉及表）、
  `census-fields.tsv`（Field 位宽分桶）、`census-per-table.json`（逐表首未支持对象）、
  `census-summary.md`（排序摘要）。
- **可信度分级**：
  - `build-ext`（结构化外层遍历的 0x5B 扩展对）：可信度较高；
  - `eval-ext*` / `eval-term*`（全表原始字节启发式扫描）：**无法区分代码与数据**，
    频次只作排期线索，不能单独作为缺口结论；
  - `build-term` 单字节计数受遍历失同步影响，同样仅作线索；表中
    `UnknownExt??` 基本可判定为遍历失同步落到数据字节上的伪命中，不作为缺口依据。
- 操作码命名与编号对照 ACPICA `source/include/acpi/amlcode.h`；支持口径对照 HEAD
  `aml64.c` 分发与 `aml64.h` 顶部注释。
- Field 粒度：现有语料中普通 `Field` 条目位宽以 ≤8 / 9–16 / 17–32 / 33–64 为主，
  未观察到 >64 位字段；详见 `census-fields.tsv`。

## 8. 重跑命令

```bash
cd /home/user/Doubao/chats/38452875115179010/hepu-os

# 0) 确认基线（不应有本审计产生的改动；不 commit / push）
git rev-parse HEAD          # 期望 a9b3b594365c4abfd9ab21305dd3f8a1789eb4da
git status --short

# 1) 准备真实/还原/固件语料（需要网络；克隆清单见 prepare_aml_corpus.py 顶部
#    RAW_PICKS / OUT_PICKS；下载件落在临时目录，不进仓库）
python3 tools/regress/prepare_aml_corpus.py

# 2) 生成合成夹具（小体量自产文件，写入证据目录 fixtures/）
python3 tools/regress/make_corpus_fixtures.py \
  docs/status/evidence-aml-corpus-2026-11-27/fixtures

# 3) 编译 HEAD 快照探针，逐表建库/枚举/求值，并跑普查与汇总
bash tools/regress/run_aml_corpus_probe.sh
```

环境变量（可选覆盖）：`CORPUS_DIR`（真实/还原/固件语料目录）、`FIXTURE_DIR`
（合成夹具目录）、`EVIDENCE_DIR`（证据输出目录）、`WORK`（编译临时目录）。

产物位置：

- 逐表结果：`docs/status/evidence-aml-corpus-2026-11-27/probe-results.tsv`
- 原始探针日志：`docs/status/evidence-aml-corpus-2026-11-27/logs/probe-raw.log`
- 建库中止定位：`docs/status/evidence-aml-corpus-2026-11-27/logs/build-stop-analysis.tsv`
- 普查明细：`census-opcodes.tsv` / `census-fields.tsv` / `census-summary.md`
- 语料清单：`corpus-manifest.tsv` / `.json`

## 9. 不确定性与未验证项

1. **语料覆盖不足**：公开转储只含 DSDT 与部分 SSDT；电池设备（PNP0C0A）可能位于
   未采集的 SSDT。本次真实语料未出现 `PNP0C0A` 标识，因此“真实电池方法长什么样、
   卡在哪个操作码”没有真机样本直接佐证；P0-3/P1 项的优先级依据是通用 AML 写法
   与合成夹具，不是真机反证。
2. **无 iasl**：`.dsl/.asl` 反编译文本未编译回 AML；`reassembled` 表由十六进制
   文本还原，未与原始固件二进制逐字节校验。
3. **启发式计数污染**：eval 段单字节/双字节频次可能把 Package/Buffer 数据字节
   误判为操作码；报告已将其降级为线索。
4. **检查器偏移口径**：`build-stop-analysis.tsv` 的中止偏移来自独立离线检查器，
   其数据跳过逻辑与解释器实现可能存在细微差异；偏移用于定位线索，不等同于解释器
   内部报错位置。HEAD API 也未暴露失败偏移与 degraded 计数。
5. **固件语料代表性**：OVMF 镜像内无静态 ACPI 表；SeaBIOS 提取件是 QEMU 虚拟
   平台模板，不含 EC/电池；不能据此推断笔记本固件行为。
6. **未做真机/QEMU 实机枚举**：本次全部为主机离线探针；EC 读取、`_REG` 时序、
   互斥竞争均未在真实硬件或带 ACPI EC 的 QEMU 场景验证。
7. **ARM64 样本**：`a16-11m` 为骁龙 ARM64 平台转储，AML 结构与 x86 笔记本 EC
   电池路径不同，仅作结构多样性样本。
8. 并行任务正在改动 `kernel/arch/x86_64/aml64.c/.h`；本审计结论绑定 HEAD
   `a9b3b59` 快照，工作树后续改动可能改变部分结果，重跑时以第 8 节命令重新生成。
