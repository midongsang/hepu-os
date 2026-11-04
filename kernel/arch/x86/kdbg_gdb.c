/* ============================================================================
 * 赫普OS - GDB远程调试（从kdebug.c拆分）
 * GDB远程串行协议支持
 * ========================================================================== */

#include <stdint.h>
#include "kdebug.h"
#include "vga.h"
#include "string.h"

extern kdebug_t dbg;

int kdbg_gdb_start(int port)
{
    dbg.gdb_stub_enabled = 1;
    dbg.gdb_port = port;
    strncpy(dbg.gdb_protocol, "rsp", 15);
    vga_printf("[KDBG] GDB stub start, port %d\n", port);
    return 0;
}

int kdbg_gdb_stop(void)
{
    dbg.gdb_stub_enabled = 0;
    dbg.gdb_connected = 0;
    return 0;
}

int kdbg_gdb_handle_packet(const char *packet, char *response, int max_response)
{
    if (!packet || !response || max_response <= 0) return -1;
    if (packet[0] == 'g') {
        /* 读取寄存器 */
        response[0] = 0;
    } else if (packet[0] == 'G') {
        /* 写入寄存器 */
        strncpy(response, "OK", max_response - 1);
    } else if (packet[0] == 'm') {
        /* 读取内存 */
        response[0] = 0;
    } else if (packet[0] == 'M') {
        /* 写入内存 */
        strncpy(response, "OK", max_response - 1);
    } else if (packet[0] == 'c') {
        /* 继续 */
        kdbg_continue();
        strncpy(response, "S05", max_response - 1);
    } else if (packet[0] == 's') {
        /* 单步 */
        kdbg_step();
        strncpy(response, "S05", max_response - 1);
    } else if (packet[0] == '?') {
        /* 查询状态 */
        strncpy(response, "S05", max_response - 1);
    } else {
        strncpy(response, "", max_response - 1);
    }
    return 0;
}

int kdbg_gdb_get_status(int *connected, int *port)
{
    if (connected) *connected = dbg.gdb_connected;
    if (port) *port = dbg.gdb_port;
    return 0;
}

/* 统计 */
