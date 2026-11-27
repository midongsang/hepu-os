# 电池状态与机型识别方案（2026-11-27）

## 0. 结论

不要把“台式机 / 笔记本”当成电池图标的开关。正确顺序是：

1. **先保证不撒谎**：没有电池证据时，不显示 100%，不触发低电量策略；
2. **再判断电池设备是否存在**：ACPI `PNP0C0A`（`_STA` present/enabled）是主判据；
3. **机型只做布局提示**：SMBIOS Type 3 Chassis Type 用于决定“预留/不预留电池位”，不用于伪造电量；
4. **最后才读电量**：`_BIX`/`_BIF` + `_BST` 可用时才进入 `VALID`，否则显示“--/未知”。

当前赫普的状态是占位实现，不是检测：

- `kernel/arch/x86_64/hpdesk.c:8578-8579` 把 `battery_percent` 固定为 `100`，`battery_charging=0`；
- `hpdesk.c:4694` 顶栏无条件绘制电池图标；
- `hpdesk.c:8719` 无条件调用 `hs_battery_render`；
- `kernel/arch/x86/device_support.c:71` 把电池硬编码成 `no battery(台式机)`；
- ACPI 侧只解析 MADT / HPET，没有 FADT→DSDT、SSDT 枚举，也没有 AML 解释器。

因此 QEMU 台式机显示 100% 电池不是检测错误，是硬编码占位。

## 1. 判据设计

### 1.1 电池状态机

```text
ABSENT      没有发现可用的电池设备
DECLARED    固件里声明了 PNP0C0A，但还没读到可信电量
VALID       已读到 _BIX/_BIF + _BST，电量可信
UNKNOWN     有电池设备，但当前读不到/方法执行失败
```

规则：

- `ABSENT`：不画电池图标，不参与低电量关机；
- `DECLARED/UNKNOWN`：可以画电池轮廓 + `--`，但不能画 100%；
- `VALID`：画百分比/充电状态，低电量策略才生效；
- 任何状态下都不允许把“读不到”回退成 100%。

### 1.2 判据优先级

| 信号 | 用途 | 是否可作为电池存在判据 |
|---|---|---|
| ACPI `PNP0C0A` + `_STA` present/enabled | 电池设备是否存在 | **是，主判据** |
| ACPI `ACPI0003` | 交流适配器/插电状态 | 否，只决定充电图标 |
| SMBIOS Type 3 Chassis Type | 台式/笔记本/平板外形提示 | 否，只做布局提示 |
| DSDT/SSDT 原始字节中出现 `PNP0C0A` | 低成本“声明存在”探测 | 仅 M1 过渡判据，标 `DECLARED` |
| 用户配置 `hepu.battery=auto/present/absent` | 无 ACPI 的测试机/工控机兜底 | 仅调试，不作为默认证据 |

SMBIOS 只能回答“厂商说这是什么形状”，不能回答“现在有没有电池”。台式机接 UPS、笔记本拔电池、虚拟机 Chassis 为 `Other` 都会让纯机型判断出错。

## 2. 分阶段计划

### M0：先移除假 100%（0.5 天，P0）

目标：QEMU 默认台式机不再显示 100% 电池；这是用户可见的第一处修正。

改动：

- `hpdesk.h` 增加 `battery_state`、`battery_percent_valid` 字段（或独立 `hs_battery_status_t`，避免改共享结构布局）；
- `hpdesk.c:hs_status_init` 不再写 `battery_percent=100`，默认 `ABSENT/UNKNOWN`；
- `hpdesk.c:4694` 顶栏按 `battery_state != ABSENT` 条件绘制；隐藏时 WiFi/音量/齿轮/电源整体右移，不能留空位；
- `hpdesk.c:hs_battery_render` 增加 guard：`ABSENT` 直接 return，`DECLARED/UNKNOWN` 画 `--`，只有 `VALID` 画百分比；
- 32 位 `device_support.c` 的“no battery(台式机)”改成读取同一状态，不再写死。

验收：

- QEMU `pc` 默认启动：顶栏无电池图标、无 `100%`；
- 串口出现 `[BAT] state=absent reason=no-acpi-battery`；
- 64 位桌面截图与旧图对比，右侧图标间距连续；
- 32 位冒烟不回归。

### M1：固件层“有没有电池”识别（1–2 天，P1）

目标：不实现完整 AML，也能区分“有电池设备”和“没有”，并给 M2 留接口。

改动：

