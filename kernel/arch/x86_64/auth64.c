/* ============================================================================
 * 赫普OS - 64位账户与角色权限模块 (V0.55 P2)
 * - /account.conf: user=xxx role=1|2 hash=<sha256hex> adminhash=<sha256hex>
 * - role: 0=未配置 1=普通用户 2=开发者
 * - 密码哈希: 内置 SHA-256（64位内核无其他加密依赖），十六进制存储
 * - 文件读写: ext2_64_read_file / ext2_64_write_file（按路径）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* ===== 内核 API ===== */
extern void vga_puts(const char *str);
extern int  vga_printf(const char *fmt, ...);
extern int  ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
extern int  ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);

/* ===== 常量 ===== */
#define ACC_PATH      "/account.conf"
#define SYS_PATH      "/system.conf"
#define ROLE_NONE     0
#define ROLE_NORMAL   1
#define ROLE_DEVELOPER 2
#define HASH_HEX_LEN  64   /* SHA256 = 32 bytes = 64 hex chars */

/* ===== 状态 ===== */
static char     g_user[32];
static int      g_role = ROLE_NONE;
static char     g_hash[HASH_HEX_LEN + 1];
static char     g_admin_hash[HASH_HEX_LEN + 1];
static int      g_auth_inited = 0;

/* ===== 小工具 ===== */
static int a_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int a_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (*a == *b);
}
static void a_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void hex_encode(const uint8_t *bin, int len, char *out) {
    static const char hexd[] = "0123456789abcdef";
    for (int i = 0; i < len; i++) {
        out[i * 2]     = hexd[bin[i] >> 4];
        out[i * 2 + 1] = hexd[bin[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

/* ============================================================================
 * 紧凑 SHA-256（FIPS 180-4，无依赖）
 * ========================================================================== */
static const uint32_t sha_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t shr32(uint32_t x, int n) { return x >> n; }
static uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void auth64_sha256(const uint8_t *data, uint32_t len, uint8_t out[32])
{
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint64_t bitlen = (uint64_t)len * 8;
    uint8_t msg[128];               /* 最多支持 len<=55 的密码（足够） */
    uint32_t i, ml = len;

    /* 安全修复：msg[128] 单缓冲实现只容纳 len<=55（1块）~111（2块）字节。
     * 旧实现无长度检查，超长密码（OOBE/登录输入可被用户控制）直接写穿
     * 栈缓冲区覆盖返回地址 —— ring0 栈溢出 RCE。超长输入一律拒绝。 */
    if (len > 55) {
        for (i = 0; i < 32; i++) out[i] = 0;
        return;
    }

    for (i = 0; i < ml; i++) msg[i] = data[i];
    msg[ml++] = 0x80;
    while ((ml % 64) != 56) msg[ml++] = 0;
    for (i = 0; i < 8; i++) msg[ml + 7 - i] = (uint8_t)(bitlen >> (8 * i));
    ml += 8;

    for (uint32_t off = 0; off < ml; off += 64) {
        uint32_t w[64];
        for (i = 0; i < 16; i++) {
            w[i] = ((uint32_t)msg[off + i*4] << 24) |
                   ((uint32_t)msg[off + i*4 + 1] << 16) |
                   ((uint32_t)msg[off + i*4 + 2] << 8) |
                   (uint32_t)msg[off + i*4 + 3];
        }
        for (i = 16; i < 64; i++) {
            uint32_t s0 = rotr32(w[i-15],7) ^ rotr32(w[i-15],18) ^ shr32(w[i-15],3);
            uint32_t s1 = rotr32(w[i-2],17) ^ rotr32(w[i-2],19) ^ shr32(w[i-2],10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (i = 0; i < 64; i++) {
            uint32_t S1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + sha_k[i] + w[i];
            uint32_t S0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    for (i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)(h[i]);
    }
}

/* SHA256(pass) → 64位hex字符串 */
static void hash_password(const char *pass, char *out_hex) {
    uint8_t digest[32];
    auth64_sha256((const uint8_t *)pass, (uint32_t)a_strlen(pass), digest);
    hex_encode(digest, 32, out_hex);
}

/* ===== 配置文件读写 ===== */
static int conf_write(const char *path, const char *text, int len)
{
    return ext2_64_write_file(path, (const uint8_t *)text, (uint32_t)len);
}

static int conf_read(const char *path, char *buf, int max)
{
    int r = ext2_64_read_file(path, (uint8_t *)buf, (uint32_t)(max - 1));
    if (r <= 0) return -1;
    buf[r] = '\0';
    return r;
}

/* ===== "key=value" 逐行解析 ===== */
static int parse_line(const char *line, const char *key, char *val, int valmax)
{
    int kl = a_strlen(key);
    int i = 0;
    while (line[i] == ' ') i++;
    int j = 0;
    while (j < kl && line[i + j] && line[i + j] == key[j]) j++;
    if (j != kl || line[i + j] != '=') return 0;
    i += j + 1;
    int v = 0;
    while (line[i] && line[i] != '\n' && line[i] != '\r' && v < valmax - 1) {
        val[v++] = line[i++];
    }
    val[v] = '\0';
    return 1;
}

/* ===== 账户文件持久化 ===== */
static int persist_account(void)
{
    char buf[512];
    int p = 0;
    const char *u = "user=";
    for (int i = 0; u[i]; i++) buf[p++] = u[i];
    for (int i = 0; g_user[i]; i++) buf[p++] = g_user[i];
    buf[p++] = '\n';
    const char *r = "role=";
    for (int i = 0; r[i]; i++) buf[p++] = r[i];
    buf[p++] = (char)('0' + g_role);
    buf[p++] = '\n';
    const char *h = "hash=";
    for (int i = 0; h[i]; i++) buf[p++] = h[i];
    for (int i = 0; g_hash[i]; i++) buf[p++] = g_hash[i];
    buf[p++] = '\n';
    const char *ah = "adminhash=";
    for (int i = 0; ah[i]; i++) buf[p++] = ah[i];
    for (int i = 0; g_admin_hash[i]; i++) buf[p++] = g_admin_hash[i];
    buf[p++] = '\n';
    buf[p] = '\0';
    return conf_write(ACC_PATH, buf, p);
}

/* ===== 公开 API ===== */
void auth64_init(void)
{
    if (g_auth_inited) return;
    g_auth_inited = 1;
    g_user[0] = '\0';
    g_hash[0] = '\0';
    g_admin_hash[0] = '\0';
    g_role = ROLE_NONE;

    static char buf[512];
    if (conf_read(ACC_PATH, buf, sizeof(buf)) < 0) {
        vga_puts("[AUTH] No account yet (first boot)\n");
        return;
    }

    char val[80];
    const char *line = buf;
    while (*line) {
        if (parse_line(line, "user", val, sizeof(val)))      a_copy(g_user, val, sizeof(g_user));
        if (parse_line(line, "role", val, sizeof(val)))      g_role = val[0] - '0';
        if (parse_line(line, "hash", val, sizeof(val)))      a_copy(g_hash, val, sizeof(g_hash));
        if (parse_line(line, "adminhash", val, sizeof(val))) a_copy(g_admin_hash, val, sizeof(g_admin_hash));
        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
    if (g_role < ROLE_NONE || g_role > ROLE_DEVELOPER) g_role = ROLE_NONE;
    if (g_hash[0] == '\0') g_role = ROLE_NONE;
    if (g_role != ROLE_NONE) {
        vga_printf("[AUTH] Account loaded: %s (role %d)\n", g_user, g_role);
    }
}

int auth64_is_configured(void) { return g_role != ROLE_NONE; }
const char *auth64_current_user(void) { return g_user; }
int auth64_current_role(void) { return g_role; }

/* 校验账户密码 */
int auth64_verify_password(const char *pass)
{
    if (g_hash[0] == '\0') return 0;
    if (!pass || a_strlen(pass) > 55) return 0;  /* sha256 单缓冲实现上限 */
    char hex[HASH_HEX_LEN + 1];
    hash_password(pass, hex);
    return a_streq(hex, g_hash);
}

/* 创建账户并持久化（role: 1=普通 2=开发者; admin_pass 可为空串） */
int auth64_create_account(const char *name, const char *pass, int role,
                          const char *admin_pass)
{
    if (!name || !name[0] || !pass || !pass[0]) return -1;
    if (role != ROLE_NORMAL && role != ROLE_DEVELOPER) return -1;
    /* sha256 单缓冲实现上限 55 字节，超长密码拒绝（防栈溢出+语义截断碰撞） */
    if (a_strlen(pass) > 55) return -1;
    if (admin_pass && a_strlen(admin_pass) > 55) return -1;
    a_copy(g_user, name, sizeof(g_user));
    hash_password(pass, g_hash);
    g_admin_hash[0] = '\0';
    if (role == ROLE_DEVELOPER && admin_pass && admin_pass[0]) {
        hash_password(admin_pass, g_admin_hash);
    }
    g_role = role;
    if (persist_account() < 0) {
        g_role = ROLE_NONE;
        return -2;
    }
    return 0;
}

/* 普通用户 → 开发者（验证账户密码后升级并持久化） */
int auth64_elevate(const char *pass)
{
    if (g_role == ROLE_NONE) return -1;
    if (!auth64_verify_password(pass)) return -2;
    g_role = ROLE_DEVELOPER;
    if (persist_account() < 0) { g_role = ROLE_NORMAL; return -3; }
    return 0;
}

/* 开发者降回普通用户 */
int auth64_deelevate(void)
{
    if (g_role != ROLE_DEVELOPER) return -1;
    g_role = ROLE_NORMAL;
    if (persist_account() < 0) { g_role = ROLE_DEVELOPER; return -2; }
    return 0;
}

/* system.conf 读写（OOBE 与 l10n 用） */
int auth64_write_config(const char *path, const char *text, int len)
{ return conf_write(path, text, len); }
int auth64_read_config(const char *path, char *buf, int max)
{ return conf_read(path, buf, max); }
