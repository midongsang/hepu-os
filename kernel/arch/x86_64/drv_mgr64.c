/* ============================================================================
 * 赫普OS 64位 - 驱动管理器（仓库/下载/安装）
 * V0.13.18: P0 系统底层 — 驱动联网下载与安装框架
 *
 * 驱动仓库协议（HTTP）：
 *   GET <repo>/drivers/index.txt  → 每行: name|category_id|chipset|desc|size
 *   GET <repo>/drivers/<name>.bin → 驱动数据包
 *
 * 驱动包格式（自定义二进制, 带魔数校验）：
 *   [0]   "HPUDRV01"   8B 魔数
 *   [8]   data_size    u32 LE  负载大小
 *   [12]  name[32]     驱动名
 *   [44]  category     u32 LE  驱动类别（drv_category_t）
 *   [48]  chipset[48]  芯片组
 *   [96]  desc[64]     描述
 *   [160] payload[data_size]  固件/配置数据
 *
 * 说明: 驱动代码为内核静态链接（模块加载器规划中），本框架完成
 *       仓库索引 → 联网下载 → 校验 → 注册安装 → 状态管理 闭环。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "../../shared/drivers/driver_framework.h"

/* 外部依赖 */
extern int http64_get(const char *url, uint8_t *response, int max_response);
extern void vga_puts(const char *s);
extern void* kmalloc(size_t size);
extern void kfree(void *ptr);
extern size_t strlen(const char *str);
extern char *strcpy(char *dst, const char *src);
extern int strcmp(const char *s1, const char *s2);

/* ---------- 常量 ---------- */
#define DRV_PACK_MAGIC      "HPUDRV01"
#define DRV_PACK_HEADER     160          /* 包头长度 */
#define DRV_REPO_MAX_ENTRY  32
#define DRV_NAME_MAX        32
#define DRV_FETCH_MAX       16384        /* 单次下载上限 */

/* 仓库默认地址（宿主测试服务器; 不可达时命令报错, 不崩溃） */
#define DRV_REPO_HOST       "http://10.0.2.2:8081/drivers"

/* ---------- 仓库条目 ---------- */
typedef struct {
    char name[DRV_NAME_MAX];
    int  category;
    char chipset[48];
    char desc[64];
    uint32_t size;
    int  available;
} drv_repo_entry_t;

static drv_repo_entry_t g_repo[DRV_REPO_MAX_ENTRY];
static int g_repo_count = 0;
static int g_repo_loaded = 0;

