/*
 * 赫普OS - 图形模式测试应用 (gfxtest)
 *
 * 测试VGA 320x200 256色图形模式：
 * 1. 切换到图形模式
 * 2. 绘制彩色条纹、矩形、渐变
 * 3. 通过/dev/fb0写入帧缓冲
 * 4. 切换回文本模式
 */

typedef unsigned char  uint8_t;
typedef unsigned int   uint32_t;

/* 系统调用号 */
#define SYS_GFX_INIT  200
#define SYS_GFX_TEXT  201
#define SYS_GFX_FLIP  202
#define SYS_GFX_DEMO  203
#define SYS_WM_DEMO   204
#define SYS_GAME_SNAKE 205
#define SYS_GAME_TETRIS 206
#define SYS_GAME_MINESWEEPER 207
#define SYS_OPEN      5
#define SYS_CLOSE     6
#define SYS_WRITE     4
#define SYS_LSEEK     19
#define SYS_EXIT      1

/* 内联系统调用 */
static inline uint32_t syscall1(uint32_t num, uint32_t arg1) {
    uint32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1) : "memory");
    return ret;
}

static inline uint32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3) : "memory");
    return ret;
}

#define FB_WIDTH  320
#define FB_HEIGHT 200
#define FB_SIZE   (FB_WIDTH * FB_HEIGHT)

static uint8_t framebuffer[FB_SIZE];

/* 画点 */
static void putpixel(int x, int y, uint8_t color) {
    if (x >= 0 && x < FB_WIDTH && y >= 0 && y < FB_HEIGHT) {
        framebuffer[y * FB_WIDTH + x] = color;
    }
}

/* 画矩形 */
static void fillrect(int x, int y, int w, int h, uint8_t color) {
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            putpixel(i, j, color);
        }
    }
}

/* 画水平线 */
static void hline(int x, int y, int w, uint8_t color) {
    for (int i = x; i < x + w; i++) {
        putpixel(i, y, color);
    }
}

/* 画垂直线 */
static void vline(int x, int y, int h, uint8_t color) {
    for (int j = y; j < y + h; j++) {
        putpixel(x, j, color);
    }
}

void _start(void) {
    /* 1. 切换到图形模式 */
    syscall1(SYS_GFX_INIT, 0);

    /* 2. 清屏（黑色） */
    for (int i = 0; i < FB_SIZE; i++) {
        framebuffer[i] = 0;
    }

    /* 3. 绘制彩色条纹（顶部16色条） */
    for (int c = 0; c < 16; c++) {
        fillrect(c * 20, 0, 20, 20, c);
    }

    /* 4. 绘制渐变（左侧） */
    for (int y = 20; y < 100; y++) {
        for (int x = 0; x < 160; x++) {
            uint8_t color = (x / 10) + (y / 10) * 16;
            if (color > 255) color = 255;
            putpixel(x, y, color);
        }
    }

    /* 5. 绘制彩色矩形（右侧） */
    fillrect(170, 25, 60, 30, 4);   /* 红 */
    fillrect(240, 25, 60, 30, 2);   /* 绿 */
    fillrect(170, 65, 60, 30, 1);   /* 蓝 */
    fillrect(240, 65, 60, 30, 14);  /* 黄 */

    /* 6. 绘制边框 */
    hline(0, 100, FB_WIDTH, 15);
    vline(0, 100, 100, 15);
    vline(FB_WIDTH - 1, 100, 100, 15);
    hline(0, FB_HEIGHT - 1, FB_WIDTH, 15);

    /* 7. 绘制对角线图案 */
    for (int i = 0; i < 100; i++) {
        putpixel(i + 10, i + 110, 12);  /* 浅红 */
        putpixel(100 - i + 10, i + 110, 10); /* 浅绿 */
    }

    /* 8. 绘制同心圆 */
    for (int r = 5; r < 40; r += 5) {
        for (int angle = 0; angle < 360; angle++) {
            int x = 240 + (int)(r * 3.14159 * angle / 180.0 / 3.14159);
            int y = 150 + (int)(r * 3.14159 * angle / 180.0 / 3.14159);
            /* 简化：用矩形近似圆 */
        }
    }
    /* 简化的圆：用多个矩形 */
    fillrect(220, 130, 40, 40, 0);
    fillrect(225, 125, 30, 50, 0);
    fillrect(215, 135, 50, 30, 0);
    fillrect(230, 140, 20, 20, 11); /* 青色圆心 */

    /* 9. 打开/dev/fb0并写入帧缓冲 */
    int fd = syscall3(SYS_OPEN, (uint32_t)"/dev/fb0", 2 /* O_RDWR */, 0);
    if (fd >= 0) {
        /* 分块写入（每次写1KB） */
        for (int offset = 0; offset < FB_SIZE; offset += 1024) {
            int size = 1024;
            if (offset + size > FB_SIZE) size = FB_SIZE - offset;
            syscall3(SYS_LSEEK, fd, offset, 0);
            syscall3(SYS_WRITE, fd, (uint32_t)(framebuffer + offset), size);
        }
        syscall1(SYS_CLOSE, fd);
    }

    /* 9.5 提交后台缓冲区到显存 (双缓冲flip, 关键！否则屏幕不更新) */
    syscall1(SYS_GFX_FLIP, 0);

    /* 10. 延时显示用户态图形（约2秒） */
    for (volatile int i = 0; i < 20000000; i++) {
        __asm__ volatile("nop");
    }

    /* 10.5 调用内核图形演示 (展示圆形/椭圆/三角形/渐变等绘图API) */
    syscall1(SYS_GFX_DEMO, 0);

    /* 11. 延时显示内核图形演示（约2秒） */
    for (volatile int i = 0; i < 20000000; i++) {
        __asm__ volatile("nop");
    }

    /* 11.5 调用窗口管理器演示 (展示多窗口/Z轴/焦点) */
    syscall1(SYS_WM_DEMO, 0);

    /* 12. 延时显示窗口管理器（约2秒） */
    for (volatile int i = 0; i < 20000000; i++) {
        __asm__ volatile("nop");
    }

    /* 12.5 启动扫雷游戏 (运行约12秒, 250帧) */
    syscall1(SYS_GAME_MINESWEEPER, 250);

    /* 13. 切换回文本模式 */
    syscall1(SYS_GFX_TEXT, 0);

    /* 14. 退出 */
    syscall1(SYS_EXIT, 0);
    while (1) { __asm__ volatile("hlt"); }
}
