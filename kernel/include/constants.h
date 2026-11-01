#ifndef CONSTANTS_H
#define CONSTANTS_H

/* ============================================================================
 * 赫普OS - 统一常量定义
 * 将分散在各模块中的魔法数字集中管理
 * ========================================================================== */

/* === 内存地址 === */
#define KERNEL_LOAD_ADDR    0x100000    /* 内核加载地址 1MB */
#define USER_STACK_ADDR     0x40000000  /* 用户栈虚拟地址 */
#define USER_TEXT_ADDR      0x20000000  /* V3.86: 用户代码加载基址 512MB
                                         * (.hug 按此基址链接, 静态数据/
                                         * 字符串绝对寻址正确). 必须高于
                                         * 物理内存上限(256MB): 原 128MB
                                         * 与 pmm 分配的用户页目录物理帧
                                         * (0x8000000 区)撞车, 自映射混乱 */
#define KERNEL_HEAP_ADDR    0x200000    /* 内核堆起始地址 */
#define PAGE_SIZE           4096        /* 页大小 */
#ifndef PAGE_MASK
#define PAGE_MASK           0xFFFFF000  /* 页掩码 */
#endif

/* === 段选择子 === */
#define GDT_KERNEL_CODE     0x08        /* 内核代码段 */
#define IDT_BASE_LOW_MASK      0xFFFF      /* IDT基地址低16位掩码 */
#define IDT_BASE_HIGH_MASK     0xFFFF      /* IDT基地址高16位掩码 */
#define IDT_FLAGS_INT_GATE     0x8E        /* 中断门: P=1,DPL=0,32位 */
#define IDT_FLAGS_SYSCALL_GATE 0xEE        /* 系统调用门: P=1,DPL=3,32位 */
#define IDT_SYSCALL_VECTOR     0x80        /* 系统调用中断号 */
#define GDT_BASE_LOW_MASK      0xFFFF      /* GDT基地址低16位掩码 */
#define GDT_BASE_MID_MASK      0xFF        /* GDT基地址中8位掩码 */
#define GDT_BASE_HIGH_MASK     0xFF        /* GDT基地址高8位掩码 */
#define GDT_LIMIT_LOW_MASK     0xFFFF      /* GDT段限长低16位掩码 */
#define GDT_LIMIT_HIGH_MASK    0x0F        /* GDT段限长高4位掩码 */
#define GDT_GRANULARITY_MASK   0xF0        /* GDT粒度高4位掩码 */
#define GDT_MAX_LIMIT          0xFFFFFFFF  /* GDT最大段限长 */

/* PS/2控制器端口 */
#define PS2_DATA_PORT         0x60   /* PS/2数据端口 */
#define PS2_CMD_PORT          0x64   /* PS/2命令端口 */

/* PS/2控制器命令 */
#define PS2_CMD_SET_LEDS      0xED   /* 设置键盘LED */
#define PS2_CMD_DISABLE_KB    0xAD   /* 禁用键盘 */
#define PS2_CMD_DISABLE_MOUSE 0xA7   /* 禁用鼠标 */
#define PS2_CMD_READ_CONFIG   0x20   /* 读取控制器配置 */
#define PS2_CMD_WRITE_CONFIG  0x60   /* 写入控制器配置 */
#define PS2_CMD_ENABLE_KB     0xAE   /* 启用键盘 */
#define PS2_CMD_ENABLE_MOUSE  0xA8   /* 启用鼠标 */
#define PS2_CMD_RESET         0xFF   /* 复位设备 */
#define PS2_CMD_WRITE_MOUSE   0xD4   /* 写入鼠标 */

/* PS/2配置位 */
#define PS2_CONFIG_INT_ENABLE 0x01   /* 启用中断 */
#define PS2_CONFIG_MOUSE_CLK  0x10   /* 鼠标时钟 */

/* 分区类型 */
#define PARTITION_TYPE_EFI    0xEF   /* EFI系统分区 */
#define PARTITION_TYPE_LINUX  0x83   /* Linux分区 */
#define GDT_KERNEL_DATA     0x10        /* 内核数据段 */
#define GDT_USER_CODE       0x1B        /* 用户代码段 (RPL=3) */
#define GDT_USER_DATA       0x23        /* 用户数据段 (RPL=3) */

/* === 中断/异常 === */
#define IRQ_BASE            32          /* IRQ起始向量号 */
#define IRQ_TIMER           0           /* 时钟中断 */
#define IRQ_KEYBOARD        1           /* 键盘中断 */
#define IRQ_MOUSE           12          /* 鼠标中断 */
#define EFLAGS_IF           0x200       /* 中断允许标志 */
#define EFLAGS_IOPL_MASK    0x3000      /* IOPL掩码 */

/* === Multiboot === */
#define MULTIBOOT_MAGIC     0x1BADB002  /* Multiboot魔数 */
#define MULTIBOOT_FLAGS     0x00000003  /* Multiboot标志 */

/* === .hug应用格式 === */
#define HUG_MAGIC_KERNEL    0x00475548  /* "HUG\0" 内核加载器格式 */
#define HUG_MAGIC_RELEASE   0x31475548  /* "HUG1" 发布格式 */
#define HUG_HEADER_SIZE     64          /* .hug头部大小 */

