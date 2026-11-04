/*
 * 赫普OS - GDB远程调试桩（gdbstub）实现
 * V6.84: 初始版本，支持基础GDB远程协议
 */

#include "gdbstub.h"
#include <stdint.h>
#include <string.h>

/* 串口基地址 */
static uint16_t g_serial_base = 0x3F8;
static gdbstub_state_t g_state = GDBSTUB_STATE_DISCONNECTED;

/* 寄存器上下文（在异常中断中保存） */
static uint32_t g_regs[16];  /* EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI, EIP, EFLAGS, CS, SS, DS, ES, FS, GS */
static int g_regs_valid = 0;

/* 缓冲区 */
#define GDB_BUF_SIZE 1024
static char g_rx_buf[GDB_BUF_SIZE];
static char g_tx_buf[GDB_BUF_SIZE];

/* 十六进制转换 */
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char hex_char(int v) {
    return "0123456789abcdef"[v & 0xF];
}

/* 串口读写 */
static void serial_putc(char c) {
    /* 等待发送缓冲区空 */
    while ((*(volatile uint8_t *)(g_serial_base + 5) & 0x20) == 0);
    *(volatile uint8_t *)g_serial_base = c;
}

static int serial_getc(void) {
    /* 检查是否有数据 */
    if ((*(volatile uint8_t *)(g_serial_base + 5) & 0x01) == 0)
        return -1;
    return *(volatile uint8_t *)g_serial_base;
}

/* 计算校验和 */
static uint8_t checksum(const char *data, int len) {
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) sum += (uint8_t)data[i];
    return sum;
}

/* 发送数据包 */
static void gdb_send_packet(const char *data) {
    int len = strlen(data);
    uint8_t cs = checksum(data, len);

    serial_putc('$');
    for (int i = 0; i < len; i++) serial_putc(data[i]);
    serial_putc('#');
    serial_putc(hex_char(cs >> 4));
    serial_putc(hex_char(cs & 0xF));
}

/* 接收数据包（返回数据长度，-1表示错误） */
static int gdb_recv_packet(char *buf, int max_len) {
    int c;
    int state = 0;  /* 0: wait $, 1: data, 2: checksum1, 3: checksum2 */
    int len = 0;
    uint8_t cs_recv = 0;
    uint8_t cs_calc = 0;

    while (1) {
        c = serial_getc();
        if (c < 0) continue;

        if (state == 0) {
            if (c == '$') {
                state = 1;
                len = 0;
                cs_calc = 0;
            } else if (c == '+') {
                /* ACK，忽略 */
            } else if (c == '-') {
                /* NAK，重传上次包 */
                return -2;
            }
        } else if (state == 1) {
            if (c == '#') {
                state = 2;
                buf[len] = 0;
            } else if (len < max_len - 1) {
                buf[len++] = c;
                cs_calc += c;
            }
        } else if (state == 2) {
            cs_recv = hex_val(c) << 4;
            state = 3;
        } else if (state == 3) {
            cs_recv |= hex_val(c);
            if (cs_recv == cs_calc) {
                serial_putc('+');  /* ACK */
                return len;
            } else {
                serial_putc('-');  /* NAK */
                state = 0;
            }
        }
    }
}

/* 读取内存为十六进制字符串 */
static int mem_to_hex(uint32_t addr, int length, char *out, int out_max) {
    int pos = 0;
    uint8_t *p = (uint8_t *)addr;
    for (int i = 0; i < length && pos < out_max - 2; i++) {
        out[pos++] = hex_char(p[i] >> 4);
        out[pos++] = hex_char(p[i] & 0xF);
    }
    out[pos] = 0;
    return pos;
}

/* 写入内存（从十六进制字符串） */
static int hex_to_mem(uint32_t addr, const char *hex, int length) {
    uint8_t *p = (uint8_t *)addr;
    for (int i = 0; i < length; i++) {
        int hi = hex_val(hex[i * 2]);
        int lo = hex_val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        p[i] = (hi << 4) | lo;
    }
    return 0;
}

/* 读取寄存器为十六进制字符串（GDB i386顺序） */
static int regs_to_hex(char *out, int out_max) {
    /* GDB i386寄存器顺序：EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI, EIP, EFLAGS, CS, SS, DS, ES, FS, GS */
    int pos = 0;
    for (int i = 0; i < 16 && pos < out_max - 8; i++) {
        uint32_t v = g_regs[i];
        for (int b = 0; b < 4; b++) {
            out[pos++] = hex_char((v >> (b * 8)) & 0xF);
            out[pos++] = hex_char((v >> (b * 8 + 4)) & 0xF);
        }
    }
    out[pos] = 0;
    return pos;
}

