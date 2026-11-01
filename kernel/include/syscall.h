#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* 系统调用号 */
#define SYS_HEPU_INFO      0    /* 获取系统信息 */
#define SYS_HEPU_FP_VERIFY 1    /* 校验内核版权指纹 */
#define SYS_PRINT          2    /* 打印字符串 */
#define SYS_MALLOC         3    /* 内存分配 */
#define SYS_FREE           4    /* 内存释放 */
#define SYS_EXIT           5    /* 进程退出 */
#define SYS_YIELD          6    /* 让出CPU */
#define SYS_GETPID         7    /* 获取PID */
#define SYS_OPEN           8    /* 打开文件 */
#define SYS_READ           9    /* 读取文件 */
#define SYS_WRITE          10   /* 写入文件 */
#define SYS_CLOSE          11   /* 关闭文件 */
#define SYS_IPC_SEND       12   /* IPC发送 */
#define SYS_IPC_RECV       13   /* IPC接收 */
#define SYS_LSEEK          14   /* 文件定位 */
#define SYS_SLEEP          15   /* 睡眠(毫秒) */
#define SYS_PIPE           16   /* 创建管道 */
#define SYS_SHMGET         17   /* 获取共享内存 */
#define SYS_SHMAT          18   /* 附加共享内存 */
#define SYS_KILL           19   /* 发送信号 */
#define SYS_SIGNAL         20   /* 设置信号处理 */
/* === V0.37 新增系统调用 === */
#define SYS_TIME           21   /* 获取系统时间(秒) */
#define SYS_GETTIMEOFDAY   22   /* 获取精确时间(微秒) */
#define SYS_SETTIMEOFDAY   23   /* 设置系统时间 */
#define SYS_STAT           24   /* 文件状态 */
#define SYS_FSTAT          25   /* 文件描述符状态 */
#define SYS_ACCESS         26   /* 检查文件访问权限 */
#define SYS_UNLINK         27   /* 删除文件 */
#define SYS_RENAME         28   /* 重命名文件 */
#define SYS_MKDIR          29   /* 创建目录 */
#define SYS_RMDIR          30   /* 删除目录 */
#define SYS_READDIR        31   /* 读取目录 */
#define SYS_CHMOD          32   /* 修改文件权限 */
#define SYS_CHOWN          33   /* 修改文件所有者 */
#define SYS_MOUNT          34   /* 挂载文件系统 */
#define SYS_UMOUNT         35   /* 卸载文件系统 */
#define SYS_IOCTL          36   /* 设备控制 */
#define SYS_MMAP           37   /* 内存映射 */
#define SYS_MUNMAP         38   /* 取消内存映射 */
#define SYS_MPROTECT       39   /* 内存保护 */
#define SYS_CLONE          40   /* 创建线程/进程 */
#define SYS_WAITPID        41   /* 等待进程结束 */
#define SYS_EXECVE         42   /* 执行程序 */
#define SYS_FORK           43   /* 分叉进程 */
#define SYS_NANOSLEEP      44   /* 高精度睡眠(纳秒) */
#define SYS_POLL           45   /* 多路复用IO */
#define SYS_SELECT         46   /* select IO多路复用 */
#define SYS_SOCKET         47   /* 创建socket */
#define SYS_BIND           48   /* 绑定地址 */
#define SYS_LISTEN         49   /* 监听连接 */
#define SYS_ACCEPT         50   /* 接受连接 */
#define SYS_CONNECT        51   /* 连接服务器 */
#define SYS_SEND           52   /* 发送数据 */
#define SYS_RECV           53   /* 接收数据 */
#define SYS_GETCWD         54   /* 获取当前工作目录 */
#define SYS_CHDIR          55   /* 切换工作目录 */
#define SYS_DUP            56   /* 复制文件描述符 */
#define SYS_DUP2           57   /* 复制到指定fd */
#define SYS_FCNTL          58   /* 文件控制 */
#define SYS_GETUID         59   /* 获取用户ID */
#define SYS_GETEUID        60   /* 获取有效用户ID */
#define SYS_GETGID         61   /* 获取组ID */
#define SYS_GETEGID        62   /* 获取有效组ID */
#ifndef SYS_MAX
#define SYS_SHMDT          63   /* 释放共享内存 (V3.77: 补 shmget 回收路径) */
#define SYS_BRK            64   /* 设置/查询进程堆顶 (V3.87: Linux brk 兼容) */
#define SYS_GFX_INIT       200   /* 初始化VGA图形模式 (V4.02: gfxtest支持) */
#define SYS_GFX_TEXT       201   /* 切换回文本模式 (V4.02: gfxtest支持) */
#define SYS_GFX_FLIP       202   /* 提交后台缓冲区到显存 (双缓冲flip) */
#define SYS_GFX_DEMO       203   /* 内核图形演示 (展示所有绘图API) */
#define SYS_WM_DEMO        204   /* 窗口管理器演示 */
#define SYS_GAME_SNAKE     205   /* 贪吃蛇游戏 */
#define SYS_GAME_TETRIS    206   /* 俄罗斯方块游戏 */
#define SYS_GAME_MINESWEEPER 207 /* 扫雷游戏 */
#define SYS_SOCKETCALL      208   /* Linux socketcall多路复用 (V6.69: BSD Socket API) */
#define SYS_SERVICE_REGISTER 210 /* 注册用户态服务 (V6.64: 微内核服务发现) */
#define SYS_SERVICE_LOOKUP   211 /* 查找用户态服务 (V6.64: 微内核服务发现) */
#define SYS_SET_PASSWORD     212 /* 设置用户密码 (V8.46) */
#define SYS_VERIFY_PASSWORD  213 /* 验证用户密码 (V8.46) */
#define SYS_SWITCH_USER      214 /* 切换用户 (V8.46) */
#define SYS_CREATE_USER      215 /* 创建用户 (V8.46) */
#define SYS_REMOVE_USER      216 /* 删除用户 (V8.46) */
#define SYS_LOCK_USER        217 /* 锁定用户 (V8.46) */
#define SYS_UNLOCK_USER      218 /* 解锁用户 (V8.46) */
#define SYS_LIST_USERS       219 /* 列出用户 (V8.46) */
#define SYS_GET_CURRENT_USER 220 /* 获取当前用户 (V8.46) */
#define SYS_GFX_3D_DEMO      221 /* 3D图形演示 (V0.9.0: P5-5硬件加速) */
#define SYS_MAX              222
#endif

/* 初始化系统调用 */
void syscall_init(void);

/* 系统调用处理函数 */
uint32_t syscall_handler(uint32_t num, uint32_t arg1, uint32_t arg2,
                         uint32_t arg3, uint32_t arg4);

#endif /* SYSCALL_H */
