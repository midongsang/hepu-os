/* ============================================================================
 * 赫普OS 64位 - 系统注册表（底层配置持久化）
 * V0.13.19: P0 系统底层 — 注册表
 *
 * 内存 KV 存储 + 持久化到 /etc/registry.dat (ext2)。
 * 命名约定: "sys." 系统级 / "app." 应用级 / "user." 用户级
 * 格式: 每行 key=value (key 不含 '='; value 可含 '=')
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 外部依赖 */
extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
extern int ext2_64_is_mounted(void);
extern void vga_puts(const char *s);
extern size_t strlen(const char *str);

#define REG_FILE       "/etc/registry.dat"
#define REG_MAX_ENTRY  64
#define REG_KEY_MAX    48
#define REG_VAL_MAX    96
#define REG_BUF_MAX    8192

typedef struct {
    char key[REG_KEY_MAX];
    char val[REG_VAL_MAX];
    int  exists;
} reg_entry_t;

static reg_entry_t g_reg[REG_MAX_ENTRY];
static int g_reg_count = 0;
static int g_reg_loaded = 0;

/* ---------- 内部工具 ---------- */
static int reg_strlen(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int reg_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void reg_strcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ---------- 1. 初始化: 从磁盘加载 ---------- */
int reg64_init(void)
{
    static uint8_t buf[REG_BUF_MAX];
    g_reg_count = 0;
    g_reg_loaded = 0;

    if (!ext2_64_is_mounted()) return -1;

    int len = ext2_64_read_file(REG_FILE, buf, sizeof(buf) - 1);
    if (len < 0) {
        /* 文件不存在: 首次启动, 建立空注册表 */
        return 0;
    }
    buf[len] = 0;

    int pos = 0;
    while (pos < len && g_reg_count < REG_MAX_ENTRY) {
        /* 读一行 */
        char line[160];
        int lp = 0;
        while (pos < len && buf[pos] != '\n' && lp < 159) line[lp++] = (char)buf[pos++];
        if (pos < len) pos++;
        if (lp == 0) continue;
        line[lp] = 0;

        /* 跳过注释与空行 */
        if (line[0] == '#' || line[0] == 0) continue;

        /* 找 '=' 分隔 */
        int eq = -1;
        for (int i = 0; i < lp; i++) {
            if (line[i] == '=') { eq = i; break; }
        }
        if (eq <= 0) continue;

        reg_entry_t *e = &g_reg[g_reg_count];
        reg_strcpy(e->key, line, eq < REG_KEY_MAX ? eq + 1 : REG_KEY_MAX);
        reg_strcpy(e->val, line + eq + 1, REG_VAL_MAX);
        e->exists = 1;
        g_reg_count++;
    }
    g_reg_loaded = 1;
    return g_reg_count;
}

/* ---------- 2. 读取 ---------- */
/* V0.13.38: 注册表条目数(供设置页管理UI) */
int reg64_count(void)
{
    return g_reg_count;
}

/* V0.13.38: 第i条键名/键值(供设置页查看) */
const char *reg64_key_at(int i)
{
    if (i < 0 || i >= g_reg_count) return 0;
    return g_reg[i].key;
}
const char *reg64_value_at(int i)
{
    if (i < 0 || i >= g_reg_count) return 0;
    return g_reg[i].val;
}

const char *reg64_get(const char *key, const char *def)
{
    for (int i = 0; i < g_reg_count; i++) {
        if (g_reg[i].exists && reg_strcmp(g_reg[i].key, key) == 0) {
            return g_reg[i].val;
        }
    }
    return def;
}

int reg64_get_int(const char *key, int def)
{
    const char *v = reg64_get(key, 0);
    if (!v) return def;
    int neg = 0;
    if (*v == '-') { neg = 1; v++; }
    int n = 0;
    while (*v >= '0' && *v <= '9') { n = n * 10 + (*v - '0'); v++; }
    return neg ? -n : n;
}

/* ---------- 3. 写入 ---------- */
int reg64_set(const char *key, const char *value)
{
    if (!key || key[0] == 0 || !value) return -1;

    for (int i = 0; i < g_reg_count; i++) {
        if (g_reg[i].exists && reg_strcmp(g_reg[i].key, key) == 0) {
            reg_strcpy(g_reg[i].val, value, REG_VAL_MAX);
            return 0;
        }
    }
    if (g_reg_count >= REG_MAX_ENTRY) return -1;
    reg_entry_t *e = &g_reg[g_reg_count];
    reg_strcpy(e->key, key, REG_KEY_MAX);
    reg_strcpy(e->val, value, REG_VAL_MAX);
    e->exists = 1;
    g_reg_count++;
    return 0;
}

/* ---------- 4. 删除 ---------- */
int reg64_delete(const char *key)
{
    for (int i = 0; i < g_reg_count; i++) {
        if (g_reg[i].exists && reg_strcmp(g_reg[i].key, key) == 0) {
            g_reg[i].exists = 0;
            return 0;
        }
    }
    return -1;
}

/* ---------- 5. 持久化到磁盘 ---------- */
int reg64_save(void)
{
    static uint8_t buf[REG_BUF_MAX];
    int p = 0;

    /* 头注释 */
    const char *hdr = "# Hepu OS Registry v1\n";
    for (int i = 0; hdr[i] && p < REG_BUF_MAX - 1; i++) buf[p++] = (uint8_t)hdr[i];

    for (int i = 0; i < g_reg_count && p < REG_BUF_MAX - 1; i++) {
        if (!g_reg[i].exists) continue;
        /* key=value\n */
        int kl = reg_strlen(g_reg[i].key);
        int vl = reg_strlen(g_reg[i].val);
        for (int k = 0; k < kl && p < REG_BUF_MAX - 1; k++) buf[p++] = (uint8_t)g_reg[i].key[k];
        if (p < REG_BUF_MAX - 1) buf[p++] = '=';
        for (int k = 0; k < vl && p < REG_BUF_MAX - 1; k++) buf[p++] = (uint8_t)g_reg[i].val[k];
        if (p < REG_BUF_MAX - 1) buf[p++] = '\n';
    }

    if (!ext2_64_is_mounted()) return -1;
    return ext2_64_write_file(REG_FILE, buf, (uint32_t)p);
}

/* ---------- 6. 打印 ---------- */
void reg64_list(void)
{
    if (!g_reg_loaded) {
        vga_puts("(registry not loaded)\n");
        return;
    }
    int n = 0;
    for (int i = 0; i < g_reg_count; i++) {
        if (!g_reg[i].exists) continue;
        vga_puts("  ");
        vga_puts(g_reg[i].key);
        vga_puts(" = ");
        vga_puts(g_reg[i].val);
        vga_puts("\n");
        n++;
    }
    vga_puts("(total ");
    /* 十进制计数 */
    char tmp[8];
    int tp = 0, v = n;
    if (v == 0) tmp[tp++] = '0';
    while (v) { tmp[tp++] = '0' + v % 10; v /= 10; }
    for (int k = 0; k < tp / 2; k++) { char c = tmp[k]; tmp[k] = tmp[tp - 1 - k]; tmp[tp - 1 - k] = c; }
    tmp[tp] = 0;
    vga_puts(tmp);
    vga_puts(" entries)\n");
}
