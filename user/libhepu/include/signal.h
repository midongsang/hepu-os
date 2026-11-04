/* ============================================================================
 * 赫普OS - signal.h 头文件
 * 信号相关定义和函数声明
 * V8.20: 初始实现
 * ========================================================================== */

#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

/* 信号编号 */
#define SIGHUP      1   /* 挂起（控制终端关闭或父进程死亡） */
#define SIGINT      2   /* 中断（Ctrl+C） */
#define SIGQUIT     3   /* 退出（Ctrl+\） */
#define SIGILL      4   /* 非法指令 */
#define SIGTRAP     5   /* 跟踪陷阱（调试器） */
#define SIGABRT     6   /* 异常中止（abort()） */
#define SIGIOT      6   /* IOT陷阱（与SIGABRT相同） */
#define SIGBUS      7   /* 总线错误（内存访问错误） */
#define SIGFPE      8   /* 浮点异常 */
#define SIGKILL     9   /* 强制终止（不可捕获/忽略） */
#define SIGUSR1     10  /* 用户自定义信号1 */
#define SIGSEGV     11  /* 段错误（无效内存访问） */
#define SIGUSR2     12  /* 用户自定义信号2 */
#define SIGPIPE     13  /* 管道破裂（写入无读者的管道） */
#define SIGALRM     14  /* 闹钟（alarm()超时） */
#define SIGTERM     15  /* 终止（默认终止信号） */
#define SIGSTKFLT   16  /* 协处理器栈错误 */
#define SIGCHLD     17  /* 子进程状态改变 */
#define SIGCLD      17  /* 与SIGCHLD相同（System V） */
#define SIGCONT     18  /* 继续（如果停止） */
#define SIGSTOP     19  /* 停止（不可捕获/忽略） */
#define SIGTSTP     20  /* 终端停止（Ctrl+Z） */
#define SIGTTIN     21  /* 后台进程尝试读终端 */
#define SIGTTOU     22  /* 后台进程尝试写终端 */
#define SIGURG      23  /* 套接字紧急数据 */
#define SIGXCPU     24  /* 超过CPU时间限制 */
#define SIGXFSZ     25  /* 超过文件大小限制 */
#define SIGVTALRM   26  /* 虚拟闹钟（进程CPU时间） */
#define SIGPROF     27  /* 性能闹钟（CPU+系统时间） */
#define SIGWINCH    28  /* 窗口大小改变 */
#define SIGIO       29  /* I/O可用（异步I/O） */
#define SIGPOLL     29  /* 与SIGIO相同（System V） */
#define SIGPWR      30  /* 电源故障 */
#define SIGSYS      31  /* 非法系统调用 */
#define SIGUNUSED   31  /* 未使用（与SIGSYS相同） */

/* 实时信号范围 */
#define SIGRTMIN    32  /* 最小实时信号 */
#define SIGRTMAX    64  /* 最大实时信号 */

/* 信号处理函数指针类型 */
typedef void (*sighandler_t)(int);

/* 特殊信号处理值 */
#define SIG_DFL     ((sighandler_t)0)  /* 默认处理 */
#define SIG_IGN     ((sighandler_t)1)  /* 忽略信号 */
#define SIG_ERR     ((sighandler_t)-1) /* 错误返回 */

/* 信号集操作 */
#define _NSIG       64  /* 信号总数 */
#define _NSIG_BPW   32  /* 每个字的位数 */
#define _NSIG_WORDS (_NSIG / _NSIG_BPW)  /* 信号集字数 */

/* 信号集结构 */
typedef struct {
    unsigned long sig[_NSIG_WORDS];
} sigset_t;

/* 信号处理动作结构 */
struct sigaction {
    void     (*sa_handler)(int);  /* 信号处理函数 */
    void     (*sa_sigaction)(int, siginfo_t *, void *); /* 扩展信号处理函数 */
    sigset_t sa_mask;              /* 信号处理期间阻塞的信号集 */
    int      sa_flags;             /* 信号处理标志 */
    void     (*sa_restorer)(void); /* 恢复函数（内部使用） */
};

/* 信号信息结构 */
typedef struct {
    int      si_signo;     /* 信号编号 */
    int      si_code;      /* 信号代码 */
    int      si_errno;     /* 错误号（如果相关） */
    pid_t    si_pid;       /* 发送信号的进程ID */
    uid_t    si_uid;       /* 发送信号的用户ID */
    int      si_status;    /* 退出状态或信号（SIGCHLD） */
    void    *si_addr;      /* 导致信号的内存地址（SIGSEGV/SIGBUS） */
    long     si_band;      /* 带事件（SIGPOLL） */
    int      si_fd;        /* 文件描述符（SIGPOLL） */
    int      si_tid;       /* 线程ID */
    int      si_overrun;   /* 定时器超限计数 */
    int      si_timerid;   /* 定时器ID */
    void    *si_ptr;       /* 指针值（sigqueue） */
    int      si_int;       /* 整数值（sigqueue） */
    union {
        int   _pad[28];    /* 填充 */
    } _sifields;
} siginfo_t;

