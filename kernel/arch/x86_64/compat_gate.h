/* ============================================================================
 * HepuOS 64 位 - 兼容层网关（compat_gate）
 *
 * 背景（DESIGN.md 约束一）：多平台兼容层必须作为用户态服务实现，禁止在内核态
 * 做外部 ABI 的解析/翻译。当前 64 位内核里还留着的兼容逻辑是：
 *   ① syscall64_full.c 里的 linux_map[350]（Linux 号 → Hepu 原生号）
 *   ② syscall 路径上 5 处 env_type 分支
 *   ③ elf64_is_linux_abi() 的 ELF 判定
 *   ④ user_process.c 的 Linux 适配分支
 * 这些属于已记录的架构债务（DESIGN.md 约束一下的"落地现状"注记）。
 *
 * 本模块是化解债务的**第一步：把"要不要翻译、翻译成几号"收成一个网关入口**。
 * 内核此后只调 compat_gate_*，不再直接碰翻译表 —— 目标形态是把这张表连同
 * 判定整段搬到用户态 compat 服务，内核侧改成一次 IPC 查询；调用点不需要再动。
 * 这一步本身零行为变化：表还是同一张，判定还是 process64_current_env_type()。
 *
 * 参照 KERNEL.md 3.2「渐进替换流程」：新实现独立文件、同一套接口、可并行验证。
 * ========================================================================== */

#ifndef HEPU_COMPAT_GATE_H
#define HEPU_COMPAT_GATE_H

/* 当前进程是否处于 Linux 兼容域（ENV_LINUX_COMPAT）。
 * 目标形态：由用户态 compat 实例在 exec 时登记，内核只读一个 per-process 标记。 */
int compat_gate_active(void);

/* 把 Linux x86_64 系统调用号翻成 Hepu 原生号。
 * 返回 0 表示"不需要/不支持翻译"，调用方保留原号（与原实现语义一致）。 */
int compat_gate_translate_nr(int nr);

/* 判定一个 ELF 该走哪套 ABI：返回 1 = Linux 兼容域，0 = Hepu 原生。
 * 从 elf64.c 的 elf64_is_linux_abi() 搬来 —— "这是不是外来 ABI" 属于兼容层
 * 的判断，不属于 ELF 解析模块的判断。elf64.c 里保留同名薄封装供既有调用方使用，
 * 判定逻辑只有这一份。 */
int compat_gate_detect_elf(void *elf_data, uint64_t size);

/* 翻译表规模（非零项数），供自检与文档统计使用。 */
int compat_gate_map_entries(void);

/* 启动自检：验证几个关键映射与"未映射返回 0"。
 * 返回 0 = 通过，-1 = 有映射错。串口打印一行结论。 */
int compat_gate_selftest(void);

#endif /* HEPU_COMPAT_GATE_H */
