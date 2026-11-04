/* Hepu OS - pipe test */
#include <stdint.h>

static inline uint64_t syscall3(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    asm volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static inline void write(int fd, const char *s, int n) {
    asm volatile("syscall" : : "a"(1), "D"(fd), "S"(s), "d"(n) : "rcx", "r11", "memory");
}
static void puts(const char *s) { int n=0; while(s[n])n++; write(1,s,n); }

int main(void) {
    int fds[2];
    int r = (int)syscall3(42, (uint64_t)fds, 0, 0); /* pipe */
    if (r < 0) { puts("PIPE: FAIL\n"); return 1; }
    puts("PIPE: OK\n");
    return 0;
}
