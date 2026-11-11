#ifndef LUA_STDIO_H
#define LUA_STDIO_H
#include <stdarg.h>
#include <stddef.h>
#define BUFSIZ 8192
#define EOF (-1)
typedef struct _lua_file FILE;
extern FILE *stdin, *stdout, *stderr;
int  vsnprintf(char *, size_t, const char *, va_list);
int  snprintf(char *, size_t, const char *, ...);
int  printf(const char *, ...);
int  fprintf(FILE *, const char *, ...);
int  fflush(FILE *);
int  fputs(const char *, FILE *);
FILE *fopen(const char *, const char *);
FILE *freopen(const char *, const char *, FILE *);
int  fclose(FILE *);
int  feof(FILE *);
int  ferror(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
int  getc(FILE *);
#endif
