/* ============================================================================
 * 赫普OS 64位 - 完整系统调用实现
 * 支持：write, read, open, close, exit, fork, execve, getpid, brk
 * V0.10.17: 接入copy_from_user/copy_to_user，修复用户指针安全漏洞
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 用户空间访问安全 */
extern int copy_from_user(void *kernel_dst, const void *user_src, size_t size);
extern int copy_to_user(void *user_dst, const void *kernel_src, size_t size);

#define SYSCALL_WRITE   1
#define SYSCALL_READ    2
#define SYSCALL_OPEN    3
#define SYSCALL_CLOSE   4
#define SYSCALL_EXIT    6
#define SYSCALL_FORK    7
#define SYSCALL_EXECVE  11
#define SYSCALL_GETPID  20
#define SYSCALL_BRK     12
#define SYSCALL_TIME    13
#define SYSCALL_SLEEP   14
/* 新增系统调用 */
#define SYSCALL_GETPPID 64
#define SYSCALL_UNLINK  10
#define SYSCALL_MKDIR   83
#define SYSCALL_RMDIR   84
#define SYSCALL_LSEEK   8
#define SYSCALL_ACCESS  21
#define SYSCALL_RENAME  82
#define SYSCALL_FSTAT   5
#define SYSCALL_GETCWD  79

#define MAX_FILES 16

extern void vga_puts(const char *s);
extern void vga_putchar(char c);
extern int ext2_64_read_file(const char *path, uint8_t *buf, int max_size);
extern int ext2_64_write_file(const char *path, const uint8_t *data, int size);
extern int ext2_64_create_file(const char *path);
extern void rtc64_get_time(int *year, int *month, int *day,
                            int *hour, int *minute, int *second);

/* 文件描述符表 */
typedef struct {
    int used;
    char path[256];
    int offset;
    int mode;  /* 0=read, 1=write, 2=read/write */
} sys_file_t;

static sys_file_t sys_files[MAX_FILES];
static int syscall_initialized = 0;

static void sys_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static int sys_strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

/* 初始化系统调用 */
void syscall64_full_init(void)
{
    for (int i = 0; i < MAX_FILES; i++) {
        sys_files[i].used = 0;
    }
    syscall_initialized = 1;
    vga_puts("[SYSCALL64] Full syscall interface initialized\n");
}

/* sys_write */
static int64_t sys_write(int fd, const char *buf, int count)
{
    if (count <= 0) return 0;
    if (count > 4096) count = 4096;  /* 限制单次写入大小 */

    /* 安全：从用户空间复制数据到内核缓冲区 */
    static uint8_t kernel_buf[4096];
    if (copy_from_user(kernel_buf, buf, count) != 0) {
        return -1;  /* 用户地址无效 */
    }

    if (fd == 1 || fd == 2) {  /* stdout/stderr */
        for (int i = 0; i < count; i++) {
            vga_putchar(kernel_buf[i]);
        }
        return count;
    }

    /* 文件写入 */
    if (fd >= 0 && fd < MAX_FILES && sys_files[fd].used) {
        ext2_64_write_file(sys_files[fd].path, kernel_buf, count);
        sys_files[fd].offset += count;
        return count;
    }

    return -1;
}

/* sys_read */
static int64_t sys_read(int fd, char *buf, int count)
{
    if (count <= 0) return 0;
    if (count > 4096) count = 4096;

    if (fd == 0) {  /* stdin - 从键盘缓冲区读取 */
        extern int keyboard64_haschar(void);
        extern int keyboard64_getchar(void);
        int read_count = 0;
        /* 阻塞等待至少一个字符 */
        while (!keyboard64_haschar()) {
            asm volatile("hlt");  /* 等待中断 */
        }
        /* 读取可用字符（最多count个） */
        while (read_count < count && keyboard64_haschar()) {
            int c = keyboard64_getchar();
            if (c < 0) break;
            /* 安全：复制到用户空间 */
            if (copy_to_user(buf + read_count, &c, 1) != 0) {
                return -1;
            }
            read_count++;
            /* 遇到换行停止（行缓冲） */
            if (c == '\n') break;
        }
        return read_count;
    }

    /* 文件读取 */
    if (fd >= 0 && fd < MAX_FILES && sys_files[fd].used) {
        static uint8_t file_buf[4096];
        int len = ext2_64_read_file(sys_files[fd].path, file_buf, sizeof(file_buf));
        if (len <= 0) return 0;
        int avail = len - sys_files[fd].offset;
        if (avail <= 0) return 0;
        int to_read = count < avail ? count : avail;
        if (to_read > (int)sizeof(file_buf)) to_read = sizeof(file_buf);

        /* 安全：从内核缓冲区复制到用户空间 */
        if (copy_to_user(buf, file_buf + sys_files[fd].offset, to_read) != 0) {
            return -1;  /* 用户地址无效 */
        }
        sys_files[fd].offset += to_read;
        return to_read;
    }

    return -1;
}

