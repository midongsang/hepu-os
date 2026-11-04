/*
 * 赫普OS - 扫雷游戏 (V6.50)
 *
 * 赫普OS第三个游戏，验证：
 * - 图形API（矩形/文字/数字颜色）
 * - 输入系统（鼠标/键盘）
 * - 游戏逻辑（雷区生成/数字计算/旗帜标记/连锁展开）
 *
 * 操作：
 * - 方向键/WASD：移动光标
 * - 空格/回车：翻开格子
 * - F：标记/取消旗帜
 * - R：重新开始
 * - Q：退出
 */

#include <stdint.h>
#include "vga_gfx.h"

/* 游戏配置 */
#define BOARD_COLS    16
#define BOARD_ROWS    12
#define CELL_SIZE     10
#define BOARD_X       70
#define BOARD_Y       30
#define MINE_COUNT    30

/* 格子状态 */
#define CELL_HIDDEN   0
#define CELL_REVEALED 1
#define CELL_FLAGGED  2

/* 游戏状态 */
#define STATE_PLAYING 0
#define STATE_WON     1
#define STATE_LOST    2

/* 游戏数据 */
static uint8_t board_mines[BOARD_ROWS][BOARD_COLS];   /* 1=有雷 */
static uint8_t board_numbers[BOARD_ROWS][BOARD_COLS]; /* 周围雷数 */
static uint8_t board_state[BOARD_ROWS][BOARD_COLS];   /* 格子状态 */
static int cursor_x = 0, cursor_y = 0;
static int game_state = STATE_PLAYING;
static int mines_flagged = 0;
static int cells_revealed = 0;
static int first_click = 1;

/* 数字颜色 */
static const uint8_t number_colors[9] = {
    0,  /* 0: 无数字 */
    9,  /* 1: 蓝色 */
    10, /* 2: 绿色 */
    12, /* 3: 红色 */
    13, /* 4: 紫色 */
    6,  /* 5: 棕色 */
    11, /* 6: 青色 */
    0,  /* 7: 黑色 */
    8   /* 8: 深灰 */
};

/* 随机数 */
static uint32_t rng_state = 13579;
static int rng_next(void)
{
    rng_state = rng_state * 1103515245 + 12345;
    return (int)((rng_state >> 16) & 0x7FFF);
}

/* 初始化棋盘（首次点击后生成雷，保证首次点击安全） */
static void minesweeper_init(void)
{
    for (int y = 0; y < BOARD_ROWS; y++) {
        for (int x = 0; x < BOARD_COLS; x++) {
            board_mines[y][x] = 0;
            board_numbers[y][x] = 0;
            board_state[y][x] = CELL_HIDDEN;
        }
    }
    cursor_x = BOARD_COLS / 2;
    cursor_y = BOARD_ROWS / 2;
    game_state = STATE_PLAYING;
    mines_flagged = 0;
    cells_revealed = 0;
    first_click = 1;
}

/* 生成雷区（避开首次点击位置及周围） */
static void generate_mines(int safe_x, int safe_y)
{
    int placed = 0;
    while (placed < MINE_COUNT) {
        int x = rng_next() % BOARD_COLS;
        int y = rng_next() % BOARD_ROWS;
        if (board_mines[y][x]) continue;
        /* 避开安全区域（3x3） */
        if (x >= safe_x - 1 && x <= safe_x + 1 &&
            y >= safe_y - 1 && y <= safe_y + 1) continue;
        board_mines[y][x] = 1;
        placed++;
    }
    /* 计算数字 */
    for (int y = 0; y < BOARD_ROWS; y++) {
        for (int x = 0; x < BOARD_COLS; x++) {
            if (board_mines[y][x]) continue;
            int count = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < BOARD_COLS && ny >= 0 && ny < BOARD_ROWS) {
                        if (board_mines[ny][nx]) count++;
                    }
                }
            }
            board_numbers[y][x] = (uint8_t)count;
        }
    }
}

/* 翻开格子（连锁展开空白区域） */
static void reveal_cell(int x, int y)
{
    if (x < 0 || x >= BOARD_COLS || y < 0 || y >= BOARD_ROWS) return;
    if (board_state[y][x] != CELL_HIDDEN) return;

    board_state[y][x] = CELL_REVEALED;
    cells_revealed++;

    /* 如果是空白格子（周围无雷），连锁展开 */
    if (board_numbers[y][x] == 0 && !board_mines[y][x]) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                reveal_cell(x + dx, y + dy);
            }
        }
    }
}

