---
title: BOOT_FLOW
status: active
sys_version: 0.63.0
updated: 2026-11-17
owner: architecture
source_of_truth: tools/hepboot/uefi/hepboot_uefi.c, kernel/kernel_main_64.c
supersedes: null
---

# 启动链路（BOOT_FLOW）

> 本文描述当前交付路径（UEFI/OVMF 引导 `HEPUBOOT.EFI` → 内核）的真实流程：
> 引导器三种模式语义、加载屏各阶段对应的实际工作、交接内存约定、锁屏与登录流程。
> 每条结论标注源码位置；代码未实现的部分如实标注，不假设 32/64 对称。
>
> 适用基线：`3d0bd23`（2026-11-17）。legacy BIOS 引导链不在当前交付路径上，
> 见文末「边界与未验证项」。

## 一、三种引导模式（菜单里的代码命名）

引导菜单定义在 `tools/hepboot/uefi/hepboot_uefi.c:840-843`：

| 模式常量 | 值 | 语义 | 加载的内核 | cmdline |
|---|---|---|---|---|
| `MODE_64` | 0 | 正常 64 位图形桌面 | `KERNEL64.BIN` | `hepu.arch=x86_64` |
| `MODE_32` | 1 | 正常 32 位图形桌面 | `KERNEL32.BIN` | `hepu.arch=i386` |
| `MODE_PROTECT` | 2 | 保护模式：只起文本终端，不进图形；兜底用途 | 介质上可用的内核（64 位优先，64 位缺失才退 32 位） | 架构参数 + `hepu.graphics=0` |

要点（`hepboot_uefi.c:1668-1682`）：

- 保护模式**不是第四种内核**，是「用现有内核但不要图形」：选一个真能读到的内核文件，
  再把 `hepu.graphics=0` 交给它。
- 某模式是否可选，判据是 ESP 里对应内核文件是否存在且可读（`kernel_available()`，
  `hepboot_uefi.c:847-854`）；保护模式恒可选（`hepboot_uefi.c:1218`）。
- 上次选择存 UEFI 变量 `HepuBootDefaultMode`（`read_default_mode()`/`save_default_mode()`，
  `hepboot_uefi.c:858-872`）；记忆的模式在当前介质不可用时，退到第一个可用模式。
- 引导失败计数 `HepuBootFailCount` ≥ 3 时，菜单强制落到保护模式
  （`hepboot_uefi.c:1227-1233`、`:977`、`:1041`）；菜单里按 `C` 清零计数
  （`hepboot_uefi.c:1270-1273`）。

> 注意区分：`docs/boot/HEPBOOT_DESIGN.md` 里的 TEXT / VGA / VBE 是**显示后端降级**
> 三档，与本文的 64 / 32 / Protect **引导模式**是两套维度，不要混为一谈。

## 二、引导器侧流程与加载屏阶段

入口 `efi_main()`（`hepboot_uefi.c`）。顺序与对应真实工作：

| 阶段 | 源码位置 | 实际工作 |
|---|---|---|
| 读配置 | `load_config()` `hepboot_uefi.c:804-824`；调用点 `:1658` | 读 ESP `HEPBOOT.CFG`：`timeout` / `default` / `kernel_arch` / `resolution=` / `cmdline` / `expected_crc`。**必须先于 `init_gop()`**：`init_gop` 挑模式要用 `g_config.res_w/res_h`（顺序注释 `:1655-1657`）。 |
| 初始化 GOP | `init_gop()` `:320-449`；调用点 `:1663` | 定位 GOP 协议、枚举模式、按评分选模式；cfg `resolution=WxH` 命中时该模式压过评分（`:426-429`）。 |
| 引导菜单 | `show_boot_menu()` `:1215-1274` | 三模式可选性、倒计时、上下键/数字键选择、失败计数兜底。 |
| 加载屏开场 | `ld_begin(use32, protect)` `:1163-1206` | 图形加载屏初始化；保护/文本模式返回 0，不画图形加载屏（`:1167`）。 |
| 加载内核镜像 | `load_kernel_to_memory()` `:1365`；状态 `:1708-1711` | 把 `KERNEL64.BIN`/`KERNEL32.BIN` 读到 `0x100000`；首选架构缺失时换另一架构再试（`:1714-1723`）。 |
| 校验镜像 | CRC32 `crc32_calc()` `:116-124`；状态 `:1739-1752` | 仅当 cfg 配了 `expected_crc != 0` 才比对；不匹配报错停机。未配 CRC 时不做哈希比对。 |
| 预热内核页 | `:1754-1769` | 逐段触碰镜像每一页，提前进缓存；spinner 节奏对应真实触碰进度。 |
| 交接准备 | 状态 `:1771-1772`；低内存跳转区 `:1806-1817` | 分配 `0x80000` 一带跳转代码区；`GetMemoryMap`（`:1831-1847`）。 |
| 构建 Multiboot Info | `build_multiboot_info()` `:242-`；调用 `:1854` | 在 `0x90000` 构建 multiboot 信息结构（内存映射、framebuffer、cmdline 等）。 |
| 写交接内存 | 见第三节 | 总内存字节、ACPI RSDP、GOP fb 信息块、HLDY 魔数。 |
| 退出引导服务 | `ExitBootServices` 重试循环 `:1890-1903` | 成功后不再调用任何 BS 服务；之后用 VGA 文本打调试输出。 |
| 跳转内核 | `:1960` 起；`jump_to_kernel_64` / `jump_to_kernel_32`（声明 `:173-176`） | 按已解析的 `use32` 选跳转：`!use32` 走 64 位长模式交接，否则走 32 位交接。 |

