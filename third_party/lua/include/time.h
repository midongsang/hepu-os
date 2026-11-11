#ifndef LUA_TIME_H
#define LUA_TIME_H
typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000000L
clock_t clock(void);
time_t time(time_t *t);
#endif
