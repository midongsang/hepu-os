/*
 * 赫普OS - 俄罗斯方块游戏 (V6.50)
 *
 * 赫普OS第二个游戏，验证：
 * - 图形API（矩形/文字/清屏）
 * - 输入系统（键盘控制）
 * - 游戏逻辑（7种方块/旋转/消行/计分）
 *
 * 操作：
 * - 左右方向键/A D：移动
 * - 下方向键/S：加速下落
 * - 上方向键/W：旋转
 * - 空格：直接落底
 * - P：暂停
 * - R：重新开始
 * - Q：退出
 */

#include <stdint.h>
#include "vga_gfx.h"

/* 游戏配置 */
#define BOARD_WIDTH   10
#define BOARD_HEIGHT  20
#define BLOCK_SIZE    8
#define BOARD_X       100
#define BOARD_Y       10
#define PREVIEW_X     220
#define PREVIEW_Y     30

/* 方块颜色 */
#define COLOR_I  11  /* 青色 */
#define COLOR_O  14  /* 黄色 */
#define COLOR_T  13  /* 紫色 */
#define COLOR_S  10  /* 绿色 */
#define COLOR_Z  12  /* 红色 */
#define COLOR_J  9   /* 蓝色 */
#define COLOR_L  6   /* 橙色 */

/* 7种方块形状定义 (4x4网格, 1=填充) */
static const uint8_t tetrominoes[7][4][4] = {
    /* I */
    {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
    /* O */
    {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
    /* T */
    {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    /* S */
    {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
    /* Z */
    {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
    /* J */
    {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    /* L */
    {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}
};

static const uint8_t tetromino_colors[7] = {
    COLOR_I, COLOR_O, COLOR_T, COLOR_S, COLOR_Z, COLOR_J, COLOR_L
};

/* 游戏状态 */
static uint8_t board[BOARD_HEIGHT][BOARD_WIDTH];  /* 0=空, 1-7=方块颜色 */
static int current_x, current_y;
static int current_type;
static uint8_t current_shape[4][4];
static int next_type;
static int score = 0;
static int lines = 0;
static int level = 1;
static int game_over = 0;
static int paused = 0;
static int drop_timer = 0;
static int drop_speed = 30;  /* 每30帧下落一格 */

/* 随机数 */
static uint32_t rng_state = 67890;
static int rng_next(void)
{
    rng_state = rng_state * 1103515245 + 12345;
    return (int)((rng_state >> 16) & 0x7FFF);
}

/* 复制形状 */
static void copy_shape(uint8_t dst[4][4], const uint8_t src[4][4])
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            dst[i][j] = src[i][j];
}

/* 旋转形状 (顺时针90度) */
static void rotate_shape(uint8_t shape[4][4])
{
    uint8_t temp[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            temp[j][3 - i] = shape[i][j];
    copy_shape(shape, temp);
}

/* 检查碰撞 */
static int check_collision(int x, int y, uint8_t shape[4][4])
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (shape[i][j]) {
                int bx = x + j;
                int by = y + i;
                if (bx < 0 || bx >= BOARD_WIDTH || by >= BOARD_HEIGHT)
                    return 1;
                if (by >= 0 && board[by][bx])
                    return 1;
            }
        }
    }
    return 0;
}

/* 锁定方块到棋盘 */
static void lock_piece(void)
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (current_shape[i][j]) {
                int by = current_y + i;
                int bx = current_x + j;
                if (by >= 0 && by < BOARD_HEIGHT && bx >= 0 && bx < BOARD_WIDTH)
                    board[by][bx] = (uint8_t)(current_type + 1);
            }
        }
    }
}

/* 消除满行 */
static int clear_lines(void)
{
    int cleared = 0;
    for (int y = BOARD_HEIGHT - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (!board[y][x]) { full = 0; break; }
        }
        if (full) {
            cleared++;
            /* 上移所有行 */
            for (int yy = y; yy > 0; yy--)
                for (int x = 0; x < BOARD_WIDTH; x++)
                    board[yy][x] = board[yy - 1][x];
            for (int x = 0; x < BOARD_WIDTH; x++)
                board[0][x] = 0;
            y++;  /* 重新检查当前行 */
        }
    }
    return cleared;
}

/* 生成新方块 */
static void spawn_piece(void)
{
    current_type = next_type;
    next_type = rng_next() % 7;
    copy_shape(current_shape, tetrominoes[current_type]);
    current_x = 3;
    current_y = 0;

    /* 检查游戏结束 */
    if (check_collision(current_x, current_y, current_shape)) {
        game_over = 1;
    }
}