/* 检查胜利条件 */
static void check_win(void)
{
    int total_cells = BOARD_COLS * BOARD_ROWS;
    if (cells_revealed == total_cells - MINE_COUNT) {
        game_state = STATE_WON;
    }
}

/* 翻开当前格子 */
static void do_reveal(void)
{
    if (game_state != STATE_PLAYING) return;
    if (board_state[cursor_y][cursor_x] == CELL_FLAGGED) return;

    /* 首次点击生成雷区 */
    if (first_click) {
        generate_mines(cursor_x, cursor_y);
        first_click = 0;
    }

    if (board_mines[cursor_y][cursor_x]) {
        /* 踩雷，游戏结束 */
        board_state[cursor_y][cursor_x] = CELL_REVEALED;
        game_state = STATE_LOST;
        /* 显示所有雷 */
        for (int y = 0; y < BOARD_ROWS; y++)
            for (int x = 0; x < BOARD_COLS; x++)
                if (board_mines[y][x])
                    board_state[y][x] = CELL_REVEALED;
    } else {
        reveal_cell(cursor_x, cursor_y);
        check_win();
    }
}

/* 切换旗帜 */
static void toggle_flag(void)
{
    if (game_state != STATE_PLAYING) return;
    if (board_state[cursor_y][cursor_x] == CELL_HIDDEN) {
        board_state[cursor_y][cursor_x] = CELL_FLAGGED;
        mines_flagged++;
    } else if (board_state[cursor_y][cursor_x] == CELL_FLAGGED) {
        board_state[cursor_y][cursor_x] = CELL_HIDDEN;
        mines_flagged--;
    }
}

/* 绘制单个格子 */
static void draw_cell(int x, int y)
{
    int px = BOARD_X + x * CELL_SIZE;
    int py = BOARD_Y + y * CELL_SIZE;
    uint8_t state = board_state[y][x];

    if (state == CELL_HIDDEN) {
        /* 未翻开：凸起效果 */
        gfx_fillrect(px, py, CELL_SIZE, CELL_SIZE, 7);
        gfx_hline(px, py, CELL_SIZE - 1, 15);
        gfx_vline(px, py, CELL_SIZE - 1, 15);
        gfx_hline(px + 1, py + CELL_SIZE - 1, CELL_SIZE - 1, 8);
        gfx_vline(px + CELL_SIZE - 1, py + 1, CELL_SIZE - 1, 8);
    } else if (state == CELL_FLAGGED) {
        /* 旗帜 */
        gfx_fillrect(px, py, CELL_SIZE, CELL_SIZE, 7);
        gfx_fillrect(px + 3, py + 2, 4, 6, 12);  /* 红旗 */
        gfx_vline(px + 3, py + 2, 7, 0);           /* 旗杆 */
    } else {
        /* 已翻开 */
        gfx_fillrect(px, py, CELL_SIZE, CELL_SIZE, 8);
        gfx_rect(px, py, CELL_SIZE, CELL_SIZE, 7);

        if (board_mines[y][x]) {
            /* 雷 */
            gfx_fillrect(px + 2, py + 2, CELL_SIZE - 4, CELL_SIZE - 4, 12);
            gfx_putpixel(px + 3, py + 3, 15);
        } else if (board_numbers[y][x] > 0) {
            /* 数字 */
            char num[2];
            num[0] = '0' + board_numbers[y][x];
            num[1] = 0;
            gfx_puts(px + 2, py + 1, num, number_colors[board_numbers[y][x]]);
        }
    }
}