加载屏状态文案与百分比（`ld_set_status()` `:1135`）：

1. `Loading 64-bit/32-bit kernel image`（10%）
2. `Verifying kernel image`（30%）
3. `Preheating kernel pages`（45% 起，随页数推进）
4. `Preparing to hand off`（92%）

## 三、交接内存约定（引导器 → 内核）

引导器在 `ExitBootServices` 前后把下列固定低内存块写给内核。地址均为物理地址，
低内存恒等映射区。

| 地址 | 内容 | 写入位置 | 读取方 |
|---|---|---|---|
| `0x90000` | `multiboot_info_t`（内存映射 / framebuffer / cmdline 等） | `build_multiboot_info()` `:242` | 64 位 `bootopts64_init()`（`kernel/arch/x86_64/bootopts64.c:68-92`）；32 位 `mb_info`（`kernel/boot/multiboot.c`） |
| `0x6000` | GOP 帧缓冲信息块：`w(u16)@0`、`h(u16)@2`、`pitch(u16)@4`、`bpp(u8)@6`、`fb 低32@8`、`fb 高32@12` | `:1923-1939`、`:1948` | 64 位 `early_fb_text_init_from_bootloader()`（`kernel/arch/x86_64/gfx64.c:1375-1409`） |
| `0x6010` | **HLDY 魔数** `0x484C4459`（ASCII `"HLDY"`），即 `0x6000` 块偏移 16 | `:1930-1934` | 见下方现状说明 |
| `0x6020` | ACPI RSDP 地址（`u64`） | `:1909` | 内核 `acpi_find_rsdp` 优先读此地址 |
| `0x6100` | 枚举模式表：count(`u32`) + count×8 字节（每项 w/u32、h/u32，上限 16 项） | `:1942-1946` | 内核 Display 页列出引导器支持的分辨率 |
| `0x90100` / `0x90104` | 总内存字节数低 32 / 高 32（夹在 256MB–2GB） | `:1861-1867` | 64 位 `pmm64_init` |

### 3.1 `0x6010` HLDY 交接约定（32/64 位现状不对称，照实写）

**约定语义**：HLDY 表示「屏幕上正停着引导器的加载界面」。内核见到这个魔数后：
不激活早期 fb 文本镜像、不清屏打初始化标题，让加载屏一路留到桌面第一帧覆盖；
否则内核启动早期那几十行 `[PCI64]`/`[APIC]` 日志会直接糊在加载屏上
（写入注释 `hepboot_uefi.c:1925-1929`；消费侧注释 `gfx64.c:1380-1383`）。

**写入侧现状**（`hepboot_uefi.c:1930-1934`）：

- GOP 可用且**非保护模式**时写 `0x484C4459`；
- 保护模式写 0（保护模式内核根本不碰图形，写了会误导）；
- GOP 不可用时 `0x6000` 整块清零（`:1790-1793`），内核据此回退文本 shell。

**读取侧现状（不对称，勿假设对称）**：