- 新增 `kernel/arch/x86_64/firmware_info.c/.h`（名称可调）：
  - SMBIOS：扫描 `_SM_` / `_SM3_` entry point，读 Type 3 Chassis Type；
  - ACPI：从 RSDP→RSDT/XSDT 枚举 `FACP`、`DSDT`、`SSDT`；
  - `FACP` 取 `DSDT`/`X_DSDT`，把 DSDT 与所有 SSDT 当只读 blob；
  - 在 DSDT/SSDT 中扫描 AML 字符串 `PNP0C0A` / `ACPI0003`，记录“声明存在”；
- `hpdesk.c:hs_status_update` 调 `firmware_info_poll()`：
  - 找到 `PNP0C0A` → `DECLARED`；
  - 没找到 → `ABSENT`；
  - Chassis Type 只写入 `form_factor`，不直接决定电池状态；
- 串口打印固定格式，便于取证：
  - `[BAT] form_factor=desktop/laptop/other`
  - `[BAT] acpi_battery=declared/absent`
  - `[BAT] state=declared/absent`
- 启动选项 `hepu.battery=auto|present|absent` 只用于测试夹具；默认 `auto`。

为什么先做原始扫描而不是 AML：`PNP0C0A` 是 `_HID` 的固定字符串，扫描能回答“固件是否声明了电池设备”，成本低、可回滚；它不能回答 `_STA` 是否启用、也不能读电量，所以状态只能到 `DECLARED`，不能到 `VALID`。

验收：

- QEMU 默认 `pc`：`form_factor=desktop/other`、`acpi_battery=absent`、顶栏无电池；
- QEMU 注入含 `PNP0C0A` 的 SSDT：`acpi_battery=declared`、顶栏 `--`，不出现 100%；
- 真笔记本（若可用）：`acpi_battery=declared`、顶栏 `--`；
- 真台式机：`absent`、无图标；
- 构建 64/32，`run_tests` 通过，无电池探针默认不编入。

### M2：最小 ACPI 电池读数（3–5 天，P1/P2）

目标：从 `DECLARED` 走到 `VALID`，读真实电量/充电状态；这是本方案的主要技术风险。

范围只做“状态读取”，不做充电控制：

- 最小 AML 执行器：
  - 表加载：FADT→DSDT + RSDT/XSDT→SSDT；
  - 命名空间：`Scope`、`Device`、`Name`、`Method`、`Package`、`Buffer`、`Return`、`If/Else`、`Store`、`Add/Subtract/Multiply/Divide` 等电池方法常用 opcode；
  - 对象解析：Integer/String/Buffer/Package/Reference；
  - 方法调用：只支持无参/少量参数，超时/异常返回 `UNKNOWN`；
- 电池对象发现：按 `_HID=PNP0C0A` 找到设备，读 `_STA`；
- 电量：
  - 优先 `_BIX`，回退 `_BIF`：设计容量、最后一次满容量、单位（mAh/mWh）；
  - `_BST`：放电/充电状态、剩余容量、当前电压；
  - `ACPI0003` 的 `_PSR` 判断插电；
- 失败策略：任何方法执行失败/超时/返回类型不符 → `UNKNOWN`，保留 `--`；绝不再回退 100%；
- 轮询：桌面 30s 一次，低电量阈值只在 `VALID` 时触发；不做 SCI 中断处理（当前 ACPI 无 SCI 路径）。

验收：

- QEMU 合成 SSDT（含 `PNP0C0A`、`_STA`、`_BIX`、`_BST`）返回 85%、放电中；
- 合成低电量 10%：只触发一次低电量提示，不重复刷屏；
- 合成 `_BST` 返回错误：状态 `UNKNOWN`、显示 `--`、不触发关机；
- 真笔记本：百分比与固件/BIOS 显示差异在可解释范围内（记录原始 `_BST` 值）；
- 真台式机：仍 `ABSENT`，无电池图标。

### M3：UI / 电源策略 / 32 位对齐（1–2 天，P2）

- 64 位顶栏：电池图标、百分比、充电闪电、低电量红色；
- 32 位 `hpdesk32`/`device_support`：读同一状态源，删除“no battery(台式机)”硬编码；
- Settings：只有 `battery_state != ABSENT` 才显示“电池百分比”开关；
- 电源策略：`app_freeze` / `power_optimizer` 的电池字段只在 `VALID` 时更新；`ABSENT` 时保持 AC 模式；
- 日志与 shell：`battery` / `status` 输出 `state/form_factor/percent/charging`，方便现场取证。

验收：