/* sys_open */
static int64_t sys_open(const char *path, int flags, int mode)
{
    /* 安全：从用户空间复制路径到内核缓冲区 */
    static char kernel_path[256];
    if (copy_from_user(kernel_path, path, 255) != 0) {
        return -1;  /* 用户地址无效 */
    }
    kernel_path[255] = 0;  /* 确保终止 */

    for (int i = 0; i < MAX_FILES; i++) {
        if (!sys_files[i].used) {
            sys_files[i].used = 1;
            int len = sys_strlen(kernel_path);
            if (len > 255) len = 255;
            sys_memcpy(sys_files[i].path, kernel_path, len);
            sys_files[i].path[len] = 0;
            sys_files[i].offset = 0;
            sys_files[i].mode = flags & 3;

            /* 如果是创建模式，创建文件 */
            if (flags & 0x40) {  /* O_CREAT */
                ext2_64_create_file(kernel_path);
            }

            return i;
        }
    }
    return -1;
}

/* sys_close */
static int64_t sys_close(int fd)
{
    if (fd >= 0 && fd < MAX_FILES && sys_files[fd].used) {
        sys_files[fd].used = 0;
        return 0;
    }
    return -1;
}

/* sys_exit */
static void sys_exit(int code)
{
    vga_puts("\n[SYSCALL64] Process exited with code ");
    char buf[8];
    int n = code, i = 0;
    char tmp[8];
    if (n < 0) n = -n;
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    if (code < 0) tmp[i++] = '-';
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts("\n");

    /* 如果在用户态，返回到Shell */
    extern void user_exit_to_kernel(void);
    user_exit_to_kernel();

    /* 如果不在用户态，停机 */
    while (1) { asm volatile("hlt"); }
}

/* sys_getpid */
static int64_t sys_getpid(void)
{
    return 1;  /* 简化：始终返回1 */
}

/* sys_time */
static int64_t sys_time(void)
{
    int year, month, day, hour, minute, second;
    rtc64_get_time(&year, &month, &day, &hour, &minute, &second);
    /* 简化：返回秒数（从午夜开始） */
    return hour * 3600 + minute * 60 + second;
}

/* sys_brk */
static int64_t sys_brk(uint64_t addr)
{
    /* 用户空间范围：0x40000000 - 0x70000000 */
    #define USER_BRK_BASE  0x40100000ULL  /* 1MB后开始 */
    #define USER_BRK_LIMIT 0x60000000ULL  /* 上限512MB */
    #define USER_BRK_INIT  0x40200000ULL  /* 初始2MB */

    static uint64_t program_break = USER_BRK_INIT;

    if (addr == 0) {
        /* 查询当前break */
        return program_break;
    }

    /* 边界检查 */
    if (addr < USER_BRK_BASE) {
        return program_break;  /* 无效地址，返回当前break */
    }
    if (addr > USER_BRK_LIMIT) {
        return program_break;  /* 超出上限，拒绝 */
    }

    /* 设置新的break（简化：假设用户空间已映射或按需分页） */
    program_break = addr;
    return program_break;
}

/* 外部函数 */
extern int process64_create(const char *name, void (*entry)(void), int priority);
extern int process64_get_current_pid(void);
extern void vga_put_hex(uint64_t val);

