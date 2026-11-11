/* ============================================================================
 * lua64.c — 把 Lua 5.4 接进 64 位内核
 *
 * 只做最小的桥: 分配器(kheap)、输出(串口)、以及一个能跑脚本的入口。
 * Lua 的 io/os/package 库全部不编(它们依赖 fopen/system/dlfcn, 内核没有);
 * 保留 base/coroutine/table/string/math/utf8。
 *
 * ABI: 本文件必须与 Lua 一起用 -msse2 编译(见 Makefile 的 lua64.o 规则),
 * 否则 double 的返回/传参走 x87 与 SSE 两种约定, 混用会拿到垃圾值。
 * ========================================================================== */
#include <stdint.h>
#include <stdarg.h>

extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
extern int vga_printf(const char *fmt, ...);

/* Lua 的公共头 —— 只取类型与 API, 不用它的实现细节 */
#include "../../../third_party/lua/src/lua.h"
#include "../../../third_party/lua/src/lauxlib.h"
#include "../../../third_party/lua/src/lualib.h"

/* ---------------- 输出 ---------------- */
/* 直接吐到串口(COM1)。桌面起来后 fb 接管, 串口仍是最可靠的日志通道 */
extern int uart64_putc(int com, char c);
void lua64_write(const char *s, unsigned long len)
{
    for (unsigned long i = 0; i < len; i++) uart64_putc(0, s[i]);
}

/* ---------------- 分配器 ---------------- */
extern void *realloc(void *ptr, unsigned long new_size);  /* klibm64.c */

static void *lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud; (void)osize;
    if (nsize == 0) { if (ptr) kfree(ptr); return 0; }
    if (!ptr) return kmalloc((uint64_t)nsize);
    return realloc(ptr, (unsigned long)nsize);
}

/* ---------------- 库集合 ---------------- */
static const luaL_Reg lua64_libs[] = {
    {"_G",        luaopen_base},
    {"coroutine", luaopen_coroutine},
    {"table",     luaopen_table},
    {"string",    luaopen_string},
    {"math",      luaopen_math},
    {"utf8",      luaopen_utf8},
    {NULL, NULL}
};

static lua_State *lua64_newstate(void)
{
    lua_State *L = lua_newstate(lua_alloc, NULL);
    if (!L) return 0;
    const luaL_Reg *lib;
    for (lib = lua64_libs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    return L;
}

/* ---------------- 对外入口 ---------------- */
/* 自检: 跑一段固定的脚本, 证明解释器真的在工作 */
int lua64_selftest(void)
{
    static const char script[] =
        "print('hepu lua '.._VERSION)\n"
        "local s=0\n"
        "for i=1,100 do s=s+i end\n"
        "print('sum(1..100)='..s)\n"
        "print('sqrt2='..string.format('%.6f', math.sqrt(2)))\n"
        "local t={} for i=1,5 do t[i]=i*i end\n"
        "print('table='..table.concat(t,','))\n"
        "local acc=0.0\n"
        "for i=1,200000 do acc=acc+1.0/3.0 end\n"
        "print('acc='..string.format('%.4f',acc))\n"
        "return 'ok'\n";
    lua_State *L = lua64_newstate();
    if (!L) { vga_printf("[LUA64] no state\n"); return -1; }
    int rc = luaL_loadbuffer(L, script, sizeof(script) - 1, "selftest");
    if (rc != LUA_OK) {
        vga_printf("[LUA64] load error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return -1;
    }
    rc = lua_pcall(L, 0, 1, 0);
    if (rc != LUA_OK) {
        vga_printf("[LUA64] run error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return -1;
    }
    vga_printf("[LUA64] selftest result: %s\n", lua_tostring(L, -1));
    lua_close(L);
    return 0;
}
