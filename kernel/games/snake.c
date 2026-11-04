/*
 * 赫普OS - 贪吃蛇游戏 (V6.50)
 *
 * 赫普OS第一个游戏应用，验证：
 * - 图形API（矩形/文字/清屏）
 * - 输入系统（键盘方向键控制）
 * - 窗口管理（游戏窗口+分数显示）
 *
 * 操作：
 * - 方向键/WASD：控制蛇的移动方向
 * - 空格：暂停/继续
 * - R：重新开始
 * - Q：退出游戏
 */

#include <stdint.h>
#include "vga_gfx.h"
#include "keyboard.h"
#include "pit.h"

/* 游戏配置 */
#define SNAKE_MAX_LEN  100
#define GRID_SIZE      10      /* 每个格子10x10像素 */
#define GRID_WIDTH     30      /* 30列 (300px) */
#define GRID_HEIGHT    18      /* 18行 (180px) */
#define GAME_X         10      /* 游戏区域X偏移 */
#define GAME_Y         10      /* 游戏区域Y偏移 */

/* 方向 */
#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

/* 游戏状态 */
#define STATE_PLAYING  0
#define STATE_PAUSED   1
#define STATE_GAMEOVER 2

/* 蛇身 */
static int snake_x[SNAKE_MAX_LEN];
static int snake_y[SNAKE_MAX_LEN];
static int snake_len = 3;
static int snake_dir = DIR_RIGHT;
static int next_dir = DIR_RIGHT;

/* 食物 */
static int food_x = 15;
static int food_y = 9;

/* 游戏状态 */
static int game_state = STATE_PLAYING;
static int score = 0;
static int high_score = 0;
static int game_tick = 0;
static int speed = 8;  /* 每8帧移动一次 */

/* 随机数生成 (简单LCG) */
static uint32_t rng_state = 12345;
static int rng_next(void)
{
    rng_state = rng_state * 1103515245 + 12345;
    return (int)((rng_state >> 16) & 0x7FFF);
}

/* 生成新食物 (避免在蛇身上) */
static void spawn_food(void)
{
    int tries = 0;
    do {
        food_x = rng_next() % GRID_WIDTH;
        food_y = rng_next() % GRID_HEIGHT;
        tries++;
    } while (tries < 100);
    /* 简单检查是否在蛇身上 */
    for (int i = 0; i < snake_len; i++) {
        if (snake_x[i] == food_x && snake_y[i] == food_y) {
            food_x = (food_x + 1) % GRID_WIDTH;
            break;
        }
    }
}

/* 初始化游戏 */
static void snake_init(void)
{
    snake_len = 3;
    snake_dir = DIR_RIGHT;
    next_dir = DIR_RIGHT;
    score = 0;
    game_state = STATE_PLAYING;
    game_tick = 0;

    /* 初始蛇位置 (中间) */
    snake_x[0] = 10; snake_y[0] = 9;  /* 头 */
    snake_x[1] = 9;  snake_y[1] = 9;
    snake_x[2] = 8;  snake_y[2] = 9;

    spawn_food();
}

/* 移动蛇 */
static void snake_move(void)
{
    /* 应用新方向 (禁止180度掉头) */
    if ((next_dir == DIR_UP && snake_dir != DIR_DOWN) ||
        (next_dir == DIR_DOWN && snake_dir != DIR_UP) ||
        (next_dir == DIR_LEFT && snake_dir != DIR_RIGHT) ||
        (next_dir == DIR_RIGHT && snake_dir != DIR_LEFT)) {
        snake_dir = next_dir;
    }

    /* 计算新头位置 */
    int new_x = snake_x[0];
    int new_y = snake_y[0];
    switch (snake_dir) {
        case DIR_UP:    new_y--; break;
        case DIR_DOWN:  new_y++; break;
        case DIR_LEFT:  new_x--; break;
        case DIR_RIGHT: new_x++; break;
    }

    /* 碰撞检测：墙壁 */
    if (new_x < 0 || new_x >= GRID_WIDTH ||
        new_y < 0 || new_y >= GRID_HEIGHT) {
        game_state = STATE_GAMEOVER;
        if (score > high_score) high_score = score;
        return;
    }

    /* 碰撞检测：自身 */
    for (int i = 0; i < snake_len; i++) {
        if (snake_x[i] == new_x && snake_y[i] == new_y) {
            game_state = STATE_GAMEOVER;
            if (score > high_score) high_score = score;
            return;
        }
    }

    /* 移动蛇身 (从尾到头) */
    for (int i = snake_len - 1; i > 0; i--) {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }
    snake_x[0] = new_x;
    snake_y[0] = new_y;

    /* 吃食物 */
    if (new_x == food_x && new_y == food_y) {
        score += 10;
        if (snake_len < SNAKE_MAX_LEN) {
            snake_len++;
            /* 新节在尾部 (下一帧移动时会正确位置) */
            snake_x[snake_len - 1] = snake_x[snake_len - 2];
            snake_y[snake_len - 1] = snake_y[snake_len - 2];
        }
        spawn_food();
        /* 加速 */
        if (speed > 3 && score % 50 == 0) speed--;
    }
}

