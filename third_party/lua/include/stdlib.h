#ifndef LUA_STDLIB_H
#define LUA_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void  free(void *);
double strtod(const char *, char **);
int   abs(int);
/* abort/exit 必须标 noreturn: Lua 的 luaD_throw 以 abort() 收尾, 不标会让
 * 编译器生成"函数返回"的路径(ldo.c 报 'noreturn function does return')。 */
void  exit(int) __attribute__((noreturn));
void  abort(void) __attribute__((noreturn));
char *getenv(const char *);
#endif
