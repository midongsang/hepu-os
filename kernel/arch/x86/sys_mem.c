/* ============================================================================
 * 赫普OS - 系统调用：MEM
 * ========================================================================== */

#include "types.h"
#include <stdint.h>
#include <stddef.h>
#include "syscall.h"
#include "vga.h"
#include "kheap.h"
#include "sched.h"
#include "hepu.h"
#include "fingerprint.h"
#include "vfs.h"
#include "ipc.h"
#include "process.h"
#include "paging.h"
#include "mm.h"
#include "constants.h"
extern const char *hepu_kernel_version;

/* V7.70安全修复: 用户态内存分配记录表
 * 跟踪sys_malloc/sys_mmap分配的地址，防止任意地址释放导致堆破坏 */
#define MAX_USER_ALLOCS 256
typedef struct {
    uint32_t addr;
    uint32_t size;
    int used;
} user_alloc_t;
static user_alloc_t user_allocs[MAX_USER_ALLOCS];

/* V7.78安全修复: 用户态堆空闲块链表（漏洞B完全修复）
 * sys_malloc不再返回kmalloc内核地址，而是通过brk在用户空间分配。
 * 维护简单的first-fit空闲链表，支持释放后重用。 */
#define USER_HEAP_FREE_MAX 128
typedef struct {
    uint32_t addr;
    uint32_t size;
    int used;
} user_heap_free_t;
static user_heap_free_t user_heap_free[USER_HEAP_FREE_MAX];

static int user_heap_free_add(uint32_t addr, uint32_t size) {
    for (int i = 0; i < USER_HEAP_FREE_MAX; i++) {
        if (!user_heap_free[i].used) {
            user_heap_free[i].addr = addr;
            user_heap_free[i].size = size;
            user_heap_free[i].used = 1;
            return i;
        }
    }
    return -1;
}

static int user_heap_free_find(uint32_t size) {
    /* first-fit: 找第一个足够大的空闲块 */
    for (int i = 0; i < USER_HEAP_FREE_MAX; i++) {
        if (user_heap_free[i].used && user_heap_free[i].size >= size) {
            return i;
        }
    }
    return -1;
}

static void user_heap_free_remove(int idx) {
    if (idx >= 0 && idx < USER_HEAP_FREE_MAX) {
        user_heap_free[idx].used = 0;
        user_heap_free[idx].addr = 0;
        user_heap_free[idx].size = 0;
    }
}

static int user_alloc_find(uint32_t addr) {
    for (int i = 0; i < MAX_USER_ALLOCS; i++) {
        if (user_allocs[i].used && user_allocs[i].addr == addr) return i;
    }
    return -1;
}

static int user_alloc_add(uint32_t addr, uint32_t size) {
    for (int i = 0; i < MAX_USER_ALLOCS; i++) {
        if (!user_allocs[i].used) {
            user_allocs[i].addr = addr;
            user_allocs[i].size = size;
            user_allocs[i].used = 1;
            return i;
        }
    }
    return -1;
}

static int user_alloc_remove(uint32_t addr) {
    int idx = user_alloc_find(addr);
    if (idx >= 0) {
        user_allocs[idx].used = 0;
        user_allocs[idx].addr = 0;
        user_allocs[idx].size = 0;
        return 0;
    }
    return -1;
}

/* V7.70安全修复: 用户指针校验函数
 * 校验用户空间指针范围，防止内核从任意地址读/写
 * 用户空间: 0x00000000-0xBFFFFFFF
 * 内核空间: 0xC0000000-0xFFFFFFFF (禁止用户访问) */
int user_ptr_valid(const void *ptr, uint32_t size) {
    uint32_t addr = (uint32_t)ptr;
    if (addr == 0) return 0;
    if (addr >= 0xC0000000) return 0;
    if (addr + size < addr) return 0;  /* 溢出 */
    if (addr + size > 0xC0000000) return 0;
    return 1;
}

