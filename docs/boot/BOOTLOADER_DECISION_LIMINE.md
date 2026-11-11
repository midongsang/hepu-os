---
title: BOOTLOADER_DECISION_LIMINE
status: active
sys_version: 0.57.0
updated: 2026-11-12
owner: boot
source_of_truth: null
supersedes: null
---

# 引导器取舍意见书：保留 HepuBoot，还是改用 Limine

> 立场声明：本文不是"Limine 好不好"的评测，而是"**赫普OS 该不该换引导器**"的决策意见。
> 所有结论都挂在本仓库可复核的文件与行号上；实测项标注了执行方式。

## 一、结论

**不建议用 Limine 替换自研引导。** 建议：

1. **保留 HepuBoot 作为默认引导链**；
2. **优先把已有的 UEFI 线接进 64 位主线**（这是被搁置的资产，不是新工程）；
3. **Limine 只作为可选的第二引导器**，用于"引导/内核故障隔离"与"真机兜底"，不进入默认发布镜像；
4. BIOS 线的 VBE 高分辨率问题**不再投入修复**，高分辨率职责交给 UEFI 线的 GOP。

一句话：你已经有两套自研引导器，其中 UEFI 那套今天实测还能编出可用产物；再引入第三套，收益是"省心"，代价是丢掉已有能力并把启动路径交给外部协议。

## 二、判断所依据的事实

### 2.1 项目规模与引导链现状

| 维度 | 实测值 | 复核方式 |
|------|--------|----------|
| 内核总代码 | 393,235 行 | `find kernel -name '*.c' -o -name '*.h' -o -name '*.S' \| xargs wc -l` |
| 64 位内核 | 68,617 行 | `find kernel/arch/x86_64 -name '*.c' \| xargs wc -l` |
| 32 位内核 | 45,564 行 | `find kernel/arch/x86 -name '*.c' \| xargs wc -l` |
| 引导器汇编 | 4,484 行 | `find tools/hepboot -name '*.S' \| xargs wc -l` |
| UEFI 引导器 C | 1,467 + 14KB GOP | `tools/hepboot/uefi/hepboot_uefi.c`、`hepboot_gop.c` |
| 文档 | 108 篇 | `find docs -name '*.md' \| wc -l` |

**引导链是双线的，不是单线**：

- **BIOS 线**：`hepboot_mbr.S`（MBR stage1 @0x7C00）→ `hepboot.S`/`x86_64/stage2_64.S`（stage2 @0x8000）→ 32 位内核（`KERNEL32_LBA=4400`，multiboot 入口）→ 切长模式 → 64 位内核（`KERNEL64_LBA=2000`，32 块 = 1MB 加载区）。
- **UEFI 线**：`tools/hepboot/uefi/hepboot_uefi.c`（gnu-efi，PE32+）→ ESP 读 `\hepu-kernel.bin` → `AllocateAddress` 到 0x100000 → `ExitBootServices` → x64→32 位切换 → Multiboot info → 跳内核。

### 2.2 BIOS 线有两个结构性硬伤，且都不是"再调调"能过的

| 编号 | 问题 | 证据 | 影响 |
|------|------|------|------|
| BUG-17 | VBE 高分辨率设置后内核起不来，根因是 SeaBIOS 的 VBE 实现内部切保护模式、切回实模式时破坏底层状态 | `docs/boot/HEPBOOT_DESIGN.md` L290-299，二分定位到 `INT 10H AX=4F02` | BIOS 线只能停在 VGA 320x200；桌面高分辨率实际由内核/KMS 路径兜底 |
| BUG-19 | 16 位实模式代码布局高度敏感，bootloader 侧连 `call` 指令都被禁用 | `HEPBOOT_DESIGN.md` L200、L250 | 引导失败计数只能在内核侧（读 LBA10）实现，引导器侧无法加逻辑 |

这两个问题的共同性质：**根因不在自己的代码里**（一个在固件实现，一个在 16 位地址空间的物理约束）。这也正是照抄 GRUB 也未必能救回来的地方——GRUB 在现代机器上同样要绕 SeaBIOS。

### 2.3 UEFI 线是"活的资产"，今天实测可构建

```bash
make -C tools/gnu-efi            # 先出 crt0 + libgnuefi/libefi
make -C tools/hepboot/uefi       # 出 hepboot_uefi.efi
# → 75,835 bytes, PE32+ executable (EFI application) x86-64
```