/* sys_fork - 创建子进程（简化版：创建新内核线程） */
static int64_t sys_fork(void)
{
    /* 简化版：创建一个新的内核线程作为子进程
     * 完整实现需要：复制页表、复制文件描述符、设置返回值等
     * 当前版本：创建新进程，父进程返回子PID，子进程返回0
     */
    vga_puts("[SYSCALL] fork() called\n");

    /* 创建子进程（使用fork_trampoline_64，子进程从syscall返回点继续执行） */
    extern void fork_trampoline_64(void);
    int child_pid = process64_create("fork_child", fork_trampoline_64, 2);

    if (child_pid < 0) {
        vga_puts("[SYSCALL] fork() failed: cannot create child\n");
        return -1;
    }

    vga_puts("[SYSCALL] fork() created child pid: ");
    char buf[8];
    int n = child_pid, i = 0;
    char tmp[8];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts("\n");

    /* 父进程返回子PID */
    return child_pid;
}

/* fork子进程入口 - 子进程从这里开始执行，模拟fork返回0 */
void fork_child_entry(void)
{
    vga_puts("[SYSCALL] fork child started (will return 0 to caller)\n");
    /* 子进程中fork返回0 - 通过系统调用返回机制
     * 当前架构下，子进程作为独立内核线程运行
     * 完整的fork（子进程从syscall返回点继续执行）需要在syscall入口复制用户态上下文
     * 暂用简化版：子进程执行完毕后退出
     */
    extern void process_exit_64(void);
    process_exit_64();
}

/* ELF64加载器外部声明 */
extern void elf64_init(void);
extern int elf64_verify(void *elf_data);
extern uint64_t elf64_get_entry(void *elf_data);
extern void *elf64_load(void *elf_data, uint64_t size);

/* sys_execve - 执行程序（V0.10.75: 集成ELF64加载器） */
static int64_t sys_execve(const char *path, char *const argv[], char *const envp[])
{
    (void)argv;
    (void)envp;

    /* 安全：从用户空间复制路径到内核缓冲区 */
    static char kernel_path[256];
    if (copy_from_user(kernel_path, path, 255) != 0) {
        vga_puts("[SYSCALL] execve() ERROR: invalid user path\n");
        return -1;
    }
    kernel_path[255] = 0;

    vga_puts("[SYSCALL] execve() called: ");
    vga_puts(kernel_path);
    vga_puts("\n");

    /* 1. 从ext2文件系统读取ELF文件 */
    static uint8_t elf_buffer[256 * 1024];  /* 256KB临时缓冲 */
    int file_size = ext2_64_read_file(kernel_path, elf_buffer, sizeof(elf_buffer));
    if (file_size <= 0) {
        vga_puts("[SYSCALL] execve() ERROR: Cannot read file\n");
        return -1;
    }

    vga_puts("[SYSCALL] execve() read ");
    char buf[16];
    int n = file_size, i = 0;
    char tmp[16];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts(" bytes\n");

    /* 2. 验证ELF64 */
    if (elf64_verify(elf_buffer) != 0) {
        vga_puts("[SYSCALL] execve() ERROR: Invalid ELF64 file\n");
        return -1;
    }

    /* 3. 获取入口点 */
    uint64_t entry = elf64_get_entry(elf_buffer);
    vga_puts("[SYSCALL] execve() entry point: 0x");
    vga_put_hex(entry);
    vga_puts("\n");

    /* 4. 加载ELF到内存 */
    void *loaded = elf64_load(elf_buffer, file_size);
    if (!loaded) {
        vga_puts("[SYSCALL] execve() ERROR: ELF load failed\n");
        return -1;
    }

    /* 5. 完整实现需要：
     * - 创建新的用户地址空间
     * - 映射ELF段到用户空间
     * - 设置用户栈（argv, envp）
     * - 切换到用户态并跳转到入口点
     *
     * 当前阶段：加载验证成功，返回入口点
     * 实际跳转待用户地址空间完善后实现
     */
    vga_puts("[SYSCALL] execve() ELF loaded successfully (jump pending)\n");
    return entry;
}