/* 绘制游戏 */
static void snake_draw(void)
{
    /* 清屏 */
    gfx_clear(0);

    /* 绘制游戏区域边框 */
    gfx_rect(GAME_X - 1, GAME_Y - 1,
             GRID_WIDTH * GRID_SIZE + 2, GRID_HEIGHT * GRID_SIZE + 2, 7);

    /* 绘制网格 (淡色) */
    for (int x = 0; x <= GRID_WIDTH; x++) {
        gfx_vline(GAME_X + x * GRID_SIZE, GAME_Y, GRID_HEIGHT * GRID_SIZE, 1);
    }
    for (int y = 0; y <= GRID_HEIGHT; y++) {
        gfx_hline(GAME_X, GAME_Y + y * GRID_SIZE, GRID_WIDTH * GRID_SIZE, 1);
    }

    /* 绘制食物 (红色方块) */
    gfx_fillrect(GAME_X + food_x * GRID_SIZE + 1,
                 GAME_Y + food_y * GRID_SIZE + 1,
                 GRID_SIZE - 2, GRID_SIZE - 2, 4);

    /* 绘制蛇 */
    for (int i = 0; i < snake_len; i++) {
        uint8_t color = (i == 0) ? 10 : 2;  /* 头亮绿，身深绿 */
        gfx_fillrect(GAME_X + snake_x[i] * GRID_SIZE + 1,
                     GAME_Y + snake_y[i] * GRID_SIZE + 1,
                     GRID_SIZE - 2, GRID_SIZE - 2, color);
    }

    /* 绘制分数 */
    char score_buf[32];
    int si = 0;
    score_buf[si++] = 'S'; score_buf[si++] = 'c'; score_buf[si++] = 'o';
    score_buf[si++] = 'r'; score_buf[si++] = 'e'; score_buf[si++] = ':';
    score_buf[si++] = ' ';
    /* 简单数字转字符串 */
    int s = score;
    char digits[10];
    int dn = 0;
    if (s == 0) digits[dn++] = '0';
    while (s > 0) { digits[dn++] = '0' + s % 10; s /= 10; }
    for (int i = dn - 1; i >= 0; i--) score_buf[si++] = digits[i];
    score_buf[si] = 0;
    gfx_puts(GAME_X + 5, GAME_Y + GRID_HEIGHT * GRID_SIZE + 5, score_buf, 15);

    /* 绘制最高分 */
    char high_buf[32];
    si = 0;
    high_buf[si++] = 'H'; high_buf[si++] = 'i'; high_buf[si++] = 'g';
    high_buf[si++] = 'h'; high_buf[si++] = ':'; high_buf[si++] = ' ';
    s = high_score;
    dn = 0;
    if (s == 0) digits[dn++] = '0';
    while (s > 0) { digits[dn++] = '0' + s % 10; s /= 10; }
    for (int i = dn - 1; i >= 0; i--) high_buf[si++] = digits[i];
    high_buf[si] = 0;
    gfx_puts(GAME_X + 150, GAME_Y + GRID_HEIGHT * GRID_SIZE + 5, high_buf, 14);

    /* 绘制操作提示 */
    gfx_puts(GAME_X + 5, GAME_Y + GRID_HEIGHT * GRID_SIZE + 15,
             "Arrows/WASD:Move  Space:Pause  R:Restart", 7);

    /* 暂停/游戏结束覆盖层 */
    if (game_state == STATE_PAUSED) {
        gfx_fillrect(GAME_X + 80, GAME_Y + 70, 140, 40, 1);
        gfx_rect(GAME_X + 80, GAME_Y + 70, 140, 40, 15);
        gfx_puts(GAME_X + 120, GAME_Y + 80, "PAUSED", 15);
        gfx_puts(GAME_X + 100, GAME_Y + 95, "Press Space to continue", 7);
    } else if (game_state == STATE_GAMEOVER) {
        gfx_fillrect(GAME_X + 70, GAME_Y + 60, 160, 60, 1);
        gfx_rect(GAME_X + 70, GAME_Y + 60, 160, 60, 4);
        gfx_puts(GAME_X + 115, GAME_Y + 70, "GAME OVER", 4);
        gfx_puts(GAME_X + 110, GAME_Y + 85, "Press R to restart", 15);
        gfx_puts(GAME_X + 110, GAME_Y + 100, "Press Q to quit", 7);
    }

    /* 提交到显存 */
    gfx_flip();
}

/* 处理键盘输入 */
static void snake_handle_key(int key)
{
    if (game_state == STATE_PLAYING) {
        switch (key) {
            case 'w': case 'W':
                next_dir = DIR_UP; break;
            case 's': case 'S':
                next_dir = DIR_DOWN; break;
            case 'a': case 'A':
                next_dir = DIR_LEFT; break;
            case 'd': case 'D':
                next_dir = DIR_RIGHT; break;
            case ' ':  /* 空格暂停 */
                game_state = STATE_PAUSED; break;
        }
    } else if (game_state == STATE_PAUSED) {
        if (key == ' ') game_state = STATE_PLAYING;
    } else if (game_state == STATE_GAMEOVER) {
        if (key == 'r' || key == 'R') snake_init();
    }
}

/* 游戏主循环 (运行指定帧数后返回) */
int snake_game_run(int max_frames)
{
    extern int keyboard_try_getchar(char *c);
    extern void serial_puts(uint16_t, const char *);

    serial_puts(0x3F8, "[SNAKE] 游戏开始\n");
    snake_init();

    for (int frame = 0; frame < max_frames; frame++) {
        /* 处理键盘输入 (非阻塞) */
        char key = 0;
        if (keyboard_try_getchar(&key) > 0) {
            snake_handle_key((int)key);
            if (key == 'q' || key == 'Q') {
                serial_puts(0x3F8, "[SNAKE] 玩家退出\n");
                break;
            }
        }

        /* 游戏逻辑更新 */
        if (game_state == STATE_PLAYING) {
            game_tick++;
            if (game_tick >= speed) {
                game_tick = 0;
                snake_move();
            }
        }

        /* 绘制 */
        snake_draw();

        /* 简单延时 */
        for (volatile int i = 0; i < 50000; i++) {
            __asm__ volatile("nop");
        }
    }

    serial_puts(0x3F8, "[SNAKE] 游戏结束\n");
    return score;
}