- 桌面 QEMU 截图：无电池图标，顶栏右侧无空洞；
- 笔记本 QEMU 合成电池截图：`--`/85%/充电三种状态；
- 32 位冒烟 `serial=1 initrd=1 net=1 desk=1 fault=0` 不回归；
- `run_tests` host 8/0、gfx 8 项、p3-security 2/0。

### M4：真机与边界（0.5–2 天，取决于硬件）

- 真笔记本：记录 SMBIOS Type 3、DSDT/SSDT 中的 `PNP0C0A`、`_BIX`/`_BST` 原始值；
- 真台式机：确认无电池对象、无图标；
- 台式机 + UPS（若有）：确认“有电池就显示”，不因 Chassis=Desktop 被错误隐藏；
- 笔记本拔电池/禁用电池：确认 `ABSENT` 或 `UNKNOWN`，不显示历史 100%。

## 3. 代码落点

| 文件 | 改动 |
|---|---|
| `kernel/arch/x86_64/hpdesk.h` | 电池状态字段/枚举；尽量独立结构，避免动共享布局 |
| `kernel/arch/x86_64/hpdesk.c` | `hs_status_init/update`、顶栏条件渲染、`hs_battery_render` guard |
| `kernel/arch/x86_64/firmware_info.c/.h` | 新增：SMBIOS Type 3 + ACPI 表枚举 + `PNP0C0A` 声明扫描 |
| `kernel/arch/x86_64/acpi.c/.h` | 扩展 FADT/DSDT/SSDT 定位；保留现有 MADT/HPET 路径 |
| `kernel/arch/x86_64/aml64.c/.h` | M2 新增：极简 AML 解释器（命名空间 + 电池方法所需 opcode） |
| `kernel/arch/x86_64/acpi_battery.c/.h` | M2 新增：`_STA/_BIX/_BIF/_BST` 求值与百分比可信门槛 |
| `kernel/arch/x86_64/Makefile` | 新源文件；探针/测试开关默认关闭 |
| `kernel/arch/x86/device_support.c` | 删除硬编码“台式机无电池”，接同一状态源 |
| `docs/status/evidence-battery-2026-11-27/` | 截图、串口、QEMU SSDT、宿主核对 |
| `tools/regress/` | 合成 SSDT 生成器 + QEMU `-acpitable` 启动脚本 |

## 4. 验收矩阵

| 场景 | 期望 form_factor | 期望 battery_state | 顶栏 |
|---|---|---|---|
| QEMU 默认 `pc` | desktop/other | ABSENT | 无电池图标 |
| QEMU + 合成 `PNP0C0A` SSDT | other | DECLARED | 电池轮廓 + `--` |
| QEMU + 合成 `_BIX/_BST` 85% | other | VALID | 85%，放电 |
| QEMU + 合成 `_BST` 错误 | other | UNKNOWN | `--`，不关机 |
| 真笔记本有电池 | laptop | VALID/DECLARED | 百分比或 `--` |
| 真笔记本拔电池 | laptop | ABSENT | 无图标 |
| 真台式机 | desktop | ABSENT | 无图标 |
| 台式机 + UPS | desktop | VALID/DECLARED | 有电池就显示 |

## 5. 风险与回滚

1. **AML 复杂度**：电池方法可能依赖 EC 字段、`_OSI`、Mutex、Notify；最小执行器不可能覆盖全部 DSDT。策略是“能读就读，读不到就 UNKNOWN”，不把失败伪装成 100%。
2. **表地址/分页**：DSDT/SSDT 可能在 >1GB 物理地址，统一走 `vmm64_phys_to_ptr`；校验 header length/checksum，越界即拒绝。
3. **SMBIOS 不可靠**：只做 form_factor 提示；即使 Chassis 写错，也不改变“电池存在”判据。
4. **共享结构布局**：赫普有过“给共享结构加字段导致 BSS 位移”的教训；电池状态放独立结构或放结构尾部，改后跑 64/32 全量冒烟。
5. **QEMU 合成表不代表真机**：`-acpitable` SSDT 只验证解析/UI 链路；真机结论必须单独取证，未上真机不写“已修复”。
6. **回滚**：启动项 `hepu.battery=off` 可强制回到旧行为（仅用于排障，不作为默认）；M1/M2 各自独立提交，可单独 revert。

## 6. 建议提交拆分

1. `docs(plan): 电池状态与机型识别方案`（本文）
2. `fix(desktop): 移除电池 100% 占位并按状态渲染`（M0）
3. `feat(firmware): SMBIOS Type 3 + ACPI PNP0C0A 声明探测`（M1）
4. `feat(acpi): 最小电池 _STA/_BIX/_BIF/_BST 读取`（M2）
5. `fix(ui): 电池/充电/低电量与 32 位状态对齐`（M3）
6. `test: QEMU 合成 SSDT 与真机取证`（M4）