/* 从用户空间复制到内核空间 */
int copy_from_user(void *dst, const void *src, uint32_t size) {
    if (!user_ptr_valid(src, size)) {
        vga_printf("[MEM] copy_from_user: invalid user ptr 0x%x size %d\n",
                   (uint32_t)src, size);
        return -1;
    }
    memcpy(dst, src, size);
    return 0;
}

/* 从内核空间复制到用户空间 */
int copy_to_user(void *dst, const void *src, uint32_t size) {
    if (!user_ptr_valid(dst, size)) {
        vga_printf("[MEM] copy_to_user: invalid user ptr 0x%x size %d\n",
                   (uint32_t)dst, size);
        return -1;
    }
    memcpy(dst, src, size);
    return 0;
}

/* V3.87: 进程堆顶设置/查询 (Linux brk 兼容——标准库 malloc 的基础).
 * 语义 (Linux i386 公开 ABI):
 *   brk(0)        → 返回当前堆顶 (查询)
 *   brk(new_brk)  → 尝试设置堆顶, 返回实际堆顶 (失败时保持原值)
 * 堆区间 [heap_start, heap_end), 由 ELF 加载器初始化 heap_start;
 * 增长逐页 mm_alloc_page + 映射到进程用户目录, 收缩 paging_unmap. */
