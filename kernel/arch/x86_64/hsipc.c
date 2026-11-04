/* ============================================================================
 * 赫普OS - HepuShell IPC 接口实现
 * V0.10.164
 * ========================================================================== */

#include "hsipc.h"
#include "hepushell.h"

extern void vga_puts(const char *s);

/* 简单字符串操作 */
static int str_cmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static void str_cpy(char *dst, const char *src)
{
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static int str_len(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

static void str_ncpy(char *dst, const char *src, int n)
{
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* 全局通道 */
static hsipc_channel_t channels[HSIPC_MAX_CHANNELS];
static int channel_count = 0;
static int ipc_initialized = 0;

/* 初始化 */
int hsipc_init(void)
{
    if (ipc_initialized) return 0;

    for (int i = 0; i < HSIPC_MAX_CHANNELS; i++) {
        channels[i].id = -1;
        channels[i].name[0] = 0;
        channels[i].message_count = 0;
        channels[i].read_pos = 0;
        channels[i].write_pos = 0;
    }
    channel_count = 0;

    /* 创建默认通道 */
    hsipc_create_channel("hepushell");
    hsipc_create_channel("notifications");

    ipc_initialized = 1;
    vga_puts("[HSIPC] Initialized, channels: ");
    char buf[8];
    int n = channel_count, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts("\n");

    return 0;
}

/* 创建通道 */
int hsipc_create_channel(const char *name)
{
    if (!name || channel_count >= HSIPC_MAX_CHANNELS) return -1;

    /* 检查是否已存在 */
    for (int i = 0; i < channel_count; i++) {
        if (str_cmp(channels[i].name, name) == 0) {
            return channels[i].id;
        }
    }

    hsipc_channel_t *ch = &channels[channel_count];
    ch->id = channel_count;
    str_ncpy(ch->name, name, 32);
    ch->message_count = 0;
    ch->read_pos = 0;
    ch->write_pos = 0;

    return channel_count++;
}

/* 查找通道 */
int hsipc_find_channel(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < channel_count; i++) {
        if (str_cmp(channels[i].name, name) == 0) {
            return channels[i].id;
        }
    }
    return -1;
}

/* 发送消息 */
int hsipc_send_message(int channel, hsipc_msg_type_t type,
                       const char *data, int len)
{
    if (channel < 0 || channel >= channel_count) return -1;
    if (channels[channel].message_count >= HSIPC_MAX_MESSAGES) return -1;

    hsipc_channel_t *ch = &channels[channel];
    hsipc_message_t *msg = &ch->messages[ch->write_pos];

    msg->type = type;
    msg->sender = 0;
    msg->target = channel;
    msg->processed = 0;

    if (data && len > 0) {
        int copy_len = len < HSIPC_MAX_MSG_LEN ? len : HSIPC_MAX_MSG_LEN - 1;
        str_ncpy(msg->data, data, copy_len + 1);
        msg->data_len = copy_len;
    } else {
        msg->data[0] = 0;
        msg->data_len = 0;
    }

    ch->write_pos = (ch->write_pos + 1) % HSIPC_MAX_MESSAGES;
    ch->message_count++;

    return 0;
}

/* 接收消息 */
int hsipc_recv_message(int channel, hsipc_message_t *msg)
{
    if (channel < 0 || channel >= channel_count) return -1;
    if (!msg) return -1;

    hsipc_channel_t *ch = &channels[channel];
    if (ch->message_count == 0) return 0;

    *msg = ch->messages[ch->read_pos];
    ch->read_pos = (ch->read_pos + 1) % HSIPC_MAX_MESSAGES;
    ch->message_count--;

    return 1;
}

/* 轮询通道 */
int hsipc_poll_channel(int channel)
{
    if (channel < 0 || channel >= channel_count) return 0;
    return channels[channel].message_count;
}

/* 处理HepuShell命令 */
void hsipc_handle_shell_command(const char *cmd)
{
    if (!cmd) return;

    vga_puts("[HSIPC] Command: ");
    vga_puts(cmd);
    vga_puts("\n");

    /* reload - 重新加载配置 */
    if (str_cmp(cmd, "reload") == 0) {
        hepushell_reload();
        return;
    }

    /* theme mono/default - 切换主题 */
    if (str_cmp(cmd, "theme mono") == 0) {
        hepushell_set_theme(HS_THEME_MONO);
        vga_puts("[HSIPC] Theme: mono\n");
        return;
    }
    if (str_cmp(cmd, "theme default") == 0) {
        hepushell_set_theme(HS_THEME_LIGHT);
        vga_puts("[HSIPC] Theme: default (light)\n");
        return;
    }

    /* workspace N - 切换工作区 */
    if (cmd[0] >= '1' && cmd[0] <= '9' && cmd[1] == 0) {
        hepushell_switch_workspace(cmd[0] - '1');
        return;
    }

    /* launcher - 切换启动器 */
    if (str_cmp(cmd, "launcher") == 0) {
        hepushell_toggle_launcher();
        return;
    }
}

/* 发送命令到HepuShell */
int hsipc_send_command(const char *command)
{
    if (!ipc_initialized) hsipc_init();

    int channel = hsipc_find_channel("hepushell");
    if (channel < 0) channel = hsipc_create_channel("hepushell");

    return hsipc_send_message(channel, HSIPC_CMD_EXEC,
                              command, str_len(command));
}

/* 处理所有待处理消息 */
void hsipc_process_messages(void)
{
    if (!ipc_initialized) return;

    int channel = hsipc_find_channel("hepushell");
    if (channel < 0) return;

    hsipc_message_t msg;
    while (hsipc_recv_message(channel, &msg) > 0) {
        if (msg.type == HSIPC_CMD_EXEC) {
            hsipc_handle_shell_command(msg.data);
        } else if (msg.type == HSIPC_CMD_RELOAD) {
            hepushell_reload();
        }
    }
}