- **64 位**：`gfx64.c:1384` 读 `0x6000+16`，等于 `HEPU_HOLD_SCREEN_MAGIC` 时置
  `g_hold_bootscreen = 1`，内核文本只进串口、早期 fb 镜像关闭（`gfx64.c:1384-1388`）。
- **32 位**：32 位内核从 multiboot info 的 framebuffer 字段取帧缓冲
  （`kernel/boot/multiboot.c:311-320`）；全 32 位源码树**没有**读 `0x6010` /
  HLDY 魔数的路径。即「保留引导加载屏」这套约定目前只在 64 位落地，32 位未实现。

## 四、内核侧流程

### 4.1 64 位（`MODE_64` / 保护模式下的 64 位内核）

入口链路：`kernel/arch/x86_64/multiboot_entry.S`（`multiboot_entry`）→
`x86_64_set_boot_info()` → `kernel/kernel_main_64.c` 的主流程。

1. **启动参数解析**：`bootopts64_init()`（`kernel_main_64.c:227`；实现
   `kernel/arch/x86_64/bootopts64.c:68-92`）。识别：
   - `hepu.graphics=0` / `hepu.nographics=1`：强制文本模式（`hepu_nographics=1`，运行时不可反开）；
   - `hepu.dev=1`：开发者模式；
   - `hepu.lock=0`：关闭开机锁屏。
2. **早期 fb 镜像**：`early_fb_text_init_from_bootloader()`（`kernel_main_64.c:377`），
   按 HLDY 魔数决定是否保留引导加载屏（见 3.1）。
3. **初始化阶段**（`[INIT] phase:` 标签，`kernel_main_64.c`）：
   `cores subsystem`（物理内存）→ `HW driver`（早 IDT）→ `security encryption`（CSPRNG）
   → `fs`（ext2）→ `net proto`（net64）→ `system service`（process64）→ `app framework`（desktop64）。
4. **图形子系统**（`kernel_main_64.c:777-789`）：保护模式跳过 `gfx64_init_from_bootloader()`；
   正常模式初始化 VBE/GOP 图形后端并加载 TrueType 字体。
5. **收尾分支**（`kernel_main_64.c:1383-1406`）：
   - 正常模式：显示后端等价性自检后 `desktop64_run()` 进图形桌面；
   - 保护/文本兜底：独立 `usb64_init()` 枚举 USB HID 键盘（桌面路径里的 `usb64_init()`
     在此分支不执行，故需单独枚举，`kernel_main_64.c:1389-1398`）；跳过图形化 OOBE；
     非强制文本模式才走 `oobe64_run()` 文本首设向导；最后 `shell64_run()` 进文本 shell。

### 4.2 32 位（`MODE_32` / 保护模式下退回的 32 位内核）

入口：`kernel/arch/x86/kernel.c` 的 `hepu_kernel_main()`（`:344`）→
`hepu_init_sequence()`（`kernel/arch/x86/init_sequence.c`）。

- 文本/图形开关读 multiboot cmdline：`multiboot_is_nographics()`
  （`kernel/boot/multiboot.c:335-338`），`protect_gfx` gate 住 `gfx2_init()` 等图形
  初始化（`kernel/arch/x86/init_sequence.c:418-421`、`:449`）。
- 32 位帧缓冲信息取自 multiboot info，不经 `0x6000` 块（见 3.1）。

## 五、锁屏与登录流程

### 5.1 开机置锁

`desktop64_run()` 主循环前先清空 PS/2 与 USB 键盘队列（`desktop64.c:230-237`），
再判 OOBE：`!auth64_is_configured() && !hpdesk_skip_oobe()` 时先弹图形首设向导
`hs_oobe_begin()`（`desktop64.c:241-247`）。

桌面壳初始化末尾置锁（`kernel/arch/x86_64/hpdesk.c:7526-7530`）：

- 默认开机 `g_locked = 1`，打印 `[Hepu Desktop] booting to lock screen`；
- 关闭开关 `lock_cfg_disabled()`（`hpdesk.c:4563`，32 位兜底 `:4580`）：cmdline
  `hepu.lock=0`（`bootopts_lock_disabled()`）或 `/system.conf` 里 `lock=0` 任一为真，
  则 `g_locked = 0` 直进桌面；两者都没有才置锁；
- OOBE 未走完时不在这里强制锁——首设向导自己接管画面；
- 32 位兜底锁屏关闭，保持既有旧桌面行为（`hpdesk.c:4580`）。

### 5.2 状态机