u32 sys_brk(u32 new_brk, u32 arg2,
                         u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    extern process_t *current;
    if (!current || current->heap_start == 0) {
        return 0;   /* 无堆区进程 (原生 .hug/内核): 不支持 */
    }
    if (new_brk == 0) {
        return current->heap_end;   /* 查询当前堆顶 */
    }
    if (new_brk < current->heap_start) {
        return current->heap_end;   /* 低于堆起点: 拒绝 */
    }
    if (new_brk > USER_STACK_ADDR - 0x10000) {
        return current->heap_end;   /* 撞用户栈区 (栈顶之下留 64KB): 拒绝 */
    }

    uint32_t old_pg_end = (current->heap_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t new_pg_end = (new_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (new_pg_end > old_pg_end) {
        /* 增长: 逐页分配物理帧并映射 (用户可读写) */
        for (uint32_t a = old_pg_end; a < new_pg_end; a += PAGE_SIZE) {
            uint32_t frame = mm_alloc_page();
            if (!frame) {
                return current->heap_end;   /* 内存不足: 保持原堆顶 */
            }
            if (paging_map(current->page_dir, a, frame,
                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER) < 0) {
                return current->heap_end;
            }
        }
    } else if (new_pg_end < old_pg_end) {
        /* 收缩: 解除映射 (物理帧由 paging_unmap 归还) */
        for (uint32_t a = new_pg_end; a < old_pg_end; a += PAGE_SIZE) {
            paging_unmap(current->page_dir, a);
        }
    }

    current->heap_end = new_brk;
    return new_brk;
}

u32 sys_malloc(u32 size, u32 arg2,
                           u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    extern process_t *current;

    /* V7.78安全修复: sys_malloc使用用户态堆分配，不再返回kmalloc内核地址（漏洞B完全修复）
     * 原代码直接返回kmalloc内核地址，用户态可直接读写内核堆。
     * 修复：通过brk在用户空间分配，维护空闲链表支持重用。 */
    if (size == 0) return 0;

    /* 对齐到8字节 */
    size = (size + 7) & ~7;

    /* 1. 先从空闲链表找合适的块（first-fit） */
    int free_idx = user_heap_free_find(size);
    if (free_idx >= 0) {
        uint32_t addr = user_heap_free[free_idx].addr;
        uint32_t block_size = user_heap_free[free_idx].size;
        user_heap_free_remove(free_idx);

        /* 如果块比需要的大很多，拆分剩余部分 */
        if (block_size > size + 32) {
            user_heap_free_add(addr + size, block_size - size);
        }

        user_alloc_add(addr, size);
        return addr;
    }

    /* 2. 没有合适的空闲块，通过brk增加堆顶 */
    if (!current || current->heap_start == 0) {
        /* 无堆区进程：回退到kmalloc（兼容旧代码，但标记为内核分配）
         * 注意：这是临时兼容，后续应让所有用户态进程都有堆区 */
        void *ptr = kmalloc(size);
        if (!ptr) return 0;
        user_alloc_add((uint32_t)ptr, size);
        return (u32)ptr;
    }

    uint32_t old_brk = current->heap_end;
    uint32_t new_brk = old_brk + size;

    /* 调用sys_brk增加堆顶 */
    u32 result = sys_brk(new_brk, 0, 0, 0);
    if (result != new_brk) {
        /* brk失败 */
        return 0;
    }

    user_alloc_add(old_brk, size);
    return old_brk;
}

u32 sys_free(u32 ptr, u32 arg2,
                         u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    /* V7.70/V7.78安全修复: 校验地址，防止任意地址释放
     * 1. 不允许释放NULL
     * 2. 不允许释放内核空间地址 (>=0xC0000000)
     * 3. 必须是之前通过sys_malloc分配的地址
     * V7.78: 用户态堆分配加入空闲链表，不再kfree */
    if (ptr == 0) return -1;
    if (ptr >= 0xC0000000) {
        vga_printf("[MEM] sys_free: kernel addr 0x%x rejected\n", ptr);
        return -1;
    }
    int alloc_idx = user_alloc_find(ptr);
    if (alloc_idx < 0) {
        vga_printf("[MEM] sys_free: addr 0x%x not allocated\n", ptr);
        return -1;
    }

    uint32_t size = user_allocs[alloc_idx].size;
    user_alloc_remove(ptr);

    /* V7.78: 用户态堆分配加入空闲链表，支持重用
     * （kmalloc回退的内核地址仍用kfree） */
    if (ptr >= 0xC0000000) {
        kfree((void *)ptr);
    } else {
        user_heap_free_add(ptr, size);
    }
    return 0;
}

u32 sys_shmget(u32 key, u32 size,
                           u32 arg3, u32 arg4)
{
    (void)arg3; (void)arg4;
    return ipc_shm_get(key, size);
}

u32 sys_shmat(u32 shm_id, u32 arg2,
                          u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    return (u32)ipc_shm_attach(shm_id, sched_current_pid());
}

/* V3.77: 释放共享内存段（补 shmget 回收路径, 防止反复分配耗尽 kheap） */
u32 sys_shmdt(u32 shm_id, u32 arg2,
                          u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    return (u32)ipc_shm_release(shm_id, sched_current_pid());
}

u32 sys_mmap(u32 addr, u32 length, u32 prot, u32 flags)
{
    (void)addr; (void)prot; (void)flags;
    /* V7.78安全修复: sys_mmap使用用户态堆分配，不再返回kmalloc内核地址（漏洞B）
     * mmap需要页对齐，通过brk分配页对齐的用户空间内存。 */
    if (length == 0) return 0;

    /* 页对齐 */
    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    /* 从空闲链表找页对齐的块 */
    for (int i = 0; i < USER_HEAP_FREE_MAX; i++) {
        if (user_heap_free[i].used &&
            user_heap_free[i].size >= length &&
            (user_heap_free[i].addr & (PAGE_SIZE - 1)) == 0) {
            uint32_t ret = user_heap_free[i].addr;
            user_heap_free_remove(i);
            user_alloc_add(ret, length);
            return ret;
        }
    }

    /* 通过brk分配 */
    extern process_t *current;
    if (!current || current->heap_start == 0) {
        /* 无堆区进程：回退到kmalloc */
        void *ptr = kmalloc(length);
        if (!ptr) return 0;
        user_alloc_add((uint32_t)ptr, length);
        return (u32)ptr;
    }

    uint32_t old_brk = current->heap_end;
    /* 页对齐 */
    old_brk = (old_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t new_brk = old_brk + length;

    u32 result = sys_brk(new_brk, 0, 0, 0);
    if (result != new_brk) {
        return 0;
    }

    user_alloc_add(old_brk, length);
    return old_brk;
}

u32 sys_munmap(u32 addr, u32 length, u32 arg3, u32 arg4)
{
    (void)length; (void)arg3; (void)arg4;
    /* V7.70安全修复: 校验地址，防止任意地址释放 */
    if (addr == 0) return -1;
    if (addr >= 0xC0000000) {
        vga_printf("[MEM] sys_munmap: kernel addr 0x%x rejected\n", addr);
        return -1;
    }
    if (user_alloc_find(addr) < 0) {
        vga_printf("[MEM] sys_munmap: addr 0x%x not allocated\n", addr);
        return -1;
    }
    user_alloc_remove(addr);
    kfree((void *)addr);
    return 0;
}

u32 sys_mprotect(u32 addr, u32 length, u32 prot, u32 arg4)
{
    (void)addr; (void)length; (void)prot; (void)arg4;
    return 0;
}

/* V4.02: 初始化VGA图形模式 (320x200 256色, 供gfxtest等用户态应用使用) */
u32 sys_gfx_init(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    extern void vga_gfx_init(void);
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[GFX] sys_gfx_init: entering VGA 320x200x256 mode\n");
    vga_gfx_init();
    serial_puts(0x3F8, "[GFX] sys_gfx_init: graphics mode active\n");
    return 0;
}

/* V4.02: 切换回VGA文本模式 (80x25) */
u32 sys_gfx_text(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    extern void vga_gfx_text_mode(void);
    vga_gfx_text_mode();
    return 0;
}

/* V6.50: 提交后台缓冲区到显存 (双缓冲flip, 解决gfxtest写入后不显示的问题) */
u32 sys_gfx_flip(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    extern void gfx_flip(void);
    gfx_flip();
    return 0;
}

/* V6.50: 内核图形演示 — 展示所有绘图API (圆形/椭圆/三角形/渐变/文字) */
u32 sys_gfx_demo(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    extern void gfx_clear(uint8_t);
    extern void gfx_fillrect(int, int, int, int, uint8_t);
    extern void gfx_rect(int, int, int, int, uint8_t);
    extern void gfx_line(int, int, int, int, uint8_t);
    extern void gfx_circle(int, int, int, uint8_t);
    extern void gfx_fillcircle(int, int, int, uint8_t);
    extern void gfx_ellipse(int, int, int, int, uint8_t);
    extern void gfx_triangle(int, int, int, int, int, int, uint8_t);
    extern void gfx_gradient_h(int, int, int, int, uint8_t, uint8_t);
    extern void gfx_gradient_v(int, int, int, int, uint8_t, uint8_t);
    extern void gfx_puts(int, int, const char *, uint8_t);
    extern void gfx_flip(void);
    extern void serial_puts(uint16_t, const char *);

    serial_puts(0x3F8, "[GFX-DEMO] 开始绘制图形演示...\n");

    /* 清屏为深蓝色 */
    gfx_clear(1);

    /* 标题 */
    gfx_puts(80, 5, "HepuOS Graphics Demo", 15);

    /* 1. 渐变背景条 */
    gfx_gradient_h(0, 15, 320, 8, 9, 12);

    /* 2. 圆形 (空心+填充) */
    gfx_circle(40, 50, 20, 14);       /* 黄色空心圆 */
    gfx_fillcircle(90, 50, 15, 12);    /* 红色填充圆 */
    gfx_circle(140, 50, 25, 10);       /* 绿色大圆 */

    /* 3. 椭圆 */
    gfx_ellipse(200, 50, 30, 15, 13);  /* 紫色椭圆 */
    gfx_ellipse(260, 50, 20, 25, 11);  /* 青色竖椭圆 */

    /* 4. 三角形 */
    gfx_triangle(30, 90, 60, 70, 90, 90, 14);
    gfx_triangle(110, 90, 140, 70, 170, 90, 12);
    gfx_triangle(190, 90, 220, 70, 250, 90, 10);

    /* 5. 矩形 (边框+填充) */
    gfx_rect(10, 105, 50, 30, 15);
    gfx_fillrect(70, 105, 50, 30, 9);
    gfx_rect(130, 105, 50, 30, 14);
    gfx_fillrect(190, 105, 50, 30, 13);

    /* 6. 直线 */
    gfx_line(10, 150, 300, 150, 7);
    gfx_line(10, 160, 300, 170, 8);
    gfx_line(10, 170, 300, 160, 8);

    /* 7. 底部渐变条 */
    gfx_gradient_v(0, 180, 320, 20, 1, 9);

    /* 8. 底部文字 */
    gfx_puts(60, 188, "Circles Ellipses Triangles Gradients", 15);

    /* 提交到显存 */
    gfx_flip();
    serial_puts(0x3F8, "[GFX-DEMO] 图形演示绘制完成并flip提交\n");
    return 0;
}

/* V6.50: 窗口管理器+UI控件演示 — 创建多窗口和控件并渲染 */
u32 sys_wm_demo(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    extern void wm_init(void);
    extern void *wm_create_window(const char *, int, int, int, int);
    extern void *wm_add_button(void *, int, int, int, int, const char *, void *);
    extern void *wm_add_label(void *, int, int, const char *);
    extern void *wm_add_input(void *, int, int, int, const char *);
    extern void *wm_add_checkbox(void *, int, int, const char *, int);
    extern void *wm_add_progress(void *, int, int, int, int, int);
    extern void wm_draw_all(void);
    extern void serial_puts(uint16_t, const char *);

    serial_puts(0x3F8, "[WM-DEMO] 初始化窗口管理器...\n");
    wm_init();

    /* 创建3个测试窗口 */
    serial_puts(0x3F8, "[WM-DEMO] 创建窗口1: File Manager\n");
    wm_create_window("File Manager", 10, 20, 130, 90);

    serial_puts(0x3F8, "[WM-DEMO] 创建窗口2: Terminal\n");
    wm_create_window("Terminal", 40, 45, 110, 70);

    /* 创建控件演示窗口 */
    serial_puts(0x3F8, "[WM-DEMO] 创建窗口3: Control Panel (UI控件演示)\n");
    void *panel = wm_create_window("Control Panel", 160, 30, 140, 150);

    /* 添加UI控件 */
    wm_add_label(panel, 10, 18, "UI Widgets Demo:");
    wm_add_button(panel, 10, 32, 50, 14, "OK", 0);
    wm_add_button(panel, 70, 32, 50, 14, "Cancel", 0);
    wm_add_label(panel, 10, 52, "Name:");
    wm_add_input(panel, 45, 50, 80, "HepuOS");
    wm_add_checkbox(panel, 10, 68, "Enable feature", 1);
    wm_add_checkbox(panel, 10, 82, "Debug mode", 0);
    wm_add_label(panel, 10, 98, "Progress:");
    wm_add_progress(panel, 10, 110, 110, 8, 65);
    wm_add_label(panel, 10, 124, "65% complete");

    /* 绘制所有窗口 */
    serial_puts(0x3F8, "[WM-DEMO] 绘制所有窗口(含UI控件)...\n");
    wm_draw_all();
    serial_puts(0x3F8, "[WM-DEMO] 窗口+控件绘制完成\n");
    return 0;
}

/* V6.50: 贪吃蛇游戏 — 赫普OS第一个游戏，验证图形/输入/窗口子系统 */
u32 sys_game_snake(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    extern int snake_game_run(int max_frames);
    extern void serial_puts(uint16_t, const char *);

    int frames = (int)arg1;
    if (frames <= 0) frames = 300;  /* 默认运行300帧约15秒 */

    serial_puts(0x3F8, "[GAME] 启动贪吃蛇游戏...\n");
    int score = snake_game_run(frames);
    serial_puts(0x3F8, "[GAME] 贪吃蛇结束，得分: ");
    /* 简单输出分数 */
    char buf[16];
    int si = 0;
    int s = score;
    if (s == 0) buf[si++] = '0';
    while (s > 0) { buf[si++] = '0' + s % 10; s /= 10; }
    buf[si] = 0;
    /* 反转 */
    for (int i = 0; i < si / 2; i++) {
        char t = buf[i]; buf[i] = buf[si - 1 - i]; buf[si - 1 - i] = t;
    }
    serial_puts(0x3F8, buf);
    serial_puts(0x3F8, "\n");
    return (u32)score;
}

/* V6.50: 俄罗斯方块游戏 — 赫普OS第二个游戏 */
u32 sys_game_tetris(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    extern int tetris_game_run(int max_frames);
    extern void serial_puts(uint16_t, const char *);

    int frames = (int)arg1;
    if (frames <= 0) frames = 400;

    serial_puts(0x3F8, "[GAME] 启动俄罗斯方块...\n");
    int score = tetris_game_run(frames);
    serial_puts(0x3F8, "[GAME] 俄罗斯方块结束，得分: ");
    char buf[16];
    int si = 0;
    int s = score;
    if (s == 0) buf[si++] = '0';
    while (s > 0) { buf[si++] = '0' + s % 10; s /= 10; }
    buf[si] = 0;
    for (int i = 0; i < si / 2; i++) {
        char t = buf[i]; buf[i] = buf[si - 1 - i]; buf[si - 1 - i] = t;
    }
    serial_puts(0x3F8, buf);
    serial_puts(0x3F8, "\n");
    return (u32)score;
}

/* V6.50: 扫雷游戏 — 赫普OS第三个游戏 */
u32 sys_game_minesweeper(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    extern int minesweeper_game_run(int max_frames);
    extern void serial_puts(uint16_t, const char *);

    int frames = (int)arg1;
    if (frames <= 0) frames = 300;

    serial_puts(0x3F8, "[GAME] 启动扫雷...\n");
    int revealed = minesweeper_game_run(frames);
    serial_puts(0x3F8, "[GAME] 扫雷结束\n");
    return (u32)revealed;
}


/* V0.9.0: 3D图形演示 — 展示3D渲染器能力 (P5-5硬件加速深化) */

/* 简单的sin/cos近似（泰勒展开，精度足够用于演示） */
static float demo_sin(float x)
{
    while (x > 3.14159f) x -= 6.28318f;
    while (x < -3.14159f) x += 6.28318f;
    float x2 = x * x;
    return x * (1.0f - x2/6.0f + x2*x2/120.0f - x2*x2*x2/5040.0f);
}
static float demo_cos(float x)
{
    return demo_sin(x + 1.5708f);
}

u32 sys_gfx_3d_demo(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    extern void serial_puts(uint16_t, const char *);

    /* 3D渲染器API（extern声明） */
    extern int gfx3d_init(int width, int height);
    extern void gfx3d_shutdown(void);
    extern void gfx3d_clear(uint32_t color);
    extern void gfx3d_clear_depth(float depth);
    extern void gfx3d_set_model_matrix(void *m);
    extern void gfx3d_set_view_matrix(void *m);
    extern void gfx3d_set_projection_matrix(void *m);
    extern void gfx3d_update_mvp(void);
    extern void gfx3d_set_viewport(int x, int y, int w, int h);
    extern void gfx3d_enable_depth_test(int enable);
    extern void gfx3d_enable_cull_face(int enable);
    extern void gfx3d_enable_lighting(int enable);
    extern void gfx3d_set_light_direction(float x, float y, float z);
    extern void gfx3d_set_light_color(float r, float g, float b);
    extern void gfx3d_set_ambient_color(float r, float g, float b);
    extern void gfx3d_draw_cube(float size, uint32_t color);
    extern void gfx3d_draw_sphere(float radius, int segments, int rings, uint32_t color);
    extern void gfx3d_draw_cylinder(float radius, float height, int segments, uint32_t color);
    extern void gfx3d_reset_stats(void);
    extern void gfx3d_get_stats(uint32_t *triangles, uint32_t *pixels, uint32_t *vertices);

    int frames = (int)arg1;
    if (frames <= 0) frames = 120;

    serial_puts(0x3F8, "[3D-DEMO] 启动3D图形演示...\n");

    /* 初始化3D渲染器（使用当前分辨率） */
    int width = 800, height = 600;
    if (gfx3d_init(width, height) != 0) {
        serial_puts(0x3F8, "[3D-DEMO] 3D渲染器初始化失败\n");
        return 1;
    }

    /* 设置透视投影 */
    /* 简化：使用单位矩阵作为投影（实际应使用透视矩阵） */
    float proj[16] = {
        1.0f, 0, 0, 0,
        0, 1.0f, 0, 0,
        0, 0, 1.0f, 0,
        0, 0, 0, 1.0f
    };
    gfx3d_set_projection_matrix(proj);

    /* 设置视图矩阵 */
    float view[16] = {
        1.0f, 0, 0, 0,
        0, 1.0f, 0, 0,
        0, 0, 1.0f, 0,
        0, 0, -5.0f, 1.0f  /* 相机在z=5 */
    };
    gfx3d_set_view_matrix(view);

    /* 启用渲染状态 */
    gfx3d_enable_depth_test(1);
    gfx3d_enable_cull_face(1);
    gfx3d_enable_lighting(1);

    /* 设置光照 */
    gfx3d_set_light_direction(0.5f, 1.0f, 0.8f);
    gfx3d_set_light_color(1.0f, 1.0f, 1.0f);
    gfx3d_set_ambient_color(0.3f, 0.3f, 0.3f);

    /* 渲染循环 */
    for (int i = 0; i < frames; i++) {
        /* 清屏 */
        gfx3d_clear(0x000033);
        gfx3d_clear_depth(1.0f);
        gfx3d_reset_stats();

        /* 旋转角度 */
        float angle = (float)i * 0.05f;

        /* 模型矩阵（绕Y轴旋转） */
        float model[16] = {
            demo_cos(angle), 0, demo_sin(angle), 0,
            0, 1.0f, 0, 0,
            -demo_sin(angle), 0, demo_cos(angle), 0,
            0, 0, 0, 1.0f
        };
        gfx3d_set_model_matrix(model);
        gfx3d_update_mvp();

        /* 绘制立方体 */
        gfx3d_draw_cube(1.5f, 0x00FFFF); /* 青色 */

        /* 每30帧输出统计 */
        if (i % 30 == 0) {
            uint32_t triangles, pixels, vertices;
            gfx3d_get_stats(&triangles, &pixels, &vertices);
            char buf[64];
            /* 简单格式化 */
            int pos = 0;
            const char *prefix = "[3D-DEMO] Frame ";
            while (*prefix) buf[pos++] = *prefix++;
            int tmp = i;
            char num[16]; int nlen = 0;
            if (tmp == 0) num[nlen++] = '0';
            while (tmp > 0) { num[nlen++] = '0' + tmp % 10; tmp /= 10; }
            while (nlen > 0) buf[pos++] = num[--nlen];
            buf[pos++] = ' ';
            buf[pos] = '\0';
            serial_puts(0x3F8, buf);
        }
    }

    /* 清理 */
    gfx3d_shutdown();
    serial_puts(0x3F8, "[3D-DEMO] 3D演示结束\n");
    return 0;
}
