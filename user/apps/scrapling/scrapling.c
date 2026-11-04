/* ============================================================================
 * HepuOS - scrapling: 极简网页抓取工具
 * 用法: scrapling <url>
 * 输出: 网页纯文本 + 所有链接
 * v0.20.29
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>

/* 系统调用号 (Hepu 原生) */
#define SYS_WRITE   1
#define SYS_SOCKET  40
#define SYS_CONNECT  41
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

static long sc6(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ volatile("syscall" : "=a"(ret)
        : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
        : "rcx","r11","memory");
    return ret;
}

static void p(const char *s) { long n=0; while(s[n])n++; sc6(SYS_WRITE,1,(long)s,n,0,0,0); }
static void pi(long v) {
    char b[16]; int i=0;
    if(v==0){p("0");return;}
    while(v>0){b[i++]='0'+v%10;v/=10;}
    for(int j=0;j<i/2;j++){char t=b[j];b[j]=b[i-1-j];b[i-1-j]=t;}
    b[i]=0; p(b);
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

/* 从 URL 提取 host/path */
static void parse_url(const char *url, char *host, int *port, char *path) {
    const char *p = url;
    if (p[0]=='h'&&p[1]=='t'&&p[2]=='t'&&p[3]=='p'&&p[4]==':'&&p[5]=='/') p += 7;
    int hlen=0;
    while(*p && *p!=':' && *p!='/' && hlen<63) host[hlen++]=*p++;
    host[hlen]=0;
    *port=80;
    if(*p==':'){p++;*port=0;while(*p>='0'&&*p<='9'){*port=*port*10+(*p-'0');p++;}}
    if(*p=='/'){int i=0;while(*p)path[i++]=*p++;path[i]=0;}
    else { path[0]='/'; path[1]=0; }
}

/* 提取 <a href="..."> 链接 */
static int extract_links(const char *html, int len) {
    int count = 0;
    p("\n--- LINKS ---\n");
    for (int i = 0; i < len-8; i++) {
        if (html[i]=='<' && html[i+1]=='a' && (html[i+2]==' '||html[i+2]=='h')) {
            for (int j = i; j < len-6; j++) {
                if (html[j]=='h' && html[j+1]=='r' && html[j+2]=='e' && html[j+3]=='f') {
                    int k = j+4;
                    while(k<len && (html[k]==' '||html[k]=='=')) k++;
                    if (k<len && (html[k]=='\"'||html[k]=='\'')) k++;
                    int start = k;
                    while(k<len && html[k]!='\"' && html[k]!='\'' && html[k]!='>' && html[k]!=' ') k++;
                    if (k>start) {
                        p("  -> ");
                        for(int m=start;m<k;m++){char c[2]={html[m],0};p(c);}
                        p("\n");
                        count++;
                    }
                    break;
                }
            }
        }
    }
    return count;
}

/* 过滤 HTML, 输出纯文本 */
static void render_text(const char *data, int len) {
    int in_tag=0, in_script=0, prev_sp=0;
    for (int i=0;i<len;i++) {
        char c=data[i];
        if (in_script) {
            if (data[i]=='<' && data[i+1]=='/' && data[i+2]=='s' && data[i+3]=='c' && data[i+4]=='r' && data[i+5]=='i' && data[i+6]=='p' && data[i+7]=='t') { in_script=0; i+=7; }
            continue;
        }
        if (in_tag) {
            if (c=='>') {
                in_tag=0;
                if (data[i-2]=='p'||data[i-2]=='h'||data[i-2]=='l') { if(!prev_sp){p("\n");prev_sp=1;} }
            }
            continue;
        }
        if (c=='<') {
            in_tag=1;
            if (data[i]=='<'&&data[i+1]=='s'&&data[i+2]=='c'&&data[i+3]=='r'&&data[i+4]=='i'&&data[i+5]=='p'&&data[i+6]=='t') in_script=1;
            continue;
        }
        if (c=='\n'||c=='\r'||c=='\t') c=' ';
        if (c==' ') { if(!prev_sp){p(" ");prev_sp=1;} continue; }
        prev_sp=0;
        char buf[2]={c,0}; p(buf);
    }
}

void _start(void) {
    p("=== HepuOS Scrapling v0.1 ===\n");
    /* 没 argc/argv 支持, 用默认地址 */
    char host[64], path[256];
    int port;
    /* 从命令行参数读 URL - 简化为默认 1.1.1.1 */
    for (long i = 1; i < 4; i++) {
        /* 尝试读 argv - 暂时硬编码 */
    }
    parse_url("http://1.1.1.1/", host, &port, path);

    p("Fetch: "); p(host); p(path); p("\n");

    uint32_t ip = parse_ip(host);
    long sock = sc6(SYS_SOCKET, 2, 1, 0, 0,0,0);
    if (sock < 0) { p("socket fail\n"); sc6(60,1,0,0,0,0,0); }

    struct sockaddr_in addr;
    for (int i=0;i<(int)sizeof(addr);i++) ((char*)&addr)[i]=0;
    addr.sin_family=2;
    addr.sin_port=(port>>8)|(port<<8);
    addr.sin_addr=ip;

    if (sc6(SYS_CONNECT, sock, (long)&addr, sizeof(addr),0,0,0)<0) {
        p("connect fail\n"); sc6(60,1,0,0,0,0,0);
    }
    sc6(SYS_SLEEP, 300,0,0,0,0,0);

    char req[256];
    int rl=0;
    const char *t1="GET "; while(*t1) req[rl++]=*t1++;
    const char *pp=path; while(*pp) req[rl++]=*pp++;
    const char *t2=" HTTP/1.0\r\nHost: "; while(*t2) req[rl++]=*t2++;
    const char *hh=host; while(*hh) req[rl++]=*hh++;
    const char *t3="\r\nUser-Agent: HepuScrapling/0.1\r\nConnection: close\r\n\r\n";
    while(*t3) req[rl++]=*t3++;
    req[rl]=0;

    sc6(SYS_SEND, sock, (long)req, rl, 0,0,0);
    sc6(SYS_SLEEP, 500,0,0,0,0,0);

    static char resp[8192];
    int total=0;
    for (int i=0;i<20;i++) {
        long n=sc6(SYS_RECV, sock, (long)resp+total, sizeof(resp)-total-1,0,0,0);
        if(n<=0) break;
        total+=n;
        resp[total]=0;
        sc6(SYS_SLEEP, 100,0,0,0,0,0);
    }
    sc6(SYS_CLOSE, sock,0,0,0,0,0);

    /* 找 body */
    char *body=resp;
    for(int i=0;i<total-3;i++){
        if(resp[i]=='\r'&&resp[i+1]=='\n'&&resp[i+2]=='\r'&&resp[i+3]=='\n'){body=resp+i+4;break;}
    }
    int blen = total - (int)(body-resp);

    p("\n--- CONTENT ---\n");
    render_text(body, blen);

    int nlinks = extract_links(body, blen);
    p("\nTotal: "); pi(total); p(" bytes, "); pi(nlinks); p(" links\n");

    sc6(60,0,0,0,0,0,0);
}
