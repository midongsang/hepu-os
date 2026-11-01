/* ============================================================================
 * 赫普OS - GDB远程调试Stub实现
 * V8.35: 初始实现
 * ========================================================================== */

#include "gdb_stub.h"
#include "../include/vga.h"

/* 全局状态 */
static gdb_state_t gdb_state;
static uint16_t gdb_com_port = 0x3F8;  /* COM1 */

/* 内存操作 */
extern void *memcpy(void *dst, const void *src, uint32_t n);
extern void *memset(void *dst, int val, uint32_t n);

/* 十六进制字符 */
static const char hex_chars[] = "0123456789abcdef";

/* 计算校验和 */
uint8_t gdb_stub_checksum(const char *buffer, int len)
{
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += (uint8_t)buffer[i];
    }
    return sum;
}

/* 十六进制转整数 */
int gdb_stub_hex_to_int(const char *hex, int len)
{
    int val = 0;
    for (int i = 0; i < len; i++) {
        char c = hex[i];
        val <<= 4;
        if (c >= '0' && c <= '9') val |= c - '0';
        else if (c >= 'a' && c <= 'f') val |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val |= c - 'A' + 10;
    }
    return val;
}

/* 整数转十六进制 */
void gdb_stub_int_to_hex(uint32_t val, char *hex, int len)
{
    for (int i = len - 1; i >= 0; i--) {
        hex[i] = hex_chars[val & 0xF];
        val >>= 4;
    }
}

/* 发送字符 */
void gdb_stub_putchar(char c)
{
    extern void serial_putc(uint16_t port, char c);
    serial_putc(gdb_com_port, c);
}

/* 接收字符 */
char gdb_stub_getchar(void)
{
    extern char serial_getc(uint16_t port);
    return serial_getc(gdb_com_port);
}

/* 发送ACK */
void gdb_stub_send_ack(void)
{
    gdb_stub_putchar('+');
}

/* 发送NACK */
void gdb_stub_send_nack(void)
{
    gdb_stub_putchar('-');
}

/* 发送数据包 */
void gdb_stub_putpacket(const char *buffer)
{
    int len = 0;
    while (buffer[len]) len++;

    uint8_t checksum = gdb_stub_checksum(buffer, len);

    gdb_stub_putchar('$');
    for (int i = 0; i < len; i++) {
        gdb_stub_putchar(buffer[i]);
    }
    gdb_stub_putchar('#');
    gdb_stub_putchar(hex_chars[(checksum >> 4) & 0xF]);
    gdb_stub_putchar(hex_chars[checksum & 0xF]);
}

/* 接收数据包 */
int gdb_stub_getpacket(char *buffer, int max_len)
{
    int count = 0;
    char c;

    /* 等待数据包开始 */
    while (1) {
        c = gdb_stub_getchar();
        if (c == '$') break;
        if (c == '-') {
            /* NACK，重发上一个包 */
            return -1;
        }
    }

    /* 读取数据包内容 */
    while (count < max_len - 1) {
        c = gdb_stub_getchar();
        if (c == '#') break;
        buffer[count++] = c;
    }
    buffer[count] = 0;

    /* 读取校验和 */
    char checksum_str[3];
    checksum_str[0] = gdb_stub_getchar();
    checksum_str[1] = gdb_stub_getchar();
    checksum_str[2] = 0;
    uint8_t received_checksum = gdb_stub_hex_to_int(checksum_str, 2);

    /* 验证校验和 */
    uint8_t calculated_checksum = gdb_stub_checksum(buffer, count);
    if (received_checksum != calculated_checksum) {
        gdb_stub_send_nack();
        return -1;
    }

    gdb_stub_send_ack();
    return count;
}

/* 读取寄存器 */
void gdb_stub_read_registers(void)
{
    char response[GDB_PACKET_MAX];
    int pos = 0;

    for (int i = 0; i < GDB_NUM_REGS; i++) {
        gdb_stub_int_to_hex(gdb_state.registers[i], response + pos, 8);
        pos += 8;
    }
    response[pos] = 0;

    gdb_stub_putpacket(response);
}

/* 写入寄存器 */
void gdb_stub_write_registers(void)
{
    /* 简化：返回OK */
    gdb_stub_putpacket("OK");
}

/* 读取内存 */
int gdb_stub_read_memory(uint32_t addr, uint8_t *buffer, int len)
{
    /* 简化：直接读取内存 */
    for (int i = 0; i < len; i++) {
        buffer[i] = *((volatile uint8_t *)addr + i);
    }
    return len;
}

/* 写入内存 */
int gdb_stub_write_memory(uint32_t addr, const uint8_t *buffer, int len)
{
    /* 简化：直接写入内存 */
    for (int i = 0; i < len; i++) {
        *((volatile uint8_t *)addr + i) = buffer[i];
    }
    return len;
}

