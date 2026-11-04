/* ============================================================================
 * 赫普OS - 简易浏览器 v0.1 (64位)
 * HTTP GET + HTML标签过滤 + 纯文本渲染
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>

/* 64位系统调用 */
static inline long syscall6(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
        : "rcx","r11","memory");
    return ret;
}

/* Hepu syscall numbers */
#define SYS_WRITE   1
#define SYS_SOCKET  40
#define SYS_CONNECT 41
#define SYS_SEND    45
#define SYS_RECV    46
#define SYS_CLOSE   4
#define SYS_SLEEP   14

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t  sin_zero[8];
};

static void p(const char *s) {
    long n=0; while(s[n]) n++;
    syscall6(SYS_WRITE, 1, (long)s, n, 0,0,0);
}

static uint32_t parse_ip(const char *s) {
    uint32_t ip=0; int sh=24, v=0;
    while(*s && sh>=0) {
        if(*s>='0'&&*s<='9') v=v*10+(*s-'0');
        else if(*s=='.'){ip|=(v&0xFF)<<sh;sh-=8;v=0;}
        s++;
    }
    ip|=(v&0xFF)<<sh;
    return ip;
}

static int my_strlen(const char *s){int n=0;while(s[n])n++;return n;}
static void my_strcpy(char *d, const char *s){while((*d++=*s++));}
static int my_strncmp(const char *a, const char *b, int n){
    while(n--){if(*a!=*b)return *a-*b;a++;b++;}return 0;
}

/* 从URL提取 host/port/path */
static void parse_url(const char *url, char *host, int *port, char *path) {
    const char *p = url;
    if (my_strncmp(p, "http://", 7) == 0) p += 7;
    int hlen = 0;
    while (*p && *p != ':' && *p != '/' && hlen < 63) host[hlen++] = *p++;
    host[hlen] = 0;
    *port = 80;
    if (*p == ':') {
        p++; *port = 0;
        while (*p >= '0' && *p <= '9') { *port = *port*10 + (*p-'0'); p++; }
    }
    if (*p == '/') my_strcpy(path, p);
    else my_strcpy(path, "/");
}

/* 简单拼接请求 */
static int build_req(char *req, const char *path, const char *host) {
    const char *tpl = "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: HepuBrowser/0.1\r\nConnection: close\r\n\r\n";
    int n = 0;
    const char *p;
    p = "GET "; while(*p) req[n++]=*p++;
    p = path; while(*p) req[n++]=*p++;
    p = " HTTP/1.0\r\nHost: "; while(*p) req[n++]=*p++;
    p = host; while(*p) req[n++]=*p++;
    p = "\r\nUser-Agent: HepuBrowser/0.1\r\nConnection: close\r\n\r\n";
    while(*p) req[n++]=*p++;
    req[n]=0;
    return n;
}

/* HTML过滤: 跳过标签, 输出纯文本 */
static void render_text(const char *data, int len) {
    int in_tag = 0, in_script = 0, prev_space = 0;
    for (int i = 0; i < len; i++) {
        char c = data[i];
        if (in_script) {
            if (my_strncmp(data+i, "</script>", 9) == 0) { in_script = 0; i += 8; }
            continue;
        }
        if (in_tag) {
            if (c == '>') {
                in_tag = 0;
                if (my_strncmp(data+i-3, "li>", 3) == 0 ||
                    my_strncmp(data+i-4, "p>", 2) == 0) { p("\n"); prev_space = 1; }
            }
            continue;
        }
        if (c == '<') {
            in_tag = 1;
            if (my_strncmp(data+i, "<script", 7) == 0) in_script = 1;
            continue;
        }
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        if (c == ' ') {
            if (!prev_space) { p(" "); prev_space = 1; }
            continue;
        }
        prev_space = 0;
        char buf[2] = {c, 0};
        p(buf);
    }
    p("\n");
}

void _start(void) {
    p("=== HepuOS Browser v0.1 ===\n");
    /* 没有命令行参数支持, 直接访问默认地址 */
    char host[64], path[256];
    int port;
    /* 默认: 访问 1.1.1.1 */
    my_strcpy(host, "1.1.1.1");
    my_strcpy(path, "/");
    port = 80;

    p("Host: "); p(host); p("\n");
    p("Path: "); p(path); p("\n");

    uint32_t server_ip = parse_ip(host);
    long sock = syscall6(SYS_SOCKET, 2, 1, 0, 0,0,0);
    if (sock < 0) { p("socket failed\n"); syscall6(60, 1,0,0,0,0,0); }

    struct sockaddr_in addr;
    for (int i=0;i<(int)sizeof(addr);i++) ((char*)&addr)[i]=0;
    addr.sin_family = 2;
    addr.sin_port = (port >> 8) | (port << 8);
    addr.sin_addr = server_ip;

    p("Connecting...\n");
    long rc = syscall6(SYS_CONNECT, sock, (long)&addr, sizeof(addr), 0,0,0);
    if (rc < 0) { p("connect failed\n"); syscall6(60, 1,0,0,0,0,0); }

    syscall6(SYS_SLEEP, 300,0,0,0,0,0);

    char req[512];
    int rl = build_req(req, path, host);
    p("Fetching...\n\n");
    syscall6(SYS_SEND, sock, (long)req, rl, 0,0,0);
    syscall6(SYS_SLEEP, 500,0,0,0,0,0);

    char resp[4096];
    int total = 0;
    for (int i = 0; i < 20; i++) {
        long n = syscall6(SYS_RECV, sock, (long)resp + total, sizeof(resp)-total-1, 0,0,0);
        if (n <= 0) break;
        total += n;
        resp[total] = 0;
        syscall6(SYS_SLEEP, 100,0,0,0,0,0);
    }
    syscall6(SYS_CLOSE, sock, 0,0,0,0,0);

    /* 找body */
    char *body = resp;
    for (int i = 0; i < total-3; i++) {
        if (resp[i]=='\r'&&resp[i+1]=='\n'&&resp[i+2]=='\r'&&resp[i+3]=='\n') {
            body = resp + i + 4; break;
        }
    }

    p("--- Page ---\n");
    render_text(body, total - (int)(body - resp));
    p("--- End ---\n");

    syscall6(60, 0,0,0,0,0,0);
}
