/* ============================================================================
 * 赫普OS - GDB远程调试Stub头文件
 * V8.35: 初始实现
 * ========================================================================== */

#ifndef _GDB_STUB_H
#define _GDB_STUB_H

#include <stdint.h>

/* GDB寄存器数量（i386） */
#define GDB_NUM_REGS 16

/* GDB寄存器索引（i386） */
#define GDB_REG_EAX   0
#define GDB_REG_ECX   1
#define GDB_REG_EDX   2
#define GDB_REG_EBX   3
#define GDB_REG_ESP   4
#define GDB_REG_EBP   5
#define GDB_REG_ESI   6
#define GDB_REG_EDI   7
#define GDB_REG_PC    8  /* EIP */
#define GDB_REG_PS    9  /* EFLAGS */
#define GDB_REG_CS    10
#define GDB_REG_SS    11
#define GDB_REG_DS    12
#define GDB_REG_ES    13
#define GDB_REG_FS    14
#define GDB_REG_GS    15

/* GDB信号值 */
#define GDB_SIGINT    2
#define GDB_SIGTRAP   5
#define GDB_SIGSEGV   11
#define GDB_SIGTERM   15

/* GDB数据包最大大小 */
#define GDB_PACKET_MAX 4096

/* GDB stub状态 */
typedef struct {
    int      initialized;
    int      connected;
    uint32_t registers[GDB_NUM_REGS];
    uint8_t  signal;
    int      single_step;
    uint32_t breakpoints[16];
    int      num_breakpoints;
    uint32_t watchpoints[16];
    int      num_watchpoints;
    char     rx_buffer[GDB_PACKET_MAX];
    int      rx_pos;
    char     tx_buffer[GDB_PACKET_MAX];
} gdb_state_t;

/* 函数声明 */
int gdb_stub_init(uint16_t com_port);
void gdb_stub_handle_exception(int vector, uint32_t error_code);
void gdb_stub_breakpoint(void);
void gdb_stub_putchar(char c);
char gdb_stub_getchar(void);
int gdb_stub_getpacket(char *buffer, int max_len);
void gdb_stub_putpacket(const char *buffer);
void gdb_stub_send_ack(void);
void gdb_stub_send_nack(void);
uint8_t gdb_stub_checksum(const char *buffer, int len);
int gdb_stub_hex_to_int(const char *hex, int len);
void gdb_stub_int_to_hex(uint32_t val, char *hex, int len);
void gdb_stub_read_registers(void);
void gdb_stub_write_registers(void);
int gdb_stub_read_memory(uint32_t addr, uint8_t *buffer, int len);
int gdb_stub_write_memory(uint32_t addr, const uint8_t *buffer, int len);
int gdb_stub_insert_breakpoint(uint32_t addr);
int gdb_stub_remove_breakpoint(uint32_t addr);
int gdb_stub_insert_watchpoint(uint32_t addr, int type);
int gdb_stub_remove_watchpoint(uint32_t addr, int type);
void gdb_stub_single_step(void);
void gdb_stub_continue(void);
void gdb_stub_detach(void);
void gdb_stub_kill(void);
void gdb_stub_report_signal(uint8_t sig);
int gdb_stub_process_packet(const char *packet);

/* 串口函数（由平台提供） */
extern void serial_init(uint16_t port, uint32_t baud);
extern void serial_putc(uint16_t port, char c);
extern char serial_getc(uint16_t port);
extern int serial_haschar(uint16_t port);

/* 异常处理函数（由内核提供） */
extern void gdb_exception_handler(int vector, uint32_t error_code,
                                   uint32_t *regs);

#endif /* _GDB_STUB_H */
