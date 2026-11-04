/* Hepu OS - hep task monitor (ps) */
#include <stdint.h>

struct hep_ps_entry {
    int pid;
    int ppid;
    int state;
    int priority;
    uint64_t mem_pages;
    char name[32];
};

static inline uint64_t syscall2(uint64_t nr, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    asm volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

static inline void write(int fd, const char *s, int n) {
    asm volatile("syscall" : : "a"(1), "D"(fd), "S"(s), "d"(n) : "rcx", "r11", "memory");
}

static void puts(const char *s) { int n=0; while(s[n])n++; write(1,s,n); }
static void putch(char c) { write(1,&c,1); }
static void pad(int n, int w) { while(n<w){putch(' ');n++;} }

int main(void) {
    struct hep_ps_entry e[8];
    int n = (int)syscall2(200, (uint64_t)e, 8);
    puts("PID  PPID  PRI  STATE    NAME\n");
    puts("---  ----  ---  -------  ----------------\n");
    for (int i=0;i<n;i++) {
        pad(e[i].pid,3); putch(' ');
        pad(e[i].ppid,4); putch(' ');
        pad(e[i].priority,3); putch(' ');
        const char *s="?";
        if(e[i].state==1) s="READY";
        if(e[i].state==2) s="RUN  ";
        if(e[i].state==4) s="ZOMB ";
        puts(s); putch(' '); putch(' ');
        puts(e[i].name); putch('\n');
    }
    return 0;
}
