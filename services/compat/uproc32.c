/* ============================================================================
 * 赫普OS 32位 - 统一用户程序加载 ABI 实现
 *
 * 复用既有 initrd 解析 + binfmt 加载链（ELF32/PE32/Mach-O），只做：
 *   1. 参数校验与口径适配（BINFMT_* → HEPU_E_* / pid）；
 *   2. 结构化 argv 通道下发到加载器；
 *   3. 两侧一致的 [UPROC] 取证标记。
 * 不重写任何程序加载逻辑；旧 sys_execve_impl / binfmt_exec 薄壳保留。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "uproc.h"
#include "binfmt.h"
#include "initrd.h"
#include "process.h"
#include "vga.h"
#include "serial.h"

#define UPROC_SERIAL 0x3F8

/* 串口十进制有符号数（32 位 vga_puts 不镜像串口，取证走 serial_puts） */
static void uproc_serial_dec(int v)
{
    char buf[12];
    int n = 0;
    if (v < 0) { buf[n++] = '-'; v = -v; }
    char tmp[10];
    int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
    while (t) buf[n++] = tmp[--t];
    buf[n] = 0;
    serial_puts(UPROC_SERIAL, buf);
}

/* 拼一条 "prog arg1 arg2" 整串（PE/Mach-O 加载器仍按整串分词，沿用旧路径） */
static void uproc_join_cmdline(const char *const argv[], int argc, char *out, int outsz)
{
    int c = 0;
    for (int i = 0; i < argc && c < outsz - 2; i++) {
        if (i) out[c++] = ' ';
        const char *s = argv[i];
        while (*s && c < outsz - 1) out[c++] = *s++;
    }
    out[c] = 0;
}

int hepu_exec(const char *path, int argc, const char *const argv[])
{
    /* ---- 参数校验 ---- */
    if (!path || !path[0]) return HEPU_E_ARG;
    if (argc < 1) return HEPU_E_ARG;
    if (argc > HEPU_EXEC_MAX_ARGC) return HEPU_E_ARG;
    if (!argv) return HEPU_E_ARG;
    for (int i = 0; i < argc; i++) {
        if (!argv[i]) return HEPU_E_ARG;
    }

    if (!initrd_ready()) {
        serial_puts(UPROC_SERIAL, "[UPROC] exec path=");
        serial_puts(UPROC_SERIAL, path);
        serial_puts(UPROC_SERIAL, " rc=");
        uproc_serial_dec(HEPU_E_NOENT);
        serial_puts(UPROC_SERIAL, "\n");
        return HEPU_E_NOENT;
    }

    /* ---- 文件名解析: 剥路径取 basename → 原名 → .elf 兜底（沿用 shell run 既有规则） ---- */
    const char *base = path;
    for (const char *p = path; *p; p++) if (*p == '/') base = p + 1;
    char full[80];
    int k = 0;
    while (base[k] && k < 70) { full[k] = base[k]; k++; }
    full[k] = 0;

    int fd = initrd_open(full);
    if (fd < 0 && k <= 71) {
        strcpy(full + k, ".elf");
        full[k + 4] = 0;
        fd = initrd_open(full);
    }

    int size = (fd >= 0) ? initrd_size(fd) : 0;
    int idx = initrd_find(full);
    if (fd >= 0) initrd_close(fd);

    if (idx < 0 || size <= 0) {
        serial_puts(UPROC_SERIAL, "[UPROC] exec path=");
        serial_puts(UPROC_SERIAL, path);
        serial_puts(UPROC_SERIAL, " rc=");
        uproc_serial_dec(HEPU_E_NOENT);
        serial_puts(UPROC_SERIAL, "\n");
        return HEPU_E_NOENT;
    }

    uint8_t *data = initrd_get_data(idx);
    if (!data) {
        serial_puts(UPROC_SERIAL, "[UPROC] exec path=");
        serial_puts(UPROC_SERIAL, path);
        serial_puts(UPROC_SERIAL, " rc=");
        uproc_serial_dec(HEPU_E_NOENT);
        serial_puts(UPROC_SERIAL, "\n");
        return HEPU_E_NOENT;
    }

    /* PE/Mach-O 走整串 cmdline；ELF32 走结构化 argv 通道（同一套 argc/argv） */
    char cmdline[128];
    uproc_join_cmdline(argv, argc, cmdline, sizeof(cmdline));
    binfmt_set_cmdline(cmdline);
    binfmt_set_argv(argc, argv);

    int rc = binfmt_exec(full, data, (uint32_t)size);

    int out;
    switch (rc) {
    case BINFMT_OK: {
        int pid = binfmt_last_pid();
        out = (pid > 0) ? pid : HEPU_E_LOAD;
        break;
    }
    case BINFMT_ERR_MEM:  out = HEPU_E_NOMEM; break;
    case BINFMT_ERR_IO:   out = HEPU_E_NOENT; break;
    case BINFMT_ERR_FORMAT:
    case BINFMT_ERR_UNSUP:
    default:               out = HEPU_E_LOAD; break;
    }

    serial_puts(UPROC_SERIAL, "[UPROC] exec path=");
    serial_puts(UPROC_SERIAL, path);
    serial_puts(UPROC_SERIAL, " rc=");
    uproc_serial_dec(out);
    serial_puts(UPROC_SERIAL, "\n");
    return out;
}

int hepu_exec_static(const char *name, void (*entry)(void))
{
    if (!name || !name[0] || !entry) return HEPU_E_ARG;

    extern process_t *process_create(const char *, uint32_t,
                                      privilege_t, uint32_t);
    process_t *proc = process_create(name, (uint32_t)(uintptr_t)entry,
                                     PRIV_USER, 1 /* NORM */);
    if (!proc) {
        serial_puts(UPROC_SERIAL, "[UPROC] exec-static name=");
        serial_puts(UPROC_SERIAL, name);
        serial_puts(UPROC_SERIAL, " rc=");
        uproc_serial_dec(HEPU_E_NOMEM);
        serial_puts(UPROC_SERIAL, "\n");
        return HEPU_E_NOMEM;
    }

    serial_puts(UPROC_SERIAL, "[UPROC] exec-static name=");
    serial_puts(UPROC_SERIAL, name);
    serial_puts(UPROC_SERIAL, " rc=");
    uproc_serial_dec(proc->pid);
    serial_puts(UPROC_SERIAL, "\n");
    return proc->pid;
}
