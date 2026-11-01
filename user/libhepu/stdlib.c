/* ============================================================================
 * 赫普OS - 标准库 stdlib
 * 内存分配、进程控制、随机数、字符串转换
 * V8.05: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 基本类型定义 */
typedef int pid_t;
typedef int uid_t;
typedef int gid_t;
typedef long off_t;
typedef long ssize_t;
typedef unsigned int mode_t;
typedef long time_t;

/* 内部使用的内存函数 */
static void *hepu_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static void *hepu_memset(void *dst, int val, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)val;
    return dst;
}

/* 系统调用号 */
#define SYS_EXIT    1
#define SYS_BRK     12
#define SYS_GETPID  20
#define SYS_FORK    2
#define SYS_WAITPID 7
#define SYS_EXECVE  11
#define SYS_TIME    13
#define SYS_SBRK    12

/* 系统调用 */
static inline uint32_t syscall1(uint32_t num, uint32_t arg1)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1)
        : "memory"
    );
    return ret;
}

static inline uint32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory"
    );
    return ret;
}

/* 简单的堆分配器 */
#define HEAP_BLOCK_MAGIC 0x48454150  /* "HEAP" */

typedef struct heap_block {
    uint32_t magic;
    uint32_t size;
    int free;
    struct heap_block *next;
} heap_block_t;

static heap_block_t *heap_head = NULL;
static void *heap_end = NULL;

/* 初始化堆 */
static void heap_init(void)
{
    if (heap_head) return;

    /* 通过sbrk获取初始堆空间 */
    heap_end = (void *)syscall1(SYS_BRK, 0);
    void *new_end = (void *)syscall1(SYS_BRK, (uint32_t)heap_end + 65536);
    if (new_end == heap_end) {
        return;  /* 分配失败 */
    }

    heap_head = (heap_block_t *)heap_end;
    heap_head->magic = HEAP_BLOCK_MAGIC;
    heap_head->size = 65536 - sizeof(heap_block_t);
    heap_head->free = 1;
    heap_head->next = NULL;
    heap_end = new_end;
}

/* 分配内存 */
void *malloc(size_t size)
{
    if (size == 0) return NULL;

    heap_init();

    if (!heap_head) return NULL;

    /* 对齐到8字节 */
    size = (size + 7) & ~7;

    /* 首次适应算法 */
    heap_block_t *block = heap_head;
    while (block) {
        if (block->free && block->size >= size) {
            /* 分割块 */
            if (block->size > size + sizeof(heap_block_t) + 8) {
                heap_block_t *new_block = (heap_block_t *)((uint8_t *)block + sizeof(heap_block_t) + size);
                new_block->magic = HEAP_BLOCK_MAGIC;
                new_block->size = block->size - size - sizeof(heap_block_t);
                new_block->free = 1;
                new_block->next = block->next;
                block->next = new_block;
                block->size = size;
            }
            block->free = 0;
            return (void *)((uint8_t *)block + sizeof(heap_block_t));
        }
        block = block->next;
    }

    /* 没有足够空间，扩展堆 */
    void *new_end = (void *)syscall1(SYS_BRK, (uint32_t)heap_end + size + sizeof(heap_block_t));
    if (new_end == heap_end) {
        return NULL;  /* 扩展失败 */
    }

    /* 找到最后一个块 */
    block = heap_head;
    while (block->next) block = block->next;

    /* 如果最后一个块是空闲的，合并 */
    if (block->free) {
        block->size += (uint32_t)new_end - (uint32_t)heap_end;
    } else {
        /* 创建新块 */
        heap_block_t *new_block = (heap_block_t *)heap_end;
        new_block->magic = HEAP_BLOCK_MAGIC;
        new_block->size = size;
        new_block->free = 0;
        new_block->next = NULL;
        block->next = new_block;
        block = new_block;
    }

    heap_end = new_end;
    return (void *)((uint8_t *)block + sizeof(heap_block_t));
}

/* 释放内存 */
void free(void *ptr)
{
    if (!ptr) return;

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_BLOCK_MAGIC) return;

    block->free = 1;

    /* 合并相邻空闲块 */
    block = heap_head;
    while (block && block->next) {
        if (block->free && block->next->free) {
            block->size += block->next->size + sizeof(heap_block_t);
            block->next = block->next->next;
        } else {
            block = block->next;
        }
    }
}

/* 重新分配内存 */
void *realloc(void *ptr, size_t size)
{
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    if (block->magic != HEAP_BLOCK_MAGIC) return NULL;

    if (block->size >= size) {
        return ptr;  /* 空间足够 */
    }

    /* 分配新空间并复制 */
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;

    hepu_memcpy(new_ptr, ptr, block->size);
    free(ptr);

    return new_ptr;
}

/* 分配并清零内存 */
void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        hepu_memset(ptr, 0, total);
    }
    return ptr;
}

/* 退出进程 */
void exit(int status)
{
    syscall1(SYS_EXIT, (uint32_t)status);
    while (1);  /* 不会到达这里 */
}

/* 异常终止 */
void abort(void)
{
    exit(1);
}

/* 获取进程ID */
pid_t getpid(void)
{
    return (pid_t)syscall1(SYS_GETPID, 0);
}

/* 创建子进程 */
pid_t fork(void)
{
    return (pid_t)syscall1(SYS_FORK, 0);
}

/* 等待子进程 */
pid_t waitpid(pid_t pid, int *status, int options)
{
    return (pid_t)syscall3(SYS_WAITPID, (uint32_t)pid, (uint32_t)status, (uint32_t)options);
}

/* 执行程序 */
int execve(const char *pathname, char *const argv[], char *const envp[])
{
    return (int)syscall3(SYS_EXECVE, (uint32_t)pathname, (uint32_t)argv, (uint32_t)envp);
}

/* 简单的随机数生成器 */
static uint32_t rand_state = 1;

void srand(unsigned int seed)
{
    rand_state = seed;
}

int rand(void)
{
    rand_state = rand_state * 1103515245 + 12345;
    return (int)((rand_state / 65536) % 32768);
}

/* 字符串转整数 */
int atoi(const char *nptr)
{
    int result = 0;
    int sign = 1;

    while (*nptr == ' ' || *nptr == '\t') nptr++;

    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }

    return sign * result;
}

/* 字符串转长整数 */
long atol(const char *nptr)
{
    return (long)atoi(nptr);
}

/* 整数转字符串 */
char *itoa(int value, char *str, int base)
{
    char *ptr = str;
    int negative = 0;

    if (value < 0 && base == 10) {
        negative = 1;
        value = -value;
    }

    if (value == 0) {
        *ptr++ = '0';
        *ptr = '\0';
        return str;
    }

    char temp[32];
    int idx = 0;

    while (value > 0) {
        int digit = value % base;
        temp[idx++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        value /= base;
    }

    if (negative) {
        *ptr++ = '-';
    }

    while (idx > 0) {
        *ptr++ = temp[--idx];
    }
    *ptr = '\0';

    return str;
}

/* 获取环境变量（简化：返回NULL） */
char *getenv(const char *name)
{
    (void)name;
    return NULL;
}

/* 系统名称 */
int system(const char *command)
{
    (void)command;
    return -1;  /* 简化：不支持 */
}
