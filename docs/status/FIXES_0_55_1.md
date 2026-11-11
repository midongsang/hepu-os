---
title: 编译告警清理与真实缺陷修复记录（0.55.1）
status: active
sys_version: 0.56.2
updated: 2026-11-11
owner: status
source_of_truth: kernel/arch/x86_64/
supersedes: null
---

# 编译告警清理与真实缺陷修复记录（0.55.1）

## 背景

全量重编（`rm -rf build_x86_64 && make -C kernel/arch/x86_64`）长期存在 **209 条告警**。
构建使用 `-Wall -Wextra`，**未开 `-Werror`**，所以历史告警从未阻断发布，逐次累积。

本次不是"消噪音"，而是按"是否指向真实系统缺陷"逐条判断：**告警只是线索，重点是它暴露的逻辑问题**。

## 结果

| 指标 | 清理前 | 清理后 |
|---|---|---|
| 全量重编告警总数 | 209 | **89** |
| 非 `-Wunused-parameter` 告警 | 113 | **0** |
| 内核体积 | 621108 B | 629684 B（配置加载与校验逻辑增加） |
| 编译错误 | 0 | 0 |

剩余 89 条全部是 `-Wunused-parameter`（`argc`/`argv` 之类的接口签名参数），属噪音，不逐条处理。

## 修复的真实缺陷（按严重度）

1. **系统调用号撞号导致 mkfs 被静默丢弃**（`syscall64_full.c`）
   `SYSCALL_UMASK` 原值 226，与 `SYS_MKFS` 的 226 相同。在 `syscall_table` 的 designated
   initializer 中后者覆盖前者 —— 调用 `mkfs` 实际执行的是 `umask`。这是 GCC
   `-Woverride-init` 告警真正指向的问题。已将 `SYSCALL_UMASK` 改到空闲号 209。

2. **桌面配置从未被读取**（`hpdesk.c`）
   `hpdesk_load_config()` 是空桩（注释直写"完全跳过配置加载"），`load_theme_from_config` /
   `load_ui_from_config` / `load_panels_from_config` / `load_panel_from_config` /
   `hs_read_config_file` 五个函数全仓库**零调用**。结果是：图形界面写回
   `/etc/hepushell/config.hsqml` 的配色，重启后完全不生效。
   现已真正解析并应用 `Theme` / `UI` / `Panels` 三段；配置文件缺失时回退到内置
   `default_config`（该常量此前也从未被使用）。
   验证：串口输出 `[Hepu Desktop] config: 3 sections, background=0xf0ebe0`，与配置文件一致。

3. **`hsqml_parse` 返回值语义与常规约定相反**
   成功时返回**根节点数**（正数），失败返回 -1 或置 `error`。最初的配置接入代码按
   "0=成功" 判断，导致解析成功也被判为失败。已按真实语义修正，并在调用处加注释。

4. **`DT_RELA` 重定位表从未参与应用**（`user_process.c`）
   `rela2_off` 算了 `.rela.dyn` 的文件偏移却从未加入重定位表列表，只在 trace 打印。
   若 IRELATIVE 只出现在 `DT_RELA` 而不在 section header 列表里，这些 ifunc 解析器会被
   完全跳过。已按与 `DT_JMPREL` 相同的去重规则补入。

5. **ELF 重定位不校验类型**（`elf64.c`）
   原实现把任何重定位类型都按 64 位绝对地址写入，遇到 `JUMP_SLOT`/`COPY`/`GLOB_DAT`
   会写坏目标位置。现在只处理 `R_X86_64_RELATIVE` 与 `R_X86_64_64`，其余跳过。

6. **TTF cmap 子表只筛 platform 不筛 encoding**（`ttf64.c`）
   `platform == 3` 时未检查 encoding，可能命中 Symbol(0) 子表取到错误字形。
   现在 platform 3 只接受 encoding 1/10。

7. **LAPIC 基址 int 字面量转指针**（`apic64.c`）
   `APIC_BASE_ADDR` 是 `0xF0000000`（int 类型，作为有符号是负数），直接
   `(volatile uint32_t*)(APIC_BASE_ADDR + reg)` 的转换行为依赖编译器实现，可能符号扩展。
   已显式经 `uintptr_t` 零扩展。ACPI 的 EBDA/BIOS 地址同样处理。

8. **恒真/恒假判断导致逻辑失效**
   - `ext2_fsck64.c`：`i_links_count > 65535` 对 `uint16_t` 恒假 → 改为 `== 0`（孤立 inode 判据）。
   - `user_process.c`：`uint64_t` 变量做 `< 0` 恒假，页偏移修正死代码（实际由 `copy_start/copy_end` 覆盖）。
   - `ext2_64.c` / `gfx64.c` / `gfx_algo.c`：`uint8_t`/`char` 的越界判断恒真或恒假，删除无效项。

9. **RTL8139 CBR 读取未校验**（`rtl8139_64.c`）
   `CBR`（当前接收缓冲指针）读出来未使用，收包仍只依赖 `rtl_rx_offset` 自增回绕。
   已保留读取并显式标注为遗留缺口（未改变收包行为）——属于记录而非修复。

10. **4 处函数"先调用后定义"的隐式声明**
    `gfx64_blit_background` / `gfx64_blend_pixel` / `window64_set_focus` / `vga_put_hex`：
    调用点在前、定义在后且无原型，编译器按隐式声明传参。已在文件顶部提前原型。

## 非缺陷类清理（不改变行为）

- 缩进误导（同一行多条语句）5 处拆分。
- 死变量/死代码删除：约 20 处局部变量、`e1000_wait_tx`（与 `e1000_send` 内联等待重复）、
  多个自造 `str_cpy`/`str_ncpy`/`strcmp`/`memcpy` 重复实现、16KB 未引用静态 `user_stack`。
- 预留函数（`sys_ppoll`、`eth64_find`、`ac97_read_bm_word`、`apic_map_high_address`、
  `gdt_set_entry`）显式标注 `__attribute__((unused))`，避免与真死代码混淆。
- 删除两个硬编码假输出的 `id` / `who` 命令（输出为静态字符串，未接账户系统）。
- 显式化 `int`/`uint32_t` 比较约 20 处（渲染裁剪、TLS、APIC 循环），语义等价。

## 静态未调用函数的行为提醒

`check_inode` 已实现但 fsck 主流程只校验超级块/位图，**尚未遍历 inode 表**，
因此该项检查（含本次修正的 `i_links_count == 0`）暂无实际效果。已在代码注释标注。

## 验证

- 全量重编：0 error，89 warning（全为 unused-parameter）。
- QEMU（`-smp 1`）：正常启动，APIC/IOAPIC 初始化、配置加载、桌面渲染、灵动岛动画均正常；
  截图见交付。
- 主机回归：`bash tools/host-tests/run_host_tests.sh`（含 PNG 用例）保持通过。
- 文档一致性：`python3 tools/check_docs.py` 0 错误 0 警告。

## 遗留（未在本版本处理）

- `-Wunused-parameter` 89 条（接口签名噪音）。
- fsck 未遍历 inode 表（`check_inode` 未接入）。
- `id` / `who` 命令未实现（需要接账户与登录会话）。
- `ppoll` 已实现但未挂入 syscall 表。
- `-smp 4` 启动卡在 AP trampoline（既有缺陷，与本次改动无关）。