/* === 进程 === */
#define MAX_PROCESSES       256         /* V6.77: 最大进程数(原128) */
#define PROC_NAME_LEN       32          /* 进程名长度 */
#define KERNEL_STACK_SIZE   16384       /* 内核栈大小 16KB */
#define KERNEL_STACK_CANARY 0xC0FFEE11u /* V3.89-E34: 栈底哨兵, 溢出检测 */
#define USER_STACK_SIZE     65536       /* 用户栈大小 64KB */
#define DEFAULT_TIME_SLICE  10          /* 默认时间片 */

/* === VGA === */
#define VGA_WIDTH           80          /* VGA文本宽度 */
#define VGA_HEIGHT          25          /* VGA文本高度 */
#define VGA_MEMORY          0xB8000     /* VGA显存地址 */

/* === 串口 === */
#define COM1_PORT           0x3F8       /* COM1端口 */

/* === 系统调用 === */
#define SYS_MAX             222          /* 最大系统调用号 (V0.9.0: +3D演示) */

/* === 网络 === */
#define MAX_SOCKETS         128         /* V6.77: 最大socket数(原64) */
#define SOCKET_BUF_SIZE     16384       /* V6.77: socket缓冲区(原8192) */
#define AF_INET             2           /* IPv4地址族 */
#define AF_UNIX             1           /* Unix域地址族 */
#define SOCK_STREAM         1           /* TCP流套接字 */
#define SOCK_DGRAM          2           /* UDP数据报套接字 */
#define IPPROTO_TCP         6           /* TCP协议号 */
#define IPPROTO_UDP         17          /* UDP协议号 */
#define LOOPBACK_ADDR       0x0100007F  /* 127.0.0.1 */

/* === 文件系统 === */
#define MAX_FDS             256         /* V6.77: 最大文件描述符数(原128) */
#define MAX_PATH_LEN        256         /* 最大路径长度 */
#define MAX_FILENAME_LEN    255         /* 最大文件名长度 */

/* === 驱动 === */
#define MAX_DRIVERS         256         /* V6.77: 最大驱动数(原128) */

#endif /* CONSTANTS_H */

/* === GDT段描述符 === */
#define GDT_ACCESS_KERNEL_CODE  0x9A  /* 存在、内核级、代码段、可执行、可读 */
#define GDT_ACCESS_KERNEL_DATA  0x92  /* 存在、内核级、数据段、可写 */
#define GDT_ACCESS_USER_CODE    0xFA  /* 存在、用户级、代码段、可执行、可读 */
#define GDT_ACCESS_USER_DATA    0xF2  /* 存在、用户级、数据段、可写 */
#define GDT_GRANULARITY_32BIT   0xCF  /* 4KB粒度、32位保护模式 */

/* === UI颜色 === */
#define COLOR_BLACK      0x000000
#define COLOR_WHITE      0xFFFFFF
#define COLOR_RED        0xFF0000
#define COLOR_GREEN      0x00FF00
#define COLOR_BLUE       0x0000FF
#define COLOR_HEPU_BLUE  0x1A365D
#define COLOR_EVO_GREEN  0x00C9A7
#define COLOR_DARK_GRAY  0x1A1A2E
#define COLOR_MID_GRAY   0x6B7280
#define COLOR_LIGHT_GRAY 0xF5F7FA
#define COLOR_TRANSLUCENT 0x80000000

/* === 引导与内存 === */
#define BOOT_KERNEL_LOAD_ADDR   0x100000    /* 内核加载地址1MB */
#define BOOT_INITRD_ADDR        0x1000000   /* initrd加载地址16MB */
#define BOOT_MICROCODE_ADDR     0x2000000   /* 微码加载地址32MB */
#define BOOT_MMAP_ADDR          0x1000      /* 内存映射地址 */
#define BOOT_MULTIBOOT_FLAGS    0x00000FFF  /* Multiboot信息标志 */
#define BOOT_DEVICE_HD0         0x80010000  /* 第一硬盘 */
#define MEM_LOW_TOP             0x9FC00     /* 低内存顶部639KB */
#define MEM_HIGH_BASE           0x100000    /* 高内存基址1MB */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002  /* Multiboot引导魔数 */
#define MEM_MAP_HIGH_LENGTH       0x7FF00000   /* 高内存长度2GB-1MB */
#define MEM_MAP_EXTENDED_BASE     0x80000000   /* 扩展内存基址2GB */
#define MEM_MAP_EXTENDED_LENGTH   0x10000000   /* 扩展内存长度256MB */

/* === VGA显示 === */
#define VGA_TEXT_FRAMEBUFFER  0xB8000     /* VGA文本模式帧缓冲区 */
#define VGA_GFX_FRAMEBUFFER   0xA0000     /* VGA图形模式帧缓冲区 */
#define VGA_TEXT_WIDTH        80          /* VGA文本模式宽度 */
#define VGA_TEXT_HEIGHT       25          /* VGA文本模式高度 */
#define VGA_ATTR_LIGHT_GREY   0x0700      /* VGA属性字节：浅灰前景 */

/* === V3.87 多格式兼容层开关 ===
 * HEPU_CONFIG_COMPAT: 1=启用(默认), 0=关闭.
 * 关闭时: syscall 分发不翻译、binfmt 不初始化——
 * 内核编译产物与纯 HEPU 原生系统完全一致(零行为变化).
 * 兼容层全部代码在 kernel/compat/, 不修改任何原生内核路径. */
#define HEPU_CONFIG_COMPAT  1

/* V3.91-E37: kheap 空闲水位——低于此值拒绝新建用户进程(防系统级OOM) */
#define KHEAP_LOW_WATER (512U * 1024U)
