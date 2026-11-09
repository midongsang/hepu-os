---
title: 64位内核安全审计（第二轮）
status: active
date: 2026-11-10
baseline: main（v0.54.6 显示版本）
scope: 网络栈 / 系统调用 / 密码学 / 内存管理 / ext2 / 构建链
method: 源码审计 + 主机端 ASan/UBSan 测试 + 最小复现 + QEMU 端到端验证
related: AUDIT_64BIT.md, BUG_FIX_GUIDELINES.md, OPEN_BUGS.md
---

# 64 位内核安全审计报告（第二轮）

本轮在 `AUDIT_64BIT.md` 之后，以当前源码为唯一依据重新独立审计，共定位并修复
**7 个安全/健壮性缺陷 + 1 个构建链缺口**，全部遵循 `BUG_FIX_GUIDELINES.md`
（证据优先、最小修改、验证闭环、可回退）。

## 结论汇总

| 编号 | 模块 | 缺陷 | 严重度 | 验证方式 |
|------|------|------|--------|----------|
| R2-1 | net64_dns | 压缩指针无循环保护，远程 DoS 挂死 | 高 | 主机复现 timeout 124 → 修复后返回 -1 |
| R2-2 | csprng / getrandom | 种子被 reseed 抹成固定值；getrandom 用假随机 | 高 | RFC7539 向量比对 + 两次初始化种子不同 |
| R2-3 | pmm64 | MAX_MEMORY_SIZE 整数溢出，钳制失效 | 中 | 编译警告消除 + 2GB/2048MB 校验 |
| R2-4 | syscall getdents | 直写用户指针，未走 copy_to_user | 中 | 代码重构 + 编译通过 |
| R2-5 | net64_dhcp | 内联选项遍历缺边界检查（越界读） | 低 | 三处补边界 + 回归通过 |
| R2-6 | net64_tcp | data_offset 缺合法性校验 | 低 | 加校验 + 回归通过 |
| R2-7 | ext2_algo | 间接块与目标数据同址复用（SEC-11） | 低 | 独立缓冲 + 主机 5 套件通过 |
| R2-8 | 构建链 | MBR 无自动构建，缺失时静默产出坏镜像 | 中 | 脚本自动构建 + 失败即中止 |

---

## R2-1：DNS 压缩指针无限循环（DoS）

**现象**：`dns_parse_name` 跟随压缩指针（高两位置位）时，无跳转次数限制，也未
校验指针方向。攻击者控制的 DNS 响应可在某偏移放置自指指针（如偏移 12 处放
`0xC0 0x0C`），解析时 `pos` 不断被重置为同一位置，形成无限循环，内核网络路径
挂死。

**证据**：主机端 harness（`#include "net64_dns.c"` + extern stub），构造自指
响应，`timeout 5` 运行退出码 124（超时挂死）。

**修复**：
- 压缩指针必须指向包内且严格前移（`ptr < packet_len && ptr < pos`），保证跳转
  单调、必然终止（RFC 1035 规定指针只指向先前出现的名字）。
- 跳转计数上限 64 做深度防御；畸形时返回 -1。
- `dns64_resolve` 两处调用检查 -1，丢弃畸形响应。

**回归**：自指/互指/前向指针均返回 -1；普通名称与合法压缩指针解析正确
（`www.example.com`，new_offset 正确）。

## R2-2：CSPRNG 种子被抹除 + getrandom 假随机

**现象 A（种子抹除）**：`csprng_init` 末尾调用 `csprng_reseed(entropy, 64)`，
而 reseed 内部把 `seed ^= additional_data`——两者是同一份 entropy，异或后
`seed = 0`，随后 `ChaCha20(全0 key/counter/nonce)` 输出一个**固定块**，把多源
熵彻底抹掉，导致每次启动种子相同。

**证据**：修复前 seed 为 `76b8e0ada0f13d90...`，正是 RFC 7539 / ChaCha20 在
全零 key、nonce、counter 下标准输出块的前 32 字节。

**现象 B（getrandom 假随机）**：`sys_getrandom` 返回确定性序列
`b = i*17 + 3`，与系统内已编译的 ChaCha20 CSPRNG 完全脱节，无法用于密钥、
nonce、栈金丝雀等安全用途。

**修复**：
- 删除 `csprng_init` 中自毁式 reseed；seed/state 直接由 64 字节真实熵初始化。
- 启动序列在网络栈前显式（重新）初始化 CSPRNG。
- `sys_getrandom` 改用 `csprng_get_bytes`，内核缓冲后 `copy_to_user`，保留用户
  窗口校验与长度上限。

