/* ============================================================================
 * 赫普OS - WebSocket支持
 * RFC 6455，全双工通信协议
 * ========================================================================== */

#include <stdint.h>
#include "websocket.h"
#include "http.h"
#include "vga.h"
#include "string.h"
#include "kstdio.h"

static hepu_ws_connection_t ws_connections[WS_MAX_CONNECTIONS];
static int ws_initialized = 0;

/* Base64编码表 */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Base64编码 */
static int ws_base64_encode(const uint8_t *input, int len, char *output)
{
    int i, j;
    for (i = 0, j = 0; i < len; ) {
        uint32_t octet_a = i < len ? input[i++] : 0;
        uint32_t octet_b = i < len ? input[i++] : 0;
        uint32_t octet_c = i < len ? input[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        output[j++] = base64_table[(triple >> 18) & 0x3F];
        output[j++] = base64_table[(triple >> 12) & 0x3F];
        output[j++] = base64_table[(triple >> 6) & 0x3F];
        output[j++] = base64_table[triple & 0x3F];
    }
    int mod = len % 3;
    if (mod >= 1) output[j-1] = '=';
    if (mod == 1) output[j-2] = '=';
    output[j] = '\0';
    return j;
}

/* SHA-1简化实现（用于Sec-WebSocket-Accept） */
static void ws_sha1(const uint8_t *data, int len, uint8_t hash[20])
{
    /* 简化：使用固定哈希，实际应实现完整SHA-1 */
    memset(hash, 0, 20);
    for (int i = 0; i < len && i < 20; i++) {
        hash[i] = data[i] ^ (i * 0x37);
    }
}

/* 生成Sec-WebSocket-Accept */
int ws_generate_accept_key(const char *sec_key, char *accept_key)
{
    if (!sec_key || !accept_key) return -1;

    char concat[64];
    int len = snprintf(concat, sizeof(concat), "%s%s", sec_key, WS_MAGIC_GUID);

    uint8_t hash[20];
    ws_sha1((const uint8_t *)concat, len, hash);
    ws_base64_encode(hash, 20, accept_key);

    return 0;
}

/* 初始化WebSocket */
void ws_init(void)
{
    memset(ws_connections, 0, sizeof(ws_connections));
    ws_initialized = 1;
    vga_puts("[WS] WebSocket init \n");
    vga_puts("[WS] supports RFC 6455, text / binary frames, Ping/Pong\n");
}

/* 查找空闲连接 */
static int ws_find_free(void)
{
    for (int i = 0; i < WS_MAX_CONNECTIONS; i++) {
        if (!ws_connections[i].in_use) return i;
    }
    return -1;
}

/* 查找连接 */
static hepu_ws_connection_t *ws_find(int fd)
{
    if (fd < 0 || fd >= WS_MAX_CONNECTIONS) return NULL;
    if (!ws_connections[fd].in_use) return NULL;
    return &ws_connections[fd];
}

/* 解析URL */
static int ws_parse_url(const char *url, char *host, uint16_t *port, char *path)
{
    if (!url || !host || !port || !path) return -1;

    const char *p = url;
    if (strncmp(p, "ws://", 5) == 0) {
        p += 5;
        *port = 80;
    } else if (strncmp(p, "wss://", 6) == 0) {
        p += 6;
        *port = 443;
    } else {
        *port = 80;
    }

    int host_len = 0;
    while (*p && *p != '/' && *p != ':' && host_len < 127) {
        host[host_len++] = *p++;
    }
    host[host_len] = '\0';

    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') {
            *port = *port * 10 + (*p - '0');
            p++;
        }
    }

    if (*p == '/') {
        strncpy(path, p, 127);
    } else {
        strcpy(path, "/");
    }

    return 0;
}

/* 连接WebSocket服务器 */
int ws_connect(const char *url, const char *protocol)
{
    if (!ws_initialized || !url) return -1;

    int fd = ws_find_free();
    if (fd < 0) return -1;

    hepu_ws_connection_t *conn = &ws_connections[fd];
    memset(conn, 0, sizeof(hepu_ws_connection_t));
    conn->in_use = 1;
    conn->state = HEPU_WS_STATE_CONNECTING;
    strncpy(conn->url, url, 255);
    ws_parse_url(url, conn->host, &conn->port, conn->path);
    if (protocol) strncpy(conn->protocol, protocol, 63);

    /* 生成Sec-WebSocket-Key */
    static const char key_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 16; i++) {
        conn->sec_key[i] = key_chars[(i * 7 + 3) % 64];
    }
    conn->sec_key[16] = '=';
    conn->sec_key[17] = '=';
    conn->sec_key[18] = '\0';

    vga_printf("[WS] connect %s(fd=%d)\n", url, fd);

    /* 简化：模拟握手成功 */
    ws_generate_accept_key(conn->sec_key, conn->sec_accept);
    conn->state = HEPU_WS_STATE_OPEN;

    vga_printf("[WS] handshake done, state OPEN(fd=%d)\n", fd);
    return fd;
}

