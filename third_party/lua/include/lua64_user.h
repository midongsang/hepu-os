/* 用 -DLUA_USER_H 注入: 把 Lua 的输出重定向到内核串口, 并去掉它自带的
 * fprintf(stdout,...) 依赖(内核没有 FILE 体系)。 */
#ifndef LUA64_USER_H
#define LUA64_USER_H
void lua64_write(const char *s, unsigned long len);
#undef lua_writestring
#undef lua_writeline
#define lua_writestring(s,l) lua64_write((const char *)(s), (unsigned long)(l))
#define lua_writeline()      lua64_write("\n", 1)
#endif