/* 初始化游戏 */
static void tetris_init(void)
{
    for (int y = 0; y < BOARD_HEIGHT; y++)
        for (int x = 0; x < BOARD_WIDTH; x++)
            board[y][x] = 0;
    score = 0;
    lines = 0;
    level = 1;
    game_over = 0;
    paused = 0;
    drop_timer = 0;
    drop_speed = 30;
    next_type = rng_next() % 7;
    spawn_piece();
}

/* 硬降（直接落底） */
static void hard_drop(void)
{
    while (!check_collision(current_x, current_y + 1, current_shape)) {
        current_y++;
    }
    lock_piece();
    int cleared = clear_lines();
    if (cleared > 0) {
        lines += cleared;
        score += cleared * 100 * level;
        /* 升级 */
        int new_level = lines / 10 + 1;
        if (new_level > level) {
            level = new_level;
            drop_speed = 30 - (level - 1) * 3;
            if (drop_speed < 5) drop_speed = 5;
        }
    }
    spawn_piece();
}

/* 绘制单个方块 */
static void draw_block(int x, int y, uint8_t color)
{
    gfx_fillrect(x, y, BLOCK_SIZE - 1, BLOCK_SIZE - 1, color);
    /* 高光 */
    gfx_hline(x + 1, y + 1, BLOCK_SIZE - 3, 15);
    gfx_vline(x + 1, y + 1, BLOCK_SIZE - 3, 15);
}

/* 绘制游戏 */
static void tetris_draw(void)
{
    /* 清屏 */
    gfx_clear(0);

    /* 绘制棋盘边框 */
    gfx_rect(BOARD_X - 1, BOARD_Y - 1,
             BOARD_WIDTH * BLOCK_SIZE + 2, BOARD_HEIGHT * BLOCK_SIZE + 2, 7);

    /* 绘制棋盘背景网格 */
    for (int y = 0; y < BOARD_HEIGHT; y++)
        for (int x = 0; x < BOARD_WIDTH; x++)
            gfx_rect(BOARD_X + x * BLOCK_SIZE, BOARD_Y + y * BLOCK_SIZE,
                     BLOCK_SIZE, BLOCK_SIZE, 1);

    /* 绘制已锁定的方块 */
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (board[y][x]) {
                draw_block(BOARD_X + x * BLOCK_SIZE,
                           BOARD_Y + y * BLOCK_SIZE,
                           tetromino_colors[board[y][x] - 1]);
            }
        }
    }

    /* 绘制当前方块 */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (current_shape[i][j]) {
                int by = current_y + i;
                if (by >= 0) {
                    draw_block(BOARD_X + (current_x + j) * BLOCK_SIZE,
                               BOARD_Y + by * BLOCK_SIZE,
                               tetromino_colors[current_type]);
                }
            }
        }
    }

    /* 绘制下一个方块预览 */
    gfx_puts(PREVIEW_X, PREVIEW_Y - 10, "NEXT:", 15);
    gfx_rect(PREVIEW_X - 1, PREVIEW_Y - 1, 5 * BLOCK_SIZE + 2, 5 * BLOCK_SIZE + 2, 7);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (tetrominoes[next_type][i][j]) {
                draw_block(PREVIEW_X + j * BLOCK_SIZE + 4,
                           PREVIEW_Y + i * BLOCK_SIZE + 4,
                           tetromino_colors[next_type]);
            }
        }
    }

    /* 绘制分数 */
    char buf[32];
    int si = 0;
    buf[si++] = 'S'; buf[si++] = 'c'; buf[si++] = 'o'; buf[si++] = 'r';
    buf[si++] = 'e'; buf[si++] = ':'; buf[si++] = ' ';
    int s = score;
    char digits[10]; int dn = 0;
    if (s == 0) digits[dn++] = '0';
    while (s > 0) { digits[dn++] = '0' + s % 10; s /= 10; }
    for (int i = dn - 1; i >= 0; i--) buf[si++] = digits[i];
    buf[si] = 0;
    gfx_puts(PREVIEW_X, PREVIEW_Y + 60, buf, 15);

    /* 绘制行数 */
    si = 0;
    buf[si++] = 'L'; buf[si++] = 'i'; buf[si++] = 'n'; buf[si++] = 'e';
    buf[si++] = 's'; buf[si++] = ':'; buf[si++] = ' ';
    s = lines; dn = 0;
    if (s == 0) digits[dn++] = '0';
    while (s > 0) { digits[dn++] = '0' + s % 10; s /= 10; }
    for (int i = dn - 1; i >= 0; i--) buf[si++] = digits[i];
    buf[si] = 0;
    gfx_puts(PREVIEW_X, PREVIEW_Y + 75, buf, 14);

    /* 绘制等级 */
    si = 0;
    buf[si++] = 'L'; buf[si++] = 'v'; buf[si++] = 'l'; buf[si++] = ':';
    buf[si++] = ' ';
    s = level; dn = 0;
    if (s == 0) digits[dn++] = '0';
    while (s > 0) { digits[dn++] = '0' + s % 10; s /= 10; }
    for (int i = dn - 1; i >= 0; i--) buf[si++] = digits[i];
    buf[si] = 0;
    gfx_puts(PREVIEW_X, PREVIEW_Y + 90, buf, 10);

    /* 操作提示 */
    gfx_puts(10, 180, "←→:Move  ↑:Rotate  ↓:Soft  Space:Hard", 7);
    gfx_puts(10, 190, "P:Pause  R:Restart  Q:Quit", 7);

    /* 暂停覆盖层 */
    if (paused) {
        gfx_fillrect(BOARD_X + 10, BOARD_Y + 80, 60, 30, 1);
        gfx_rect(BOARD_X + 10, BOARD_Y + 80, 60, 30, 15);
        gfx_puts(BOARD_X + 25, BOARD_Y + 88, "PAUSE", 15);
        gfx_puts(BOARD_X + 18, BOARD_Y + 100, "P to continue", 7);
    }

    /* 游戏结束覆盖层 */
    if (game_over) {
        gfx_fillrect(BOARD_X + 5, BOARD_Y + 70, 70, 50, 1);
        gfx_rect(BOARD_X + 5, BOARD_Y + 70, 70, 50, 4);
        gfx_puts(BOARD_X + 18, BOARD_Y + 80, "GAME OVER", 4);
        gfx_puts(BOARD_X + 20, BOARD_Y + 95, "R to restart", 15);
        gfx_puts(BOARD_X + 22, BOARD_Y + 108, "Q to quit", 7);
    }

    /* 提交到显存 */
    gfx_flip();
}