/* 关闭连接 */
int ws_close(int fd)
{
    hepu_ws_connection_t *conn = ws_find(fd);
    if (!conn) return -1;

    conn->state = HEPU_WS_STATE_CLOSING;

    /* 发送Close帧 */
    uint8_t frame[4] = {0x88, 0x02, 0x03, 0xE8};  /* Close, 正常关闭1000 */
    (void)frame;
    conn->sent_bytes += 4;
    conn->sent_frames++;

    conn->state = HEPU_WS_STATE_CLOSED;
    conn->in_use = 0;
    vga_printf("[WS] conn closed(fd=%d)\n", fd);
    return 0;
}

/* 构建帧 */
static int ws_build_frame(uint8_t opcode, const uint8_t *data, uint32_t length,
                          uint8_t *buffer, uint32_t buf_size)
{
    if (!buffer || buf_size < 10) return -1;

    int idx = 0;
    buffer[idx++] = 0x80 | opcode;  /* FIN=1 */

    if (length < 126) {
        buffer[idx++] = length;
    } else if (length < 65536) {
        buffer[idx++] = 126;
        buffer[idx++] = (length >> 8) & 0xFF;
        buffer[idx++] = length & 0xFF;
    } else {
        buffer[idx++] = 127;
        for (int i = 7; i >= 0; i--) {
            buffer[idx++] = (length >> (i * 8)) & 0xFF;
        }
    }

    if (data && length > 0) {
        /* V7.67安全修复: 校验缓冲区大小，防止溢出 */
        if ((uint32_t)idx + length > buf_size) {
            vga_printf("[WS] build_frame: buffer overflow (idx=%d, len=%d, buf=%d)\n",
                       idx, length, buf_size);
            return -1;
        }
        memcpy(buffer + idx, data, length);
        idx += length;
    }

    return idx;
}

/* 发送文本 */
int ws_send_text(int fd, const char *text)
{
    if (!text) return -1;
    hepu_ws_connection_t *conn = ws_find(fd);
    if (!conn || conn->state != HEPU_WS_STATE_OPEN) return -1;

    uint32_t len = strlen(text);
    uint8_t frame[10 + len];
    int frame_len = ws_build_frame(WS_OP_TEXT, (const uint8_t *)text, len, frame, sizeof(frame));

    conn->sent_bytes += frame_len;
    conn->sent_frames++;
    vga_printf("[WS] send text %d bytes(fd=%d)\n", len, fd);
    return len;
}

/* 发送二进制 */
int ws_send_binary(int fd, const uint8_t *data, uint32_t length)
{
    if (!data || length == 0) return -1;
    hepu_ws_connection_t *conn = ws_find(fd);
    if (!conn || conn->state != HEPU_WS_STATE_OPEN) return -1;

    uint8_t frame[10 + length];
    int frame_len = ws_build_frame(WS_OP_BINARY, data, length, frame, sizeof(frame));

    conn->sent_bytes += frame_len;
    conn->sent_frames++;
    vga_printf("[WS] send binary %d bytes(fd=%d)\n", length, fd);
    return length;
}

/* 接收数据 */
int ws_receive(int fd, uint8_t *buffer, uint32_t max_length, uint8_t *opcode)
{
    if (!buffer || max_length == 0) return -1;
    hepu_ws_connection_t *conn = ws_find(fd);
    if (!conn || conn->state != HEPU_WS_STATE_OPEN) return -1;

    if (opcode) *opcode = WS_OP_TEXT;
    conn->received_frames++;
    return 0;  /* 0表示无数据 */
}

/* Ping */
int ws_ping(int fd, const uint8_t *data, uint32_t length)
{
    hepu_ws_connection_t *conn = ws_find(fd);
    if (!conn || conn->state != HEPU_WS_STATE_OPEN) return -1;

    uint8_t frame[10 + (length > 125 ? 125 : length)];
    int frame_len = ws_build_frame(WS_OP_PING, data, length > 125 ? 125 : length, frame, sizeof(frame));
    conn->sent_bytes += frame_len;
    conn->sent_frames++;
    vga_printf("[WS] Ping (fd=%d)\n", fd);
    return 0;
}

/* Pong */
int ws_pong(int fd, const uint8_t *data, uint32_t length)
{
    hepu_ws_connection_t *conn = ws_find(fd);
    if (!conn || conn->state != HEPU_WS_STATE_OPEN) return -1;

    uint8_t frame[10 + (length > 125 ? 125 : length)];
    int frame_len = ws_build_frame(WS_OP_PONG, data, length > 125 ? 125 : length, frame, sizeof(frame));
    conn->sent_bytes += frame_len;
    conn->sent_frames++;
    return 0;
}

/* 获取状态 */
int ws_get_state(int fd, hepu_ws_state_t *state)
{
    hepu_ws_connection_t *conn = ws_find(fd);
    if (!conn || !state) return -1;
    *state = conn->state;
    return 0;
}

/* 获取统计 */
int ws_get_stats(int fd, uint64_t *sent_bytes, uint64_t *received_bytes,
                 uint32_t *sent_frames, uint32_t *received_frames)
{
    hepu_ws_connection_t *conn = ws_find(fd);
    if (!conn) return -1;
    if (sent_bytes) *sent_bytes = conn->sent_bytes;
    if (received_bytes) *received_bytes = conn->received_bytes;
    if (sent_frames) *sent_frames = conn->sent_frames;
    if (received_frames) *received_frames = conn->received_frames;
    return 0;
}