/* 处理GDB命令 */
static void gdb_handle_command(const char *cmd) {
    char *out = g_tx_buf;
    int out_len = 0;

    if (cmd[0] == '?') {
        /* 查询停止原因：SIGTRAP=5 */
        out[0] = 'S'; out[1] = '0'; out[2] = '5'; out[3] = 0;
    } else if (cmd[0] == 'g') {
        /* 读取寄存器 */
        if (g_regs_valid) {
            regs_to_hex(out, GDB_BUF_SIZE);
        } else {
            strcpy(out, "E00");  /* 错误 */
        }
    } else if (cmd[0] == 'G') {
        /* 写入寄存器（暂不支持） */
        strcpy(out, "OK");
    } else if (cmd[0] == 'm') {
        /* 读取内存：m addr,length */
        uint32_t addr = 0;
        int length = 0;
        const char *p = cmd + 1;
        while (*p && *p != ',') {
            int v = hex_val(*p);
            if (v >= 0) addr = addr * 16 + v;
            p++;
        }
        if (*p == ',') p++;
        while (*p) {
            int v = hex_val(*p);
            if (v >= 0) length = length * 16 + v;
            p++;
        }
        if (length > 0 && length < GDB_BUF_SIZE / 2) {
            mem_to_hex(addr, length, out, GDB_BUF_SIZE);
        } else {
            strcpy(out, "E00");
        }
    } else if (cmd[0] == 'M') {
        /* 写入内存：M addr,length:data */
        uint32_t addr = 0;
        int length = 0;
        const char *p = cmd + 1;
        while (*p && *p != ',') {
            int v = hex_val(*p);
            if (v >= 0) addr = addr * 16 + v;
            p++;
        }
        if (*p == ',') p++;
        while (*p && *p != ':') {
            int v = hex_val(*p);
            if (v >= 0) length = length * 16 + v;
            p++;
        }
        if (*p == ':') p++;
        if (hex_to_mem(addr, p, length) == 0) {
            strcpy(out, "OK");
        } else {
            strcpy(out, "E00");
        }
    } else if (cmd[0] == 'c') {
        /* 继续执行 */
        g_state = GDBSTUB_STATE_RUNNING;
        strcpy(out, "OK");
        return;  /* 不发送响应，直接返回执行 */
    } else if (cmd[0] == 's') {
        /* 单步执行 */
        g_state = GDBSTUB_STATE_RUNNING;
        strcpy(out, "OK");
        return;
    } else if (cmd[0] == 'Z' && cmd[1] == '0') {
        /* 插入软件断点：Z0 addr,kind */
        strcpy(out, "OK");  /* 暂不实际插入，返回OK */
    } else if (cmd[0] == 'z' && cmd[1] == '0') {
        /* 移除软件断点 */
        strcpy(out, "OK");
    } else if (cmd[0] == 'D') {
        /* 断开连接 */
        g_state = GDBSTUB_STATE_DISCONNECTED;
        strcpy(out, "OK");
    } else if (cmd[0] == 'k') {
        /* 杀死程序 */
        strcpy(out, "OK");
    } else {
        /* 不支持的命令，返回空 */
        out[0] = 0;
    }

    gdb_send_packet(out);
}

/* 初始化GDBstub */
void gdbstub_init(uint16_t serial_base) {
    g_serial_base = serial_base;
    g_state = GDBSTUB_STATE_DISCONNECTED;
    g_regs_valid = 0;

    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[GDBSTUB] initialized on serial 0x");
    {
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puthex(0x3F8, serial_base);
    }
    serial_puts(0x3F8, "\n");
}

/* 处理异常（在断点/异常中断中调用） */
void gdbstub_handle_exception(uint32_t exception_type, uint32_t *regs) {
    if (g_state == GDBSTUB_STATE_DISCONNECTED) {
        /* 首次异常，等待GDB连接 */
        g_state = GDBSTUB_STATE_CONNECTED;
        extern void serial_puts(uint16_t, const char *);
        serial_puts(0x3F8, "[GDBSTUB] waiting for GDB connection...\n");
    }

    /* 保存寄存器 */
    if (regs) {
        for (int i = 0; i < 16; i++) g_regs[i] = regs[i];
        g_regs_valid = 1;
    }

    g_state = GDBSTUB_STATE_STOPPED;

    /* 发送停止通知 */
    gdb_send_packet("S05");  /* SIGTRAP */

    /* 命令循环 */
    while (g_state == GDBSTUB_STATE_STOPPED) {
        int len = gdb_recv_packet(g_rx_buf, GDB_BUF_SIZE);
        if (len > 0) {
            gdb_handle_command(g_rx_buf);
        }
    }
}

/* 检查是否有GDB连接 */
int gdbstub_is_connected(void) {
    return g_state != GDBSTUB_STATE_DISCONNECTED;
}

/* 手动中断到GDB */
void gdbstub_break(void) {
    uint32_t dummy_regs[16] = {0};
    gdbstub_handle_exception(3, dummy_regs);  /* SIGTRAP */
}

/* 发送O包（控制台输出） */
void gdbstub_output(const char *msg) {
    if (g_state == GDBSTUB_STATE_DISCONNECTED) return;

    /* O包：O + 十六进制字符串 */
    int pos = 0;
    g_tx_buf[pos++] = 'O';
    for (const char *p = msg; *p && pos < GDB_BUF_SIZE - 3; p++) {
        g_tx_buf[pos++] = hex_char((*p) >> 4);
        g_tx_buf[pos++] = hex_char((*p) & 0xF);
    }
    g_tx_buf[pos] = 0;
    gdb_send_packet(g_tx_buf);
}
