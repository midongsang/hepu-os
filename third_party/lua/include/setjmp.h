#ifndef LUA_SETJMP_H
#define LUA_SETJMP_H
/* 只保存 callee-saved 寄存器 + rsp + rip, 共 8 个 8 字节槽 */
typedef unsigned long jmp_buf[8];
int setjmp(jmp_buf env);
/* longjmp 必须标 noreturn: 不标的话 Lua 的 luaD_throw(noreturn) 会生成
 * "函数返回"的兜底路径(ldo.c 报 'noreturn function does return'), 错误
 * 抛出后可能跑到未定义位置。 */
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#endif