/* 绘制游戏 */
static void minesweeper_draw(void)
{
    /* 清屏 */
    gfx_clear(0);

    /* 标题 */
    gfx_puts(BOARD_X + 30, 5, "MINESWEEPER", 15);

    /* 绘制棋盘 */
    for (int y = 0; y < BOARD_ROWS; y++) {
        for (int x = 0; x < BOARD_COLS; x++) {
            draw_cell(x, y);
        }
    }

    /* 绘制光标 */
    int cx = BOARD_X + cursor_x * CELL_SIZE;
    int cy = BOARD_Y + cursor_y * CELL_SIZE;
    gfx_rect(cx - 1, cy - 1, CELL_SIZE + 2, CELL_SIZE + 2, 14);

    /* 状态栏 */
    char buf[32];
    int si = 0;
    buf[si++] = 'M'; buf[si++] = 'i'; buf[si++] = 'n'; buf[si++] = 'e';
    buf[si++] = 's'; buf[si++] = ':'; buf[si++] = ' ';
    int remaining = MINE_COUNT - mines_flagged;
    if (remaining < 0) remaining = 0;
    char digits[10]; int dn = 0;
    if (remaining == 0) digits[dn++] = '0';
    while (remaining > 0) { digits[dn++] = '0' + remaining % 10; remaining /= 10; }
    for (int i = dn - 1; i >= 0; i--) buf[si++] = digits[i];
    buf[si] = 0;
    gfx_puts(BOARD_X, BOARD_Y + BOARD_ROWS * CELL_SIZE + 5, buf, 12);

    /* 操作提示 */
    gfx_puts(BOARD_X, BOARD_Y + BOARD_ROWS * CELL_SIZE + 18,
             "Arrows:Move  Space:Reveal  F:Flag", 7);
    gfx_puts(BOARD_X + 20, BOARD_Y + BOARD_ROWS * CELL_SIZE + 28,
             "R:Restart  Q:Quit", 7);

    /* 胜利/失败覆盖层 */
    if (game_state == STATE_WON) {
        gfx_fillrect(BOARD_X + 20, BOARD_Y + 50, 120, 40, 2);
        gfx_rect(BOARD_X + 20, BOARD_Y + 50, 120, 40, 10);
        gfx_puts(BOARD_X + 45, BOARD_Y + 60, "YOU WIN!", 10);
        gfx_puts(BOARD_X + 35, BOARD_Y + 75, "Press R to restart", 15);
    } else if (game_state == STATE_LOST) {
        gfx_fillrect(BOARD_X + 20, BOARD_Y + 50, 120, 40, 1);
        gfx_rect(BOARD_X + 20, BOARD_Y + 50, 120, 40, 12);
        gfx_puts(BOARD_X + 45, BOARD_Y + 60, "GAME OVER", 12);
        gfx_puts(BOARD_X + 35, BOARD_Y + 75, "Press R to restart", 15);
    }

    /* 提交到显存 */
    gfx_flip();
}

/* 处理键盘输入 */
static void minesweeper_handle_key(char key)
{
    if (key == 'r' || key == 'R') {
        minesweeper_init();
        return;
    }
    if (game_state != STATE_PLAYING) return;

    switch (key) {
        case 'w': case 'W':  /* 上 */
            if (cursor_y > 0) cursor_y--;
            break;
        case 's': case 'S':  /* 下 */
            if (cursor_y < BOARD_ROWS - 1) cursor_y++;
            break;
        case 'a': case 'A':  /* 左 */
            if (cursor_x > 0) cursor_x--;
            break;
        case 'd': case 'D':  /* 右 */
            if (cursor_x < BOARD_COLS - 1) cursor_x++;
            break;
        case ' ':  /* 翻开 */
            do_reveal();
            break;
        case 'f': case 'F':  /* 旗帜 */
            toggle_flag();
            break;
    }
}

/* 游戏主循环 */
int minesweeper_game_run(int max_frames)
{
    extern int keyboard_try_getchar(char *c);
    extern void serial_puts(uint16_t, const char *);

    serial_puts(0x3F8, "[MINESWEEPER] 游戏开始\n");
    minesweeper_init();

    for (int frame = 0; frame < max_frames; frame++) {
        /* 处理键盘输入 */
        char key = 0;
        if (keyboard_try_getchar(&key) > 0) {
            minesweeper_handle_key(key);
            if (key == 'q' || key == 'Q') {
                serial_puts(0x3F8, "[MINESWEEPER] 玩家退出\n");
                break;
            }
        }

        /* 绘制 */
        minesweeper_draw();

        /* 简单延时 */
        for (volatile int i = 0; i < 40000; i++) {
            __asm__ volatile("nop");
        }
    }

    serial_puts(0x3F8, "[MINESWEEPER] 游戏结束\n");
    return cells_revealed;
}