`docs/boot/HEPBOOT_DESIGN.md` L252-270 的 P1 清单**全部打勾**：OVMF 跑通、GPT+ESP、读内核（0x13B9A6 字节、魔数 02B0AD1B 校验）、加载到 0x100000、ExitBootServices、x64→32 切换、Multiboot info、hepboot.cfg、UEFI 引导菜单、NV 变量失败计数、GOP 1024x768 32bpp。

也就是说：**"高分辨率 + GPT + 配置文件 + 失败计数"这套东西，自研 UEFI 线已经做完了**，而 Limine 能提供的核心价值恰好就是这些。

### 2.4 但 UEFI 线目前没接进 64 位主线（这才是真问题）

- `hepboot_uefi.c:550` 写死 `KERNEL_PATH L"\\hepu-kernel.bin"`（32 位壳），不是 64 位产物；
- `tools/build_uefi_dual.py` 的 `PROJECT` 常量指向**另一个会话的绝对路径**（`.../38448056008342018/extracted/os-source/...`），当前环境跑不通；
- `tools/hepboot/uefi/build_uefi_image.sh` 依赖 `build_v250/hepu-kernel.bin`，该目录在当前仓库不存在。

结论：**UEFI 线是"能编译、能启动、但没接线"的状态**。它需要的是一条接线和几处路径修正，而不是重做。

### 2.5 Limine 不是即插即用 —— 内核当前没有 multiboot 头

| 事实 | 证据 |
|------|------|
| 64 位内核产物是 flat binary（`file` → `data`），由 stage2 按固定物理地址直接铺入 1MB 区 | `build_x86_64/hepu-kernel64.bin`（780,544 B） |
| 内核里有 multiboot **入口函数**，但**没有 multiboot header**（`grep -c 0x1BADB002 multiboot_entry.S` → 0） | `kernel/arch/x86_64/multiboot_entry.S` |
| MB2 魔数只有定义、没有使用：入口只把 EAX 存进 ESI、EBX 存进 EDI，不区分协议 | `multiboot_entry.S` L10-11 定义、L25-26 取参 |

后果：**Limine/GRUB 现在无法自动识别并加载这个内核**。要以 Limine 为主引导，至少要做四件事（见第四节），其中"multiboot2 信息表与现有 MB1 解析并存"和"Limine 进入时的页表/HHDM 假设与自建恒等映射"是真正的返工点。

## 三、三个方案对比

| 维度 | A. 保留自研（推荐） | B. 换 Limine | C. 并存（A + Limine 可选） |
|------|-------------------|-------------|--------------------------|
| 与项目定位一致 | 高。"HepuBoot 自恢复/三模式降级/配置"是产品特性，不是技术债 | 低。引导器变成外部组件，"全自研"叙事断裂 | 中。默认仍是自研，Limine 只在调试分支 |
| 已有资产 | 全部保留（BIOS + UEFI 两条线、4484 行汇编、1467 行 EFI C） | 全部作废，重写菜单/配置/失败计数 | 保留，另加一份配置 |
| 真机兼容 | 中（SATA/NVMe/EHCI 内核驱动未补齐，瓶颈在内核不在引导器） | 略高（Limine 的磁盘栈成熟） | 高（可切 Limine 绕过国产盘/RAID 的怪 BIOS） |
| 故障隔离能力 | 低（引导器黑屏时无法判断是引导还是内核） | 高（换一个引导器就能二分） | **高**（这是 Limine 最值钱的用途） |
| 工作量 | 接线 + 3 处路径修正（约 1 天） | 内核适配 1-2 天 + 引导特性重建 3-5 天 + 长期跟随上游 | A + 半天（Limine 只做调试镜像） |
| 长期风险 | 自己维护，问题自己背 | 上游 API/协议变动、安全通告要跟 | 两套配置漂移 |
| 体积影响 | 无（引导器不在 1MB 内核限额内） | 无 | 无 |

## 四、如果哪天决定上 Limine：代价清单（按实测）