## 7. 实施状态（2026-11-27）

- **M0 已完成**：`battery_percent=100` 占位移除；`hs_battery_state_t` 接入
  `hs_status_init/update`；顶栏按 `ABSENT` 条件绘制；`DECLARED/UNKNOWN` 只画
  轮廓 + 两道短线；32 位 `device_support.c` 的“no battery(台式机)”改为“ACPI 未探测”。
- **M1 已完成（仅声明探测）**：新增 `firmware_info.c/.h`，解析 SMBIOS
  `_SM_`/`_SM3_` Type 3、ACPI RSDP→RSDT/XSDT→FACP→DSDT/SSDT，扫描
  `PNP0C0A`/`ACPI0003`（ASCII + AML 压缩 EISA ID）。状态只到 `DECLARED/ABSENT/UNKNOWN`，
  **没有 `VALID`，不读真实百分比**。
- **M1.5 图标层已闭环（2026-11-27）**：用户 2026-11-11 图标批次里的横向电池
  （`assets/icons-status64/p64_battery64.png`）此前被内嵌成 `hs_ico64_battery64`，
  但那张图把“约 69% 电量”烧进了像素——直接贴顶栏就是一张固定谎报 69% 的图。
  `tools/gen_battery_glyph.py` 按素材实测几何重建**轮廓层**（`hs_battery_glyph.h`），
  顶栏改用它与 wifi/音量/齿轮/电源同一条 `hs_icon_bmp64()` 覆盖采样管线；
  电量由状态驱动（`hs_battery_ui_mode` / `hs_battery_fill_px`，纯函数 + 主机端单测）。
  同批次的 `assets/icons-v2/128_battery_std.png` 是实心竖版，24px 下成黑砖，确认不可用。
- **证据**：`docs/status/evidence-battery-2026-11-27/`。QEMU 默认
  `state=absent`、顶栏无电池；注入合成 `PNP0C0A` SSDT 后 `state=declared`、
  顶栏轮廓 + `--`；宿主 `host_check.py` PASS。图标层另见
  `icon-topbar/`：声明态多出的电池段宽 25px、高 12px，右侧三个图标逐像素未移动，
  `icon_check.py` PASS。64/32 构建通过，host-tests 9/0，32 位冒烟 `fault=0`。
- **M1.6 面板与设置页已闭环（2026-11-27）**：顶栏电池图标点击展开面板，
  显示剩余电量（`VALID` 才有数字，否则 `--`）与 ACPI 状态文案；面板右下角是
  “电池和电源计划”入口，点击直接进 Settings 的 `Battery` 页（第 7 个导航项）。
  面板/页面几何、命中、状态文案都单一来源（`hs_battery_icon_rect` /
  `hs_battery_panel_geom`），绘制与点击不会各算一套。电源计划一栏明确写
  “均衡（固定）”：64 位没有调度策略引擎，不做没有实际效果的开关。
- **M2 最小版已闭环（2026-11-27）**：新增 `aml64.c/.h`（极简 AML 解释器）与
  `acpi_battery.c/.h`，QEMU 注入含 `PNP0C0A` + `_STA/_BIF/_BST` 的合成 SSDT 后，
  顶栏/面板/Settings 三处都显示真实 `57%` 与 `Charging: Yes`；`_BIX` 夹具同值。
  读不出时仍 `--`（缺 `_BST` → `bst failed code=-1`，EC 字段 → `-3` UNSUP），
  没有回退 100%。`m2-valid/valid_check.py` 21 项 PASS。
  **边界**：EC 事务、IndexField/BankField、写字段、多电池、剩余时间、健康度、
  `_PSR` 均未实现；真机 EC 型电池仍会停在 `DECLARED`——这是 M2-full 的事。
- **未做**：M2-full（EC 事务/多电池/健康度等）、M3（完整 32 位 UI/电源策略）、M4（真机）。
  本次 QEMU/OVMF 未提供 legacy SMBIOS entry，`form_factor=unknown` 只说明
  “本夹具没读到”，不等于真机型号判断已验证。

## 8. 时间估算

- M0：0.5 天
- M1：1–2 天
- M2：3–5 天（主要风险）
- M3：1–2 天
- M4：0.5–2 天（取决于是否有真笔记本/台式机）

合计约 **6–11 个工作日**；如果只做“台式机不再显示假电池”，M0+M1 约 **2–3 天** 即可先交付。