/* sys_sleep - 休眠（简化版：忙等待） */
static int64_t sys_sleep(uint64_t milliseconds)
{
    /* 简化版：忙等待（大约1ms per 100000次循环，取决于CPU速度） */
    volatile uint64_t count = milliseconds * 100000;
    while (count > 0) {
        count--;
        asm volatile("nop");
    }
    return 0;
}

/* sys_getppid - 获取父进程ID */
static int64_t sys_getppid(void)
{
    extern int process64_get_current_pid(void);
    extern void *process64_get_current(void);
    /* 简化：返回0（root父进程） */
    return 0;
}

/* sys_unlink - 删除文件 */
static int64_t sys_unlink(const char *path)
{
    if (!path) return -1;
    /* 安全：从用户空间复制路径 */
    char kpath[256];
    if (copy_from_user(kpath, path, 256) != 0) return -1;
    extern int ext2_64_delete_file(const char *path);
    return ext2_64_delete_file(kpath);
}

/* sys_mkdir - 创建目录 */
static int64_t sys_mkdir(const char *path, int mode)
{
    if (!path) return -1;
    (void)mode;
    char kpath[256];
    if (copy_from_user(kpath, path, 256) != 0) return -1;
    extern int ext2_64_create_dir(const char *path);
    return ext2_64_create_dir(kpath);
}

/* sys_rmdir - 删除目录 */
static int64_t sys_rmdir(const char *path)
{
    if (!path) return -1;
    char kpath[256];
    if (copy_from_user(kpath, path, 256) != 0) return -1;
    extern int ext2_64_delete_dir(const char *path);
    return ext2_64_delete_dir(kpath);
}

/* sys_lseek - 移动文件指针 */
static int64_t sys_lseek(int fd, int64_t offset, int whence)
{
    if (fd < 0 || fd >= MAX_FILES || !sys_files[fd].used) return -1;
    switch (whence) {
        case 0: /* SEEK_SET */
            sys_files[fd].offset = offset;
            break;
        case 1: /* SEEK_CUR */
            sys_files[fd].offset += offset;
            break;
        case 2: /* SEEK_END */
            /* 简化：不支持文件大小查询，返回当前偏移 */
            break;
        default:
            return -1;
    }
    return sys_files[fd].offset;
}

/* sys_access - 检查文件访问权限 */
static int64_t sys_access(const char *path, int mode)
{
    if (!path) return -1;
    (void)mode;
    char kpath[256];
    if (copy_from_user(kpath, path, 256) != 0) return -1;
    /* 简化：检查文件是否存在 */
    extern int ext2_64_read_file(const char *path, uint8_t *buf, int max_size);
    uint8_t tmp[1];
    int ret = ext2_64_read_file(kpath, tmp, 1);
    return (ret >= 0) ? 0 : -1;
}

/* sys_rename - 重命名文件 */
static int64_t sys_rename(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath) return -1;
    /* 简化：不支持重命名，返回-1 */
    (void)oldpath; (void)newpath;
    return -1;
}

/* sys_fstat - 获取文件状态 */
static int64_t sys_fstat(int fd, void *statbuf)
{
    if (fd < 0 || fd >= MAX_FILES || !sys_files[fd].used) return -1;
    (void)statbuf;
    /* 简化：返回0 */
    return 0;
}

/* sys_getcwd - 获取当前工作目录 */
static int64_t sys_getcwd(char *buf, size_t size)
{
    if (!buf || size == 0) return -1;
    /* 简化：返回根目录 */
    const char *cwd = "/";
    size_t len = 1;
    if (size < len + 1) return -1;
    if (copy_to_user(buf, cwd, len + 1) != 0) return -1;
    return (int64_t)buf;
}

/* 系统调用函数指针类型（统一6参数） */
typedef int64_t (*syscall_fn_t)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);

/* 系统调用表最大编号 */
#define SYSCALL_MAX 128