**回归**：连续两块输出不同；256 字节覆盖 158 种取值；两次独立初始化种子不同。

## R2-3：pmm64 整数溢出

**现象**：`MAX_MEMORY_SIZE (524288 * 4096) = 2^31`，按 int 字面量计算溢出为
`-2147483648`。
- 与 `uint64_t` 比较时转为巨大值，内存上限钳制永不触发，超过 2GB 时固定
  64KB 位图存在越界风险。
- `pmm64_get_max_memory_mb` 经 int 运算返回错误的巨大值。

**修复**：强制 `uint64_t` 运算（`(uint64_t)MAX_MEMORY_PAGES * PAGE_SIZE`）。

**回归**：编译 overflow 警告消除；`MAX_MEMORY_SIZE=2147483648`、max MB=2048；
3GB 输入被正确钳制到 2GB。

## R2-4：getdents 直写用户指针

**现象**：`sys_getdents` 直接在用户地址 `dirp` 上构造 `linux_dirent64`，既未
校验用户窗口也未走 `copy_to_user`，SMAP 下内核态访问会 #PF（与此前修复的
sys_pipe 直写同类）。

**修复**：增加用户窗口校验；每个条目先在内核栈 `kentry` 构造，再 `copy_to_user`
到 `dirp + total`；非法用户地址返回错误；移除未使用的 `root_size`。

## R2-5：DHCP 选项遍历缺边界检查

**现象**：`dhcp_handle_offer` 与 `dhcp64_poll` 中三处内联选项遍历直接访问
`opts[i+1]/opts[i+2]`，当 i 接近 308 时越界读 options 之外内存，与已有完整
边界检查的 `dhcp_parse_options` 不一致。

**修复**：每处增加 `tag+len` 可读检查及具体选项值的越界检查，畸形选项安全终止。

## R2-6：TCP 数据偏移缺校验

**现象**：`tcp64_handle` 对 `data_offset` 字段不校验，畸形报文可得到负
data_len 或把 TCP 头部当载荷，并据此错误确认序列号。

**修复**：校验数据偏移在 20..60 且不超过报文长度，非法报文直接丢弃。

## R2-7：ext2 间接块同址复用（对应 SEC-11）

**现象**：`read_data_block` 用 `ctx->block_buf` 兼作间接指针缓冲与目标数据
缓冲，而调用方传入的 buffer 常为同一 `ctx->block_buf`，二者同址，仅靠写入顺序
自洽，是脆弱设计，任何重构都可能破坏数据。

**修复**：函数内使用独立 `ind_buf[4096]` 解析各级间接指针，与目标数据缓冲
分离。block_size 经 `validate_sb` 校验 ≤4096，64KB 内核栈足够。

**回归**：主机端 5 套件（uaccess/aspace/heap/elf/ext2）全部通过。

## R2-8：构建链 MBR 缺口

**现象**：`make_single_img64.sh` 依赖 `tools/hepboot/hepboot_mbr.bin`，但没有
任何 Makefile/CI 目标构建它，缺失时 `dd` 静默失败，产出镜像前 446 字节无 MBR
引导代码、无法从 MBR 启动。

**修复**：
- 打包前若 MBR 缺失则自动构建（`gcc -m32 → ld 0x7C00 → objcopy`）。
- MBR 构建与写入失败明确报错中止，不再静默产出坏镜像；`dd` 不再吞错误。

---

## 验证总览

1. **主机端测试**：5 套件（含 ASan/UBSan）全部通过，0 失败。
2. **内核编译**：无 error/undefined；本轮相关 overflow 警告消除。
3. **QEMU 端到端**：新镜像成功引导至桌面（OOBE 正常、中文渲染正常、4 核/2GB
   信息正确），串口日志确认 CSPRNG 初始化，无 panic/double fault/exception。
4. **回退保障**：每个修复独立提交，可单独 revert；未引入新的全局状态依赖。

## 备注

- 显示版本字符串（`Hepu OS v0.54.6`）在多处硬编码，与 `VERSIONING.md`
  （sys_version 0.54.8）已存在不一致；本轮未擅自变更版本号，是否 bump 由
  项目负责人决定。
- QEMU 默认 CPU 模型未暴露 RDRAND（日志 RDRAND=0），CSPRNG 走 RDTSC/MEM/
  STACK 熵；真机启用 RDRAND 时会自动使用硬件熵。