1. **加 multiboot2 header**：32 字节、必须在 ELF 前 32KB 内，声明入口与请求项（`multiboot_entry.S` 当前没有）。
2. **入口分叉协议**：MB2 的 `EAX=0x36D76289` 与 MB1 的 `0x2BADB002` 必须分支，且 EBX 指向**完全不同的信息结构**（MB2 是 8 字节对齐的 tag 链）。
3. **info 解析分叉**：`mb_info` 解析层要同时吃 MB1/MB2，否则内存映射、framebuffer 字段全错——这类错误表现为"内核起来了但内存只有 640KB"，非常隐蔽。
4. **页表/HHDM 假设**：Limine 会以它自己的页表和 HHDM 进入；而本内核大量依赖**恒等映射**（`multiboot_entry.S` 建 0-16MB 2MB 大页、`pmm64` 分配页后核段恒等映射）。不统一这两套假设，就会出现"某个物理地址在引导器眼里是 0xFFFF8000...，在内核眼里是 0x... "的幽灵 bug。
5. **引导特性重建**：F8 菜单、三模式降级、失败计数、hepboot.cfg、安全/恢复模式这些语义在 Limine 下要么用它的 cfg 近似表达，要么在内核侧重做。

## 五、不依赖本次决策、现在就该做的三件事

1. **接线 UEFI 线**：修 `build_uefi_dual.py` 的写死路径、让 `hepboot_uefi.c` 的 `KERNEL_PATH` 指向 64 位产物、在 OVMF 下跑一次"EFI → 64 位内核 → 桌面"的端到端。这一步做完，项目就有了一条**不依赖 16 位实模式**的引导链，BUG-17/19 自动绕开。
2. **BIOS 线降级为兼容路径**：不再修 VBE；高分辨率只走 UEFI-GOP 和内核 KMS。文档里明确"BIOS 线最高 VGA 320x200"。
3. **真机瓶颈在内核，不在引导器**：`docs/guides/REAL_HARDWARE_TEST.md` 的缺口清单（SATA HW-03、NVMe HW-04、EHCI HW-06）全部是**内核驱动**问题。换 Limine 不能解决这些，能解决的是"引导器读不到盘"这一类。

## 六、什么情况下我会改口支持"换 Limine"

出现下列任一情况，A 方案的成本会反超 B 方案，届时应当重新评估：

- 需要**链式引导 Windows/Linux**（Limine 原生支持，自研要重做一整套 chainload）；
- 需要**PXE/iPXE 网络引导**做规模化部署（Limine 有现成方案）；
- 真机上出现**多款固件只认标准 EFI 引导项**的兼容问题，自研 EFI 修不动；
- 引导器代码开始**阻碍内核演进**（例如每次改加载布局都要重编 16 位汇编）。

反之，只要项目目标仍是"自研操作系统、引导链是自己的一部分"，Limine 就应当停在"调试工具"的位置上，而不是主角。

## 七、给 Limine 的正确位置（如果采纳 C）

- 产物：`/tmp` 或 `dist/debug/` 下的**调试镜像**，不进默认发布；
- 用途一：真机黑屏时二分——同一内核用 Limine 启动，能进桌面即证明是引导器问题；
- 用途二：内核回归基准——CI 里用 Limine 跑一遍，排除引导器变量；
- 前提：仍要先完成第四节的 1-4 项（header/协议分叉/info 分叉/页表假设），所以**这条路线的前提工作量与"以它为主"是一样的**，这也是不建议把它当主线的原因之一。

## 附：本次意见所读材料

- `docs/boot/HEPBOOT_DESIGN.md`（全文 299 行）
- `docs/boot/BOOTLOADER_DEFINITION_PLAN.md`（宏/地址/命名规范，222 行）
- `docs/boot/UEFI_BOOT_DESIGN.md`（851 行，P1 落地细节）
- `docs/architecture/PHILOSOPHY.md`、`ANTI_BURDEN.md`（"只依赖契约、允许废弃但不许偷加补丁"）
- `docs/guides/REAL_HARDWARE_TEST.md`（真机缺口清单）
- 源码：`tools/hepboot/*.S`、`tools/hepboot/uefi/*`、`kernel/arch/x86_64/multiboot_entry.S`、`kernel/arch/x86_64/Makefile`、`tools/build_uefi_dual.py`、`tools/make_single_img64.sh`
- 实测：`make -C tools/gnu-efi && make -C tools/hepboot/uefi` → 75,835 B PE32+ EFI 应用