/* 包装函数：将具体系统调用转换为统一6参数接口 */
static int64_t wrap_write(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_write((int)a1, (const char*)a2, (int)a3);
}
static int64_t wrap_read(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_read((int)a1, (char*)a2, (int)a3);
}
static int64_t wrap_open(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_open((const char*)a1, (int)a2, (int)a3);
}
static int64_t wrap_close(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_close((int)a1);
}
static int64_t wrap_exit(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sys_exit((int)a1);
    return 0;
}
static int64_t wrap_getpid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_getpid();
}
static int64_t wrap_time(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_time();
}
static int64_t wrap_brk(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_brk((uint64_t)a1);
}
static int64_t wrap_fork(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_fork();
}
static int64_t wrap_execve(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_execve((const char*)a1, (char* const*)a2, (char* const*)a3);
}
static int64_t wrap_sleep(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_sleep((uint64_t)a1);
}
static int64_t wrap_getppid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_getppid();
}
static int64_t wrap_unlink(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_unlink((const char*)a1);
}
static int64_t wrap_mkdir(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_mkdir((const char*)a1, (int)a2);
}
static int64_t wrap_rmdir(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_rmdir((const char*)a1);
}
static int64_t wrap_lseek(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_lseek((int)a1, a2, (int)a3);
}
static int64_t wrap_access(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_access((const char*)a1, (int)a2);
}
static int64_t wrap_rename(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_rename((const char*)a1, (const char*)a2);
}
static int64_t wrap_fstat(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_fstat((int)a1, (void*)a2);
}
static int64_t wrap_getcwd(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_getcwd((char*)a1, (size_t)a2);
}

/* 系统调用表（函数指针数组，参考32位实现） */
static syscall_fn_t syscall_table[SYSCALL_MAX] = {
    [SYSCALL_WRITE]  = wrap_write,
    [SYSCALL_READ]   = wrap_read,
    [SYSCALL_OPEN]   = wrap_open,
    [SYSCALL_CLOSE]  = wrap_close,
    [SYSCALL_EXIT]   = wrap_exit,
    [SYSCALL_FORK]   = wrap_fork,
    [SYSCALL_EXECVE] = wrap_execve,
    [SYSCALL_BRK]    = wrap_brk,
    [SYSCALL_TIME]   = wrap_time,
    [SYSCALL_SLEEP]  = wrap_sleep,
    [SYSCALL_GETPID] = wrap_getpid,
    /* 新增系统调用 */
    [SYSCALL_GETPPID] = wrap_getppid,
    [SYSCALL_UNLINK]  = wrap_unlink,
    [SYSCALL_MKDIR]   = wrap_mkdir,
    [SYSCALL_RMDIR]   = wrap_rmdir,
    [SYSCALL_LSEEK]   = wrap_lseek,
    [SYSCALL_ACCESS]  = wrap_access,
    [SYSCALL_RENAME]  = wrap_rename,
    [SYSCALL_FSTAT]   = wrap_fstat,
    [SYSCALL_GETCWD]  = wrap_getcwd,
};

/* 系统调用主处理函数（查表调用） */
int64_t syscall64_dispatch(int number, int64_t arg1, int64_t arg2,
                            int64_t arg3, int64_t arg4, int64_t arg5)
{
    if (!syscall_initialized) return -1;

    /* 边界检查 */
    if (number < 0 || number >= SYSCALL_MAX) {
        vga_puts("[SYSCALL64] Unknown syscall: ");
        char buf[8];
        int n = number, i = 0;
        char tmp[8];
        do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
        int j = 0;
        while (i > 0) buf[j++] = tmp[--i];
        buf[j] = 0;
        vga_puts(buf);
        vga_puts("\n");
        return -1;
    }

    /* 检查函数指针是否存在 */
    if (!syscall_table[number]) {
        vga_puts("[SYSCALL64] Unimplemented syscall: ");
        char buf[8];
        int n = number, i = 0;
        char tmp[8];
        do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
        int j = 0;
        while (i > 0) buf[j++] = tmp[--i];
        buf[j] = 0;
        vga_puts(buf);
        vga_puts("\n");
        return -1;
    }

    /* 查表调用 */
    return syscall_table[number](arg1, arg2, arg3, arg4, arg5, 0);
}
