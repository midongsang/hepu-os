/* 桌面注册表 —— "可切换的图形化桌面接口"的实现体。
 *
 * 只做注册/选择/遍历, 不含任何绘制逻辑, 因此 32 位和 64 位都可以直接编进去。
 * 用户想加自己的桌面: 写一个 desk_backend_t, 调 desk_register(), 就完事。 */

#include "desk_api.h"

/* 不引 libc, 自带一个最小比较 */
static int strcmp_simple(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return (int)(*a - *b);
        a++; b++;
    }
    return (int)(*a - *b);
}

static desk_backend_t *g_desks[DESK_MAX_BACKENDS];
static int             g_desk_count = 0;
static int             g_desk_active = -1;

int desk_register(desk_backend_t *d)
{
    if (!d || !d->name) return -1;
    if (g_desk_count >= DESK_MAX_BACKENDS) return -1;
    for (int i = 0; i < g_desk_count; i++) {
        if (g_desks[i] && g_desks[i]->name &&
            strcmp_simple(g_desks[i]->name, d->name) == 0) {
            return -2;
        }
    }
    g_desks[g_desk_count++] = d;
    if (g_desk_active < 0) g_desk_active = 0;   /* 第一个注册的成为默认 */
    return 0;
}

int desk_select(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < g_desk_count; i++) {
        if (g_desks[i] && g_desks[i]->name &&
            strcmp_simple(g_desks[i]->name, name) == 0) {
            g_desk_active = i;
            return 0;
        }
    }
    return -1;
}

desk_backend_t *desk_current(void)
{
    if (g_desk_active < 0 || g_desk_active >= g_desk_count) return 0;
    return g_desks[g_desk_active];
}

desk_backend_t *desk_at(int index)
{
    if (index < 0 || index >= g_desk_count) return 0;
    return g_desks[index];
}

int desk_count(void)
{
    return g_desk_count;
}

int desk_list(const char **out, int max)
{
    int n = 0;
    for (int i = 0; i < g_desk_count && n < max; i++) {
        if (g_desks[i] && g_desks[i]->name) out[n++] = g_desks[i]->name;
    }
    return n;
}
