/* ============================================================================
 * 赫普OS - 统一用户程序加载 ABI（32 位 / 64 位同一份契约）
 *
 * 背景：历史上 32 位与 64 位各有一套"加载并运行用户程序"的入口：
 *   - 64 位: user_process_exec(path)，无 argv 入参，失败一律 return -1；
 *   - 32 位: sys_execve_impl / binfmt_exec 链路，返回 BINFMT_* 负数口径，
 *            argv 在加载器内部硬编码为 argc=1。
 * 两套返回口径、参数传递、入口名各不相同。本头文件是唯一对外契约，
 * 两侧内核都按这一套实现与打印。
 *
 * 栈布局规范（SysV i386 / x86_64 同一套）：
 *   用户入口处栈顶（32 位 esp / 64 位 rsp）依次为：
 *       [rsp]      argc
 *       [rsp+w]    argv[0], argv[1], ..., argv[argc-1]
 *       [..]       NULL（argv 数组终止）
 *       （其后 envp / auxv 由各架构加载器按既有 ABI 补充，不在本契约内）
 *   w = 4（32 位）/ 8（64 位）。argv[0] 约定为程序名/路径。
 *
 * 返回口径（两侧一致）：
 *   成功：返回新进程 pid（> 0）；
 *   失败：返回下列负错误码之一，不允许多种失败共用同一个 -1。
 * ========================================================================== */

#ifndef HEPU_UPROC_H
#define HEPU_UPROC_H

#include <stdint.h>

/* ---- 错误码（负值；成功 pid 恒 > 0） ---- */
#define HEPU_E_NOENT  (-2)  /* 找不到 / 读不到可执行文件 */
#define HEPU_E_LOAD   (-3)  /* 格式错误 / 段映射 / 加载失败 */
#define HEPU_E_ARG    (-4)  /* 参数非法（空路径、argc/argv 不匹配等） */
#define HEPU_E_NOMEM  (-5)  /* 内存 / 进程槽位不足 */

/* 单条 hepu_exec 允许的最大 argv 个数（内核静态缓冲上限） */
#define HEPU_EXEC_MAX_ARGC  8

/* 加载并运行一个 ELF 用户程序。
 *
 * path  : 用户程序路径（32 位走 initrd/VFS，64 位走 ext2）；不能为 NULL。
 * argc  : argv 元素个数，必须 >= 1；argv[0] 约定为程序名。
 * argv  : 参数字符串指针数组；argc > 0 时不能为 NULL。字符串内存由调用方
 *         保有，内核只在构造初始栈期间读取。
 *
 * 内核在用户栈顶按上述 SysV 布局写入 argc/argv 后再设置入口栈指针；
 * 不改用户程序本体。
 *
 * 返回：> 0 新进程 pid；< 0 HEPU_E_NOENT / HEPU_E_LOAD / HEPU_E_ARG /
 *       HEPU_E_NOMEM。
 */
int hepu_exec(const char *path, int argc, const char *const argv[]);

/* 旁路：加载无 ELF 文件的内置静态程序（入口是内核镜像内的函数地址）。
 * 不塞进 hepu_exec 做路径特殊判断。返回口径同上。
 * name  : 进程名（用于进程表记录）；entry : 用户态入口函数。
 */
int hepu_exec_static(const char *name, void (*entry)(void));

/* 错误码 → 一致文案（两侧 shell 打印同一句话，避免同一错误两边各写各的）。
 * rc > 0 时返回 "ok"。 */
static inline const char *hepu_exec_errstr(int rc)
{
    if (rc > 0) return "ok";
    switch (rc) {
    case HEPU_E_NOENT: return "file not found";
    case HEPU_E_LOAD:  return "load failed";
    case HEPU_E_ARG:   return "invalid argument";
    case HEPU_E_NOMEM: return "out of memory";
    default:           return "unknown error";
    }
}

#endif /* HEPU_UPROC_H */