`g_locked` 三态（`hpdesk.c:4554`）：

| 值 | 状态 | 画面 |
|---|---|---|
| 0 | 桌面 | 正常桌面 |
| 1 | 锁屏 | 深色半透明底 + 大时钟 + 日期 + 底部提示（`hs_lock_screen_render()` `:4561-4595`） |
| 2 | 登录 | 头像 + 用户名 `hepu` + 密码框 + Unlock 按钮（`hs_login_screen_render()` `:4598-`） |

### 5.3 状态流转（键盘 `hpdesk_handle_key()`；鼠标 `hpdesk_handle_click()`）

```
锁屏(1) ──任意键──▶ 登录(2)            键盘 :7240-7244
锁屏(1) ──点击任意位置──▶ 登录(2)      鼠标 :5664-5668
登录(2) ──ESC──▶ 锁屏(1)              键盘 :7249-7252
登录(2) ──可打印字符──▶ 写入密码缓冲（退格删除）
登录(2) ──回车(\n / \r / 0x1C)──▶ 认证通过→桌面(0)  键盘 :7257-7260；认证失败留登录页
登录(2) ──点 Unlock 按钮──▶ 认证通过→桌面(0)     鼠标 :5778-5782；认证失败留登录页
桌面(0) ──L 键──▶ 锁屏(1)
```

键盘与鼠标共用同一 `g_locked` 状态机，各迁移点打 `[LOCK] state: ...` 日志。
输入合流在 `kernel/arch/x86_64/hsinput.c:206-236`：USB HID 优先、PS/2 兜底，
0xE0 扩展键前缀归一化后再下发 `hpdesk_handle_key()`。

### 5.4 认证现状（如实记录，`9e8f2a3` 三分支）

- **64 位登录提交走 `hs_login_submit()`（`hpdesk.c:4598-4618`）三分支**：
  ① `auth64_is_configured()` 为假（未配 `/account.conf` 账户 hash）→ 放行进桌面；
  ② 已配置 → `auth64_verify_password(g_password)` 做 sha256 比对，通过才解锁；
  ③ 比对失败 → 返回 0，留在登录页并置错误提示，不退回锁屏。
  **2026-11-18 实测（基线 `61c53fa`，`0f4b9d4`）**：已配置账户分支已取证——空口令两次
  被拒（串口 `[LOCK] login: auth failed`，画面红条 "Wrong password"），输入夹具口令后
  `login -> desktop (auth ok)` 解锁进桌面。`hepu123` 仅为本回归夹具口令，**非内置默认口令**；
  仓库可登录口令由 OOBE 首设（auth64 `/account.conf` hash）。鼠标点击解锁路径本轮未单独取证。
- **32 位走 `#else` 直接解锁**：32 位无 `auth64` 且故意不开锁屏，`hs_login_submit()`
  直接返回 1；`login_authenticate` / `login_unlock_screen` / `mu_private_unlock`
  三处为显式未实现桩（`0de2133`），保留「未设密码即放行」并打未实现警告，不返回假装成功；
  真接校验需先给 `mu_user_t` 加 hash 字段与持久化，属独立工程。

## 六、总流程图

