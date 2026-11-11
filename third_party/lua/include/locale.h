#ifndef LUA_LOCALE_H
#define LUA_LOCALE_H
struct lconv { char *decimal_point; };
char *setlocale(int, const char *);
struct lconv *localeconv(void);
#define LC_ALL 6
#define LC_NUMERIC 1
#endif