/* ---------- 轻量字符串工具 ---------- */
static int drvm_strncmp(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return a[i] - b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

static int drvm_parse_int(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

static void drvm_put_hex(uint32_t v)
{
    char buf[12];
    const char *hx = "0123456789ABCDEF";
    int p = 0;
    buf[p++] = '0'; buf[p++] = 'x';
    for (int i = 28; i >= 0; i -= 4) {
        buf[p++] = hx[(v >> i) & 0xF];
    }
    buf[p] = 0;
    vga_puts(buf);
}

/* ---------- 1. 仓库索引更新 ---------- */
int drv_mgr_update(void)
{
    static uint8_t buf[DRV_FETCH_MAX];
    g_repo_count = 0;
    g_repo_loaded = 0;

    vga_puts("[DRVMGR] Fetching repo index...\n");
    /* V0.59.15: 这个调用发生在桌面渲染路径里, 用默认 15 秒会把一帧卡死 15 秒。
     * 仓库索引拿不到就拉倒, 3 秒上限。 */
    extern int http64_get_timeout(const char *url, uint8_t *response, int max_response, int timeout_s);
    int len = http64_get_timeout(DRV_REPO_HOST "/index.txt", buf, sizeof(buf) - 1, 3);
    if (len <= 0) {
        vga_puts("[DRVMGR] Repo unreachable (need network + repo server)\n");
        return -1;
    }
    buf[len] = 0;

    /* 逐行解析: name|category|chipset|desc|size */
    int line = 0;
    int pos = 0;
    while (line < DRV_REPO_MAX_ENTRY && pos < len) {
        char row[220];
        int rp = 0;
        while (pos < len && buf[pos] != '\n' && rp < 219) row[rp++] = (char)buf[pos++];
        if (pos < len) pos++;   /* 跳过 \n */
        if (rp == 0) continue;
        row[rp] = 0;

        /* 跳过空行与 # 注释 */
        if (row[0] == 0 || row[0] == '#') continue;

        drv_repo_entry_t *e = &g_repo[line];
        char *fields[5];
        int fc = 0;
        char *cur = row;
        while (fc < 5) {
            fields[fc++] = cur;
            char *sep = cur;
            while (*sep && *sep != '|') sep++;
            if (*sep) { *sep = 0; cur = sep + 1; }
            else break;
        }

        if (line < 5) {
            vga_puts(fields[0]);
            vga_puts("]\n");
        }
        int nl = strlen(fields[0]);
        if (nl >= DRV_NAME_MAX) nl = DRV_NAME_MAX - 1;
        int k;
        for (k = 0; k < nl; k++) e->name[k] = fields[0][k];
        e->name[nl] = 0;
        e->category = drvm_parse_int(fields[1]);
        for (k = 0; fields[2][k] && k < 47; k++) e->chipset[k] = fields[2][k];
        e->chipset[k] = 0;
        for (k = 0; fields[3][k] && k < 63; k++) e->desc[k] = fields[3][k];
        e->desc[k] = 0;
        e->size = (uint32_t)drvm_parse_int(fields[4]);
        e->available = 1;
        line++;
    }
    g_repo_count = line;
    g_repo_loaded = (line > 0);

    if (g_repo_count == 0) {
        vga_puts("[DRVMGR] Repo index empty\n");
        return -1;
    }
    vga_puts("[DRVMGR] Repo updated\n");
    return g_repo_count;
}

/* ---------- 2. 下载驱动包 ---------- */
int drv_mgr_download(const char *name)
{
    if (!name || name[0] == 0) return -1;

    static uint8_t pack[DRV_FETCH_MAX];
    char url[256];
    int p = 0;
    const char *base = DRV_REPO_HOST "/";
    while (base[p] && p < 200) { url[p] = base[p]; p++; }
    int n = 0;
    while (name[n] && n < 60) { url[p++] = name[n]; n++; }
    url[p++] = '.'; url[p++] = 'b'; url[p++] = 'i'; url[p++] = 'n';
    url[p] = 0;

    vga_puts("[DRVMGR] Downloading ");
    vga_puts(name);
    vga_puts(".bin ...\n");

    int len = http64_get(url, pack, sizeof(pack));
    if (len <= DRV_PACK_HEADER) {
        vga_puts("[DRVMGR] Download failed/short\n");
        return -1;
    }

    /* 校验魔数 */
    if (drvm_strncmp((const char *)pack, DRV_PACK_MAGIC, 8) != 0) {
        vga_puts("[DRVMGR] Bad driver package magic\n");
        return -1;
    }

    /* 校验负载大小一致性 */
    uint32_t dsize = (uint32_t)pack[8] | ((uint32_t)pack[9] << 8)
                   | ((uint32_t)pack[10] << 16) | ((uint32_t)pack[11] << 24);
    if (DRV_PACK_HEADER + dsize > (uint32_t)len) {
        vga_puts("[DRVMGR] Package size mismatch\n");
        return -1;
    }

    vga_puts("[DRVMGR] Downloaded OK (payload ");
    drvm_put_hex(dsize);
    vga_puts(" bytes)\n");
    return 0;
}

/* ---------- 3. 安装驱动包（下载+校验+注册） ---------- */
typedef struct {
    char name_buf[DRV_NAME_MAX];
    char chipset_buf[48];
    char desc_buf[64];
    drv_desc_t desc;
} drv_inst_entry_t;

static drv_inst_entry_t g_installed[DRV_REPO_MAX_ENTRY];
static int g_installed_count = 0;

int drv_mgr_install(const char *name)
{
    if (!name || name[0] == 0) return -1;

    /* 已安装同名驱动则拒绝 */
    for (int i = 0; i < g_installed_count; i++) {
        if (strcmp(g_installed[i].name_buf, name) == 0) {
            vga_puts("[DRVMGR] Already installed: ");
            vga_puts(name);
            vga_puts("\n");
            return -1;
        }
    }
    if (g_installed_count >= DRV_REPO_MAX_ENTRY) {
        vga_puts("[DRVMGR] Install table full\n");
        return -1;
    }

    static uint8_t pack[DRV_FETCH_MAX];
    char url[256];
    int p = 0;
    const char *base = DRV_REPO_HOST "/";
    while (base[p] && p < 200) { url[p] = base[p]; p++; }
    int n = 0;
    while (name[n] && n < 60) { url[p++] = name[n]; n++; }
    url[p++] = '.'; url[p++] = 'b'; url[p++] = 'i'; url[p++] = 'n';
    url[p] = 0;

    vga_puts("[DRVMGR] Install ");
    vga_puts(name);
    vga_puts(": downloading...\n");

    int len = http64_get(url, pack, sizeof(pack));
    if (len <= DRV_PACK_HEADER) {
        vga_puts("[DRVMGR] Download failed\n");
        return -1;
    }
    if (drvm_strncmp((const char *)pack, DRV_PACK_MAGIC, 8) != 0) {
        vga_puts("[DRVMGR] Bad magic\n");
        return -1;
    }

    uint32_t dsize = (uint32_t)pack[8] | ((uint32_t)pack[9] << 8)
                   | ((uint32_t)pack[10] << 16) | ((uint32_t)pack[11] << 24);
    if (DRV_PACK_HEADER + dsize > (uint32_t)len) {
        vga_puts("[DRVMGR] Size mismatch\n");
        return -1;
    }

    /* 负载拷入堆（驱动私有数据） */
    void *payload = kmalloc(dsize ? dsize : 1);
    if (!payload) {
        vga_puts("[DRVMGR] OOM\n");
        return -1;
    }
    for (uint32_t i = 0; i < dsize; i++) {
        ((uint8_t *)payload)[i] = pack[DRV_PACK_HEADER + i];
    }

    drv_inst_entry_t *e = &g_installed[g_installed_count];
    for (n = 0; pack[12 + n] && n < 31; n++) e->name_buf[n] = (char)pack[12 + n];
    e->name_buf[n] = 0;
    for (n = 0; pack[48 + n] && n < 47; n++) e->chipset_buf[n] = (char)pack[48 + n];
    e->chipset_buf[n] = 0;
    for (n = 0; pack[96 + n] && n < 63; n++) e->desc_buf[n] = (char)pack[96 + n];
    e->desc_buf[n] = 0;
    drv_desc_t *d = &e->desc;
    d->name = e->name_buf;
    d->category = (drv_category_t)(pack[44] | (pack[45] << 8) | (pack[46] << 16) | (pack[47] << 24));
    d->chipset = e->chipset_buf;
    d->description = e->desc_buf;
    d->ops.init = 0;
    d->ops.probe = 0;
    d->ops.remove = 0;
    d->ops.ioctl = 0;
    d->ops.read = 0;
    d->ops.write = 0;
    d->ops.irq_handler = 0;
    d->private_data = payload;
    d->state = DRV_STATE_LOADED;
    d->id = drv_register(d);
    if (d->id < 0) {
        vga_puts("[DRVMGR] Register failed\n");
        kfree(payload);
        return -1;
    }
    g_installed_count++;

    vga_puts("[DRVMGR] Installed: ");
    vga_puts(d->name);
    vga_puts(" (id=");
    drvm_put_hex((uint32_t)d->id);
    vga_puts(") category=");
    drvm_put_hex((uint32_t)d->category);
    vga_puts("\n");
    return d->id;
}

/* ---------- 4. 卸载驱动 ---------- */
int drv_mgr_uninstall(int id)
{
    drv_desc_t *d = drv_get(id);
    if (!d) {
        vga_puts("[DRVMGR] No driver id\n");
        return -1;
    }
    if (d->private_data) kfree(d->private_data);
    d->private_data = 0;
    int rc = drv_unregister(id);
    /* 从安装表移除 */
    for (int i = 0; i < g_installed_count; i++) {
        if (g_installed[i].desc.id == id) {
            for (int j = i; j < g_installed_count - 1; j++) g_installed[j] = g_installed[j + 1];
            g_installed_count--;
            break;
        }
    }
    vga_puts("[DRVMGR] Uninstalled id ");
    drvm_put_hex((uint32_t)id);
    vga_puts("\n");
    return rc;
}

/* ---------- 5. 打印仓库 ---------- */
void drv_mgr_print_repo(void)
{
    if (!g_repo_loaded) {
        vga_puts("(repo not loaded; run 'drv update')\n");
        return;
    }
    vga_puts("== Repo drivers ==\n");
    for (int i = 0; i < g_repo_count; i++) {
        vga_puts("  ");
        vga_puts(g_repo[i].name);
        vga_puts(" | cat=");
        char tmp[12];
        int v = g_repo[i].category, tp = 0;
        if (v == 0) tmp[tp++] = '0';
        while (v) { tmp[tp++] = '0' + v % 10; v /= 10; }
        if (tp == 0) tmp[tp++] = '0';
        for (int k = 0; k < tp / 2; k++) { char c = tmp[k]; tmp[k] = tmp[tp - 1 - k]; tmp[tp - 1 - k] = c; }
        tmp[tp] = 0;
        vga_puts(tmp);
        vga_puts(" | ");
        vga_puts(g_repo[i].chipset);
        vga_puts(" | ");
        vga_puts(g_repo[i].desc);
        vga_puts(" | ");
        /* 大小十进制 */
        uint32_t sz = g_repo[i].size;
        char sb[12];
        int sp = 0;
        if (sz == 0) sb[sp++] = '0';
        while (sz) { sb[sp++] = '0' + sz % 10; sz /= 10; }
        for (int k = 0; k < sp / 2; k++) { char c = sb[k]; sb[k] = sb[sp - 1 - k]; sb[sp - 1 - k] = c; }
        sb[sp] = 0;
        vga_puts(sb);
        vga_puts(" bytes\n");
    }
    vga_puts("== end ==\n");
}

/* ---------- 6. 打印已安装 ---------- */
void drv_mgr_print_installed(void)
{
    vga_puts("== Installed drivers (framework) ==\n");
    drv_print_list();
    vga_puts("== end ==\n");
}

void drv_mgr64_init(void)
{
    /* 驱动管理器初始化：空转（仓库在首次 update 时加载） */
}