```
┌──────────────────────────────────────────────────────────────────────┐
│ OVMF 固件                                                             │
│   └─ ESP /EFI/BOOT/HEPUBOOT.EFI  (tools/hepboot/uefi/hepboot_uefi.c) │
└───────────────────────────────┬──────────────────────────────────────┘
                                │
                 ┌──────────────▼───────────────┐
                 │ load_config() 读 HEPBOOT.CFG  │ hepboot_uefi.c:804,1658
                 │  (必须先于 init_gop)          │
                 └──────────────┬───────────────┘
                                │
                 ┌──────────────▼───────────────┐
                 │ init_gop() 枚举/选模式        │ hepboot_uefi.c:320,1663
                 └──────────────┬───────────────┘
                                │
                 ┌──────────────▼───────────────┐
                 │ show_boot_menu()             │ hepboot_uefi.c:1215
                 │  MODE_64 / MODE_32 / PROTECT │
                 │  失败计数≥3 → 强制 PROTECT     │
                 └───┬───────────────┬──────────┘
            选 64 位 │               │ 选 32 位 / PROTECT(32 退回)
        ┌────────────▼───────┐  ┌────▼─────────────────────────────┐
        │ 目标 KERNEL64.BIN  │  │ 目标 KERNEL32.BIN                 │
        └────────────┬───────┘  └────┬─────────────────────────────┘
                     └───────┬───────┘
                 ┌───────────▼───────────────────────────────────────┐
                 │ 加载屏 ld_begin → 读镜像(0x100000) → CRC32 校验       │
                 │ → 预热页 → 交接准备                                   │
                 │  hepboot_uefi.c:1163,1365,1739,1754,1771            │
                 └───────────┬───────────────────────────────────────────┘
                            │
                 ┌──────────▼───────────────────────────────────────────┐
                 │ 写交接内存: mb_info@0x90000 / fb块@0x6000 / HLDY@0x6010│
                 │ RSDP@0x6020 / 模式表@0x6100 / 内存@0x90100          │
                 │ ExitBootServices → 跳转 0x101000                      │
                 └───────┬───────────────────────────────┬───────────────┘
              !use32     │                               │ use32
        ┌────────────────▼───────────────┐   ┌─────────▼─────────────────┐
        │ 64 位交接 jump_to_kernel_64      │   │ 32 位交接 jump_to_kernel_32│
        └────────────────┬─────────────────┘   └─────────┬─────────────────┘
                         │                                │
        ┌────────────────▼─────────────────┐   ┌─────────▼─────────────────┐
        │ multiboot_entry.S → kernel_main_64│   │ kernel.c hepu_kernel_main │
        │  bootopts64_init() 解析 cmdline    │   │ → init_sequence.c         │
        │  gfx64.c 读 HLDY@0x6010           │   │ multiboot.c 读 mb_info fb  │
        └────────────────┬─────────────────┘   └─────────┬─────────────────┘
                         │                                │
              hepu.graphics=0?                          protect_gfx?
              ┌──────────┴──────────┐               ┌────┴──────────┐
              │ 否(正常图形)          │ 是(保护)      │ 否            │ 是
        ┌─────▼──────┐      ┌───────▼──────┐  ┌────▼─────┐  ┌────▼──────┐
        │ gfx64_init  │      │ 跳过图形初始化 │  │ 图形桌面  │  │ 跳过 gfx2 │
        │ 后端+字体   │      │ 独立 usb64_init│  │          │  │ 进文本 shell│
        └─────┬──────┘      │ 跳过 GUI OOBE │  └────┬─────┘  └───────────┘
              │             └───────┬──────┘       │
        ┌─────▼────────────────────▼──────┐       │
        │ desktop64_run()                  │       │
        │  队列清空 → OOBE 判定 → g_locked=1│      │
        └─────┬───────────────────────────┘       │
              │                                   │
        ┌─────▼─────────────────────────────────────────────▼─────┐
        │ 锁屏(1) ──任意键──▶ 登录(2) ──认证通过──▶ 桌面(0)          │
        │  hpdesk.c:7201-7234 / hsinput.c:206；32 位兜底直进旧桌面 │
        └──────────────────────────────────────────────────────────┘
```

## 七、边界与未验证项

- **legacy BIOS 引导链**：`tools/hepboot/hepboot.S` / `stage2_64.S` 路径不在当前
  交付镜像里；`stage2_64.bin` 没有构建规则产出（见 `TECH_DEBT_ROUTE.md` P3-11）。
  本文流程只覆盖 UEFI/OVMF 路径。
- **32 位 HLDY 保留加载屏**：代码中未实现（见 3.1）；是否需要补齐属待决事项，
  本文不记为已支持。
- **保护模式 32 位内核的端到端键盘/收发**：本文只记录代码路径与开关位置，
  未在本轮对 32 位保护模式做完整 QEMU 实测。
- **32 位锁屏/登录路径未触发**：32 位兜底关闭锁屏、链路直进旧桌面，登录页未被拉起，
  三处 32 位认证桩未执行；这是现状限制，不记为 32 位锁屏/认证已通过（证据
  `evidence-regress-2026-11-17/old-3d0bd23/frames32/ser.log`）。
- **64 位鼠标解锁路径未单独取证**：键盘路径（空口令拒登、夹具口令解锁）已在
  `61c53fa` 回归取证；锁屏点击→登录页→点 Unlock 按钮的鼠标路径未记录坐标/点击事件，
  记 UNVERIFIED。
- 文中出现的内核体积、帧缓冲像素数等会随构建变化的数字，以对应台账当日实测为准；
  本文不固化这类数值。