/* 插入断点 */
int gdb_stub_insert_breakpoint(uint32_t addr)
{
    if (gdb_state.num_breakpoints >= 16) return -1;

    /* 检查是否已存在 */
    for (int i = 0; i < gdb_state.num_breakpoints; i++) {
        if (gdb_state.breakpoints[i] == addr) return 0;
    }

    gdb_state.breakpoints[gdb_state.num_breakpoints++] = addr;
    return 0;
}

/* 移除断点 */
int gdb_stub_remove_breakpoint(uint32_t addr)
{
    for (int i = 0; i < gdb_state.num_breakpoints; i++) {
        if (gdb_state.breakpoints[i] == addr) {
            /* 移动后面的断点 */
            for (int j = i; j < gdb_state.num_breakpoints - 1; j++) {
                gdb_state.breakpoints[j] = gdb_state.breakpoints[j + 1];
            }
            gdb_state.num_breakpoints--;
            return 0;
        }
    }
    return -1;
}

/* 插入观察点 */
int gdb_stub_insert_watchpoint(uint32_t addr, int type)
{
    if (gdb_state.num_watchpoints >= 16) return -1;
    gdb_state.watchpoints[gdb_state.num_watchpoints++] = addr;
    return 0;
}

/* 移除观察点 */
int gdb_stub_remove_watchpoint(uint32_t addr, int type)
{
    for (int i = 0; i < gdb_state.num_watchpoints; i++) {
        if (gdb_state.watchpoints[i] == addr) {
            for (int j = i; j < gdb_state.num_watchpoints - 1; j++) {
                gdb_state.watchpoints[j] = gdb_state.watchpoints[j + 1];
            }
            gdb_state.num_watchpoints--;
            return 0;
        }
    }
    return -1;
}

/* 单步执行 */
void gdb_stub_single_step(void)
{
    gdb_state.single_step = 1;
    /* 设置TF标志 */
    gdb_state.registers[GDB_REG_PS] |= (1 << 8);
}

/* 继续执行 */
void gdb_stub_continue(void)
{
    gdb_state.single_step = 0;
    /* 清除TF标志 */
    gdb_state.registers[GDB_REG_PS] &= ~(1 << 8);
}

/* 分离 */
void gdb_stub_detach(void)
{
    gdb_state.connected = 0;
    gdb_stub_putpacket("OK");
}

/* 终止 */
void gdb_stub_kill(void)
{
    gdb_state.connected = 0;
    gdb_stub_putpacket("OK");
}

/* 报告信号 */
void gdb_stub_report_signal(uint8_t sig)
{
    char response[8];
    response[0] = 'S';
    gdb_stub_int_to_hex(sig, response + 1, 2);
    response[3] = 0;
    gdb_stub_putpacket(response);
}