/* sigaction标志 */
#define SA_NOCLDSTOP  0x00000001  /* 子进程停止时不产生SIGCHLD */
#define SA_NOCLDWAIT  0x00000002  /* 不产生僵尸进程（子进程退出时自动回收） */
#define SA_SIGINFO    0x00000004  /* 使用sa_sigaction而不是sa_handler */
#define SA_RESTORER   0x04000000  /* 使用sa_restorer */
#define SA_ONSTACK    0x08000000  /* 在替代信号栈上执行 */
#define SA_RESTART    0x10000000  /* 重启被信号中断的系统调用 */
#define SA_NODEFER    0x40000000  /* 信号处理期间不阻塞该信号 */
#define SA_NOMASK     SA_NODEFER  /* 与SA_NODEFER相同（兼容） */
#define SA_ONESHOT    0x80000000  /* 一次性处理（处理后恢复默认） */
#define SA_RESETHAND  SA_ONESHOT  /* 与SA_ONESHOT相同（POSIX） */

/* sigprocmask标志 */
#define SIG_BLOCK     0  /* 阻塞信号（添加到当前阻塞集） */
#define SIG_UNBLOCK   1  /* 解除阻塞（从当前阻塞集移除） */
#define SIG_SETMASK   2  /* 设置阻塞集（替换当前阻塞集） */

/* 函数声明 */

/* 基本信号操作 */
sighandler_t signal(int signum, sighandler_t handler);
int raise(int sig);
int kill(pid_t pid, int sig);
int killpg(int pgrp, int sig);
int sigqueue(pid_t pid, int sig, const union sigval value);

/* 信号集操作 */
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);

/* 信号掩码操作 */
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigpending(sigset_t *set);
int sigsuspend(const sigset_t *mask);

/* 高级信号处理 */
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int sigwait(const sigset_t *set, int *sig);
int sigwaitinfo(const sigset_t *set, siginfo_t *info);
int sigtimedwait(const sigset_t *set, siginfo_t *info,
                  const struct timespec *timeout);

/* 信号栈 */
int sigaltstack(const stack_t *ss, stack_t *oss);

/* 信号名称 */
extern const char *const sys_siglist[];
extern const char *const sys_signame[];

/* 信号值联合 */
union sigval {
    int   sival_int;   /* 整数值 */
    void *sival_ptr;   /* 指针值 */
};

/* 栈结构（前向声明） */
typedef struct {
    void  *ss_sp;      /* 栈基地址 */
    int    ss_flags;   /* 栈标志 */
    size_t ss_size;    /* 栈大小 */
} stack_t;

/* 栈标志 */
#define SS_ONSTACK    1  /* 当前在替代信号栈上执行 */
#define SS_DISABLE    2  /* 禁用替代信号栈 */

/* 信号代码（si_code值） */
#define SI_USER       0     /* 由kill()发送 */
#define SI_QUEUE      -1    /* 由sigqueue()发送 */
#define SI_TIMER      -2    /* 定时器到期 */
#define SI_ASYNCIO    -3    /* 异步I/O完成 */
#define SI_MESGQ      -4    /* 消息队列消息到达 */
#define SI_TKILL      -6    /* 由tkill()发送 */
#define SI_DETHREAD   -7    /* 线程死亡发送 */

/* SIGILL代码 */
#define ILL_ILLOPC    1  /* 非法操作码 */
#define ILL_ILLOPN    2  /* 非法操作数 */
#define ILL_ILLADR    3  /* 非法寻址模式 */
#define ILL_ILLTRP    4  /* 非法陷阱 */
#define ILL_PRVOPC    5  /* 特权操作码 */
#define ILL_PRVREG    6  /* 特权寄存器 */
#define ILL_COPROC    7  /* 协处理器错误 */
#define ILL_BADSTK    8  /* 内部栈错误 */

/* SIGFPE代码 */
#define FPE_INTDIV    1  /* 整数除零 */
#define FPE_INTOVF    2  /* 整数溢出 */
#define FPE_FLTDIV    3  /* 浮点除零 */
#define FPE_FLTOVF    4  /* 浮点溢出 */
#define FPE_FLTUND    5  /* 浮点下溢 */
#define FPE_FLTRES    6  /* 浮点不精确 */
#define FPE_FLTINV    7  /* 浮点无效操作 */
#define FPE_FLTSUB    8  /* 下标越界 */

/* SIGSEGV代码 */
#define SEGV_MAPERR   1  /* 地址未映射 */
#define SEGV_ACCERR   2  /* 地址权限错误 */
#define SEGV_BNDERR   3  /* 边界错误 */
#define SEGV_PKUERR   4  /* 保护键错误 */

/* SIGBUS代码 */
#define BUS_ADRALN    1  /* 地址对齐错误 */
#define BUS_ADRERR    2  /* 地址不存在 */
#define BUS_OBJERR    3  /* 对象硬件错误 */
#define BUS_MCEERR_AR 4  /* 硬件内存错误（动作必需） */
#define BUS_MCEERR_AO 5  /* 硬件内存错误（动作可选） */

/* SIGCHLD代码 */
#define CLD_EXITED    1  /* 子进程正常退出 */
#define CLD_KILLED    2  /* 子进程被信号杀死 */
#define CLD_DUMPED    3  /* 子进程转储核心 */
#define CLD_TRAPPED   4  /* 子进程被跟踪陷阱 */
#define CLD_STOPPED   5  /* 子进程停止 */
#define CLD_CONTINUED 6  /* 子进程继续 */

/* SIGPOLL代码 */
#define POLL_IN       1  /* 数据可读 */
#define POLL_OUT      2  /* 数据可写 */
#define POLL_MSG      3  /* 输入消息可用 */
#define POLL_ERR      4  /* I/O错误 */
#define POLL_PRI      5  /* 高优先级数据可读 */
#define POLL_HUP      6  /* 设备断开 */

#endif /* _SIGNAL_H */