/* 处理键盘输入 */
static void tetris_handle_key(char key)
{
    if (game_over) {
        if (key == 'r' || key == 'R') tetris_init();
        return;
    }
    if (key == 'p' || key == 'P') {
        paused = !paused;
        return;
    }
    if (paused) return;

    switch (key) {
        case 'a': case 'A':  /* 左移 */
            if (!check_collision(current_x - 1, current_y, current_shape))
                current_x--;
            break;
        case 'd': case 'D':  /* 右移 */
            if (!check_collision(current_x + 1, current_y, current_shape))
                current_x++;
            break;
        case 's': case 'S':  /* 加速下落 */
            if (!check_collision(current_x, current_y + 1, current_shape)) {
                current_y++;
                score += 1;
            }
            break;
        case 'w': case 'W':  /* 旋转 */
        {
            uint8_t rotated[4][4];
            copy_shape(rotated, current_shape);
            rotate_shape(rotated);
            /* 尝试旋转，包括墙踢 */
            int kicks[3] = {0, -1, 1};
            for (int k = 0; k < 3; k++) {
                if (!check_collision(current_x + kicks[k], current_y, rotated)) {
                    copy_shape(current_shape, rotated);
                    current_x += kicks[k];
                    break;
                }
            }
            break;
        }
        case ' ':  /* 硬降 */
            hard_drop();
            break;
    }
}

/* 游戏主循环 */
int tetris_game_run(int max_frames)
{
    extern int keyboard_try_getchar(char *c);
    extern void serial_puts(uint16_t, const char *);

    serial_puts(0x3F8, "[TETRIS] 游戏开始\n");
    tetris_init();

    for (int frame = 0; frame < max_frames; frame++) {
        /* 处理键盘输入 */
        char key = 0;
        if (keyboard_try_getchar(&key) > 0) {
            tetris_handle_key(key);
            if (key == 'q' || key == 'Q') {
                serial_puts(0x3F8, "[TETRIS] 玩家退出\n");
                break;
            }
        }

        /* 游戏逻辑更新 */
        if (!paused && !game_over) {
            drop_timer++;
            if (drop_timer >= drop_speed) {
                drop_timer = 0;
                if (!check_collision(current_x, current_y + 1, current_shape)) {
                    current_y++;
                } else {
                    lock_piece();
                    int cleared = clear_lines();
                    if (cleared > 0) {
                        lines += cleared;
                        score += cleared * 100 * level;
                        int new_level = lines / 10 + 1;
                        if (new_level > level) {
                            level = new_level;
                            drop_speed = 30 - (level - 1) * 3;
                            if (drop_speed < 5) drop_speed = 5;
                        }
                    }
                    spawn_piece();
                }
            }
        }

        /* 绘制 */
        tetris_draw();

        /* 简单延时 */
        for (volatile int i = 0; i < 30000; i++) {
            __asm__ volatile("nop");
        }
    }

    serial_puts(0x3F8, "[TETRIS] 游戏结束\n");
    return score;
}