/* 处理GDB命令 */
int gdb_stub_process_packet(const char *packet)
{
    if (!packet || !packet[0]) return -1;

    char cmd = packet[0];
    const char *args = packet + 1;

    switch (cmd) {
        case '?':  /* 报告信号 */
            gdb_stub_report_signal(gdb_state.signal);
            break;

        case 'g':  /* 读取寄存器 */
            gdb_stub_read_registers();
            break;

        case 'G':  /* 写入寄存器 */
            gdb_stub_write_registers();
            break;

        case 'm': {  /* 读取内存：m addr,length */
            uint32_t addr = 0;
            int len = 0;
            const char *p = args;
            while (*p && *p != ',') {
                addr = addr * 16 + ((*p <= '9') ? *p - '0' : *p - 'a' + 10);
                p++;
            }
            if (*p == ',') p++;
            while (*p) {
                len = len * 16 + ((*p <= '9') ? *p - '0' : *p - 'a' + 10);
                p++;
            }

            uint8_t data[256];
            if (len > 256) len = 256;
            gdb_stub_read_memory(addr, data, len);

            char response[512];
            int pos = 0;
            for (int i = 0; i < len; i++) {
                response[pos++] = hex_chars[(data[i] >> 4) & 0xF];
                response[pos++] = hex_chars[data[i] & 0xF];
            }
            response[pos] = 0;
            gdb_stub_putpacket(response);
            break;
        }

        case 'M': {  /* 写入内存：M addr,length:data */
            uint32_t addr = 0;
            int len = 0;
            const char *p = args;
            while (*p && *p != ',') {
                addr = addr * 16 + ((*p <= '9') ? *p - '0' : *p - 'a' + 10);
                p++;
            }
            if (*p == ',') p++;
            while (*p && *p != ':') {
                len = len * 16 + ((*p <= '9') ? *p - '0' : *p - 'a' + 10);
                p++;
            }
            if (*p == ':') p++;

            uint8_t data[256];
            for (int i = 0; i < len && i < 256; i++) {
                uint8_t high = p[i * 2] <= '9' ? p[i * 2] - '0' : p[i * 2] - 'a' + 10;
                uint8_t low = p[i * 2 + 1] <= '9' ? p[i * 2 + 1] - '0' : p[i * 2 + 1] - 'a' + 10;
                data[i] = (high << 4) | low;
            }
            gdb_stub_write_memory(addr, data, len);
            gdb_stub_putpacket("OK");
            break;
        }

        case 'c':  /* 继续执行 */
            gdb_stub_continue();
            return 1;  /* 返回1表示继续执行 */

        case 's':  /* 单步执行 */
            gdb_stub_single_step();
            return 1;

        case 'Z': {  /* 插入断点/观察点 */
            int type = args[0] - '0';
            uint32_t addr = 0;
            const char *p = args + 2;  /* 跳过 type, */
            while (*p && *p != ',') {
                addr = addr * 16 + ((*p <= '9') ? *p - '0' : *p - 'a' + 10);
                p++;
            }

            if (type == 0) {  /* 软件断点 */
                gdb_stub_insert_breakpoint(addr);
            } else if (type >= 2 && type <= 4) {  /* 观察点 */
                gdb_stub_insert_watchpoint(addr, type);
            }
            gdb_stub_putpacket("OK");
            break;
        }

        case 'z': {  /* 移除断点/观察点 */
            int type = args[0] - '0';
            uint32_t addr = 0;
            const char *p = args + 2;
            while (*p && *p != ',') {
                addr = addr * 16 + ((*p <= '9') ? *p - '0' : *p - 'a' + 10);
                p++;
            }

            if (type == 0) {
                gdb_stub_remove_breakpoint(addr);
            } else if (type >= 2 && type <= 4) {
                gdb_stub_remove_watchpoint(addr, type);
            }
            gdb_stub_putpacket("OK");
            break;
        }

        case 'D':  /* 分离 */
            gdb_stub_detach();
            return 2;

        case 'k':  /* 终止 */
            gdb_stub_kill();
            return 2;

        case 'H':  /* 设置线程 */
            gdb_stub_putpacket("OK");
            break;

        case 'T':  /* 查询线程是否存活 */
            gdb_stub_putpacket("OK");
            break;

        case 'q':  /* 查询 */
            if (args[0] == 'S' && args[1] == 'u') {
                gdb_stub_putpacket("");  /* 不支持StartNoAckMode */
            } else if (args[0] == 'f' && args[1] == 'f') {
                gdb_stub_putpacket("");  /* 不支持qOffsets */
            } else {
                gdb_stub_putpacket("");  /* 空响应 */
            }
            break;

        default:
            gdb_stub_putpacket("");  /* 空响应表示不支持 */
            break;
    }

    return 0;
}

/* 异常处理入口 */
void gdb_stub_handle_exception(int vector, uint32_t error_code)
{
    /* 映射异常向量到GDB信号 */
    switch (vector) {
        case 1:  /* 调试异常 */
        case 3:  /* 断点 */
            gdb_state.signal = GDB_SIGTRAP;
            break;
        case 14:  /* 页错误 */
            gdb_state.signal = GDB_SIGSEGV;
            break;
        default:
            gdb_state.signal = GDB_SIGTRAP;
            break;
    }

    vga_printf("[GDB] Exception %d, signal %d\n", vector, gdb_state.signal);

    /* 进入GDB命令循环 */
    gdb_state.connected = 1;
    gdb_stub_report_signal(gdb_state.signal);

    while (gdb_state.connected) {
        char packet[GDB_PACKET_MAX];
        int len = gdb_stub_getpacket(packet, GDB_PACKET_MAX);
        if (len > 0) {
            int result = gdb_stub_process_packet(packet);
            if (result == 1) {
                /* 继续执行或单步 */
                break;
            } else if (result == 2) {
                /* 分离或终止 */
                break;
            }
        }
    }
}

/* 断点函数（可在代码中调用） */
void gdb_stub_breakpoint(void)
{
    /* 触发int3 */
    __asm__ volatile("int $3");
}

/* 初始化GDB stub */
int gdb_stub_init(uint16_t com_port)
{
    memset(&gdb_state, 0, sizeof(gdb_state));
    gdb_com_port = com_port;
    gdb_state.initialized = 1;
    gdb_state.signal = GDB_SIGTRAP;

    vga_printf("[GDB] Stub initialized on COM%d (0x%x)\n",
               (com_port == 0x3F8) ? 1 : (com_port == 0x2F8) ? 2 : 3,
               com_port);

    return 0;
}
