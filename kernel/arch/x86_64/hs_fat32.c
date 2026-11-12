/* ============================================================================
 * hs_fat32.c —— ESP(FAT32) 的最小读写
 *
 * 用途只有一个: 让 Display 页能把 resolution=WxH 写进 ESP 上的 /HEPBOOT.CFG,
 * 从而做到"点选分辨率、重启生效"。
 *
 * 刻意做得很窄:
 *   - 只认 GPT 里的 EFI System Partition
 *   - 只处理根目录下的 8.3 短名小文件(HEPBOOT.CFG 这种几十字节的配置)
 *   - 只在"新内容 <= 原簇容量"时覆盖写, 不分配新簇、不改 FAT 链
 *     (分配簇要动 FSInfo、要写备份 FAT, 那是一条会写坏引导盘的路径)
 *
 * 写引导分区是高风险操作, 所以每一步都先校验再动手: BPB 不合法就整体放弃,
 * 找不到文件就返回错误, 长度超出原簇就拒绝写而不是扩簇。
 * ========================================================================== */
#include <stdint.h>

extern int storage_hal_read(uint32_t lba, uint8_t count, uint8_t *buffer);
extern int storage_hal_write(uint32_t lba, uint8_t count, const uint8_t *buffer);
extern void vga_printf(const char *fmt, ...);

#define F32_MAX_CHAIN 64          /* 根目录链最长遍历簇数 */

typedef struct {
    uint32_t part_lba;            /* ESP 起始 LBA(512B 单位) */
    uint32_t fat_lba;             /* FAT 表起始 */
    uint32_t data_lba;            /* 数据区起始 */
    uint32_t root_clus;           /* 根目录首簇 */
    uint32_t sec_clus;            /* 每簇多少个 512B 扇区 */
    uint32_t clus_bytes;          /* 每簇字节数 */
    int      ok;
} f32_t;

static f32_t g_f32;

static uint16_t f_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t f_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* GPT 分区项里找 EFI System Partition(类型 GUID C12A7328-F81F-11D2-BA4B-00A0C93EC93B) */
static uint32_t f_find_esp(void)
{
    static const uint8_t efi_guid[16] = {
        0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
        0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
    };
    uint8_t sec[512];
    uint32_t ent_lba, ent_num, ent_sz;
    uint8_t *hs = sec;

    if (storage_hal_read(1, 1, sec) != 0) return 0;
    if (!(sec[0] == 'E' && sec[1] == 'F' && sec[2] == 'I' && sec[3] == ' '))
        return 0;                                   /* 不是 GPT */
    ent_lba = f_rd32(hs + 72);
    ent_num = f_rd32(hs + 80);
    ent_sz = f_rd32(hs + 84);
    if (ent_sz != 128 || ent_num == 0 || ent_num > 128) return 0;

    /* 分区项一般都在头几个扇区, 只扫前 16 项即可 */
    if (ent_num > 16) ent_num = 16;
    for (uint32_t i = 0; i < ent_num; i++) {
        uint32_t byte_off = i * 128;
        uint32_t lba = ent_lba + byte_off / 512;
        uint32_t off = byte_off % 512;
        const uint8_t *e;
        int match = 1;
        if (storage_hal_read(lba, 1, sec) != 0) return 0;
        e = sec + off;
        for (int k = 0; k < 16; k++)
            if (e[k] != efi_guid[k]) { match = 0; break; }
        if (match) {
            uint64_t first = (uint64_t)f_rd32(e + 32) |
                             ((uint64_t)f_rd32(e + 36) << 32);
            if (first == 0 || first > 0xFFFFFFFFull) return 0;
            return (uint32_t)first;
        }
    }
    return 0;
}

static int f32_init(void)
{
    uint8_t b[512];
    uint32_t esp, bs, spc, rsvd, nfats, fatsz, rootc, root_sec;

    if (g_f32.ok) return 0;
    esp = f_find_esp();
    if (!esp) return -1;
    if (storage_hal_read(esp, 1, b) != 0) return -2;

    bs = f_rd16(b + 11);
    spc = b[13];
    rsvd = f_rd16(b + 14);
    nfats = b[16];
    fatsz = f_rd32(b + 36);
    rootc = f_rd32(b + 44);

    /* BPB 合理性: 任何一项不对就整体放弃, 不做"尽力而为"的写 */
    if (bs < 512 || bs > 4096 || (bs & (bs - 1)) != 0) return -3;
    if (spc == 0 || spc > 128) return -3;
    if (rsvd == 0 || nfats == 0 || nfats > 4) return -3;
    if (fatsz == 0) return -3;
    if (rootc < 2) return -3;
    if (bs != 512) return -4;        /* 目前只支持 512B 逻辑扇区(与本机存储层一致) */

    g_f32.part_lba = esp;
    g_f32.sec_clus = spc;
    g_f32.clus_bytes = spc * bs;
    g_f32.fat_lba = esp + rsvd;
    g_f32.data_lba = esp + rsvd + nfats * fatsz;
    g_f32.root_clus = rootc;
    g_f32.ok = 1;
    vga_printf("[FAT32] esp=%u data=%u root=%u clus=%u\n",
               (unsigned)esp, (unsigned)g_f32.data_lba,
               (unsigned)rootc, (unsigned)g_f32.clus_bytes);
    return 0;
}

static uint32_t f32_clus_lba(uint32_t clus)
{
    return g_f32.data_lba + (clus - 2) * g_f32.sec_clus;
}

/* 读 FAT 表项, 返回下一簇(>=0x0FFFFFF8 = 链尾) */
static uint32_t f32_next(uint32_t clus)
{
    uint8_t sec[512];
    uint32_t off = clus * 4;
    uint32_t lba = g_f32.fat_lba + off / 512;
    uint32_t in = off % 512;
    if (storage_hal_read(lba, 1, sec) != 0) return 0x0FFFFFFF;
    return f_rd32(sec + in) & 0x0FFFFFFF;
}

/* 遍历根目录找 8.3 文件, 命中则返回其首簇, 并通过 out_size 返回大小 */
static uint32_t f32_find(const char *name83, uint32_t *out_size, uint32_t *out_dir_lba,
                         uint32_t *out_dir_off)
{
    uint8_t sec[512];
    uint32_t clus = g_f32.root_clus;
    int guard = 0;

    while (clus >= 2 && clus < 0x0FFFFFF8 && guard < F32_MAX_CHAIN) {
        uint32_t base = f32_clus_lba(clus);
        for (uint32_t s = 0; s < g_f32.sec_clus; s++) {
            if (storage_hal_read(base + s, 1, sec) != 0) return 0;
            for (uint32_t e = 0; e < 512; e += 32) {
                const uint8_t *d = sec + e;
                int i;
                if (d[0] == 0x00) return 0;             /* 目录结束 */
                if (d[11] == 0x0F) continue;            /* LFN 项 */
                if (d[0] == 0xE5) continue;             /* 已删除 */
                if (d[11] & 0x08) continue;             /* 卷标 */
                /* 8.3 名逐字节比较(构建侧写入时一律大写) */
                for (i = 0; i < 11; i++) {
                    if (d[i] != (uint8_t)name83[i]) break;
                }
                if (i == 11) {
                    uint32_t first = f_rd16(d + 26) | ((uint32_t)f_rd16(d + 20) << 16);
                    if (out_size) *out_size = f_rd32(d + 28);
                    if (out_dir_lba) *out_dir_lba = base + s;
                    if (out_dir_off) *out_dir_off = e;
                    return first;
                }
            }
        }
        clus = f32_next(clus);
        guard++;
    }
    return 0;
}

/* 读根目录下的 8.3 文件(最多一个簇) */
int hs_fat32_read(const char *name83, uint8_t *out, uint32_t max)
{
    uint8_t sec[512];
    uint32_t clus, size, i, n = 0;

    if (f32_init() != 0) return -1;
    clus = f32_find(name83, &size, 0, 0);
    if (clus < 2 || clus >= 0x0FFFFFF8) return -2;
    if (size > g_f32.clus_bytes) size = g_f32.clus_bytes;   /* 只读首簇 */
    if (max < size) size = max;
    if (size == 0) return 0;

    for (i = 0; i < g_f32.sec_clus && n < size; i++) {
        uint32_t k;
        if (storage_hal_read(f32_clus_lba(clus) + i, 1, sec) != 0) return -3;
        for (k = 0; k < 512 && n < size; k++) out[n++] = sec[k];
    }
    return (int)size;
}

/* 覆盖写根目录下的 8.3 文件。
 * 前置条件: 文件已存在, 且新内容不超过一个簇 —— 两条都不满足就返回错误,
 * 绝不扩簇(扩簇要写 FAT 链与 FSInfo, 是能写坏引导盘的路径)。 */
int hs_fat32_write(const char *name83, const uint8_t *data, uint32_t len)
{
    uint8_t sec[512], dir_sec[512];
    uint32_t clus, old_size, dir_lba = 0, dir_off = 0;
    uint32_t i, p = 0;

    if (f32_init() != 0) return -1;
    if (len > g_f32.clus_bytes) return -2;
    clus = f32_find(name83, &old_size, &dir_lba, &dir_off);
    if (clus < 2 || clus >= 0x0FFFFFF8) return -3;
    if (len == 0) return -4;

    /* 分扇区覆盖: 数据区只写前 len 字节, 余下清 0 免得残留旧内容 */
    for (i = 0; i < g_f32.sec_clus; i++) {
        uint32_t k;
        if (p >= len) {
            /* 后面的扇区整扇区清 0 */
            for (k = 0; k < 512; k++) sec[k] = 0;
            if (storage_hal_write(f32_clus_lba(clus) + i, 1, sec) != 0) return -5;
            continue;
        }
        for (k = 0; k < 512; k++) {
            sec[k] = (p < len) ? data[p++] : 0;
        }
        if (storage_hal_write(f32_clus_lba(clus) + i, 1, sec) != 0) return -5;
    }

    /* 更新目录项的"文件大小": 必须写回, 否则 bootloader 读到旧长度会截断 */
    if (storage_hal_read(dir_lba, 1, dir_sec) != 0) return -6;
    {
        uint8_t *d = dir_sec + dir_off;
        d[28] = (uint8_t)(len & 0xFF);
        d[29] = (uint8_t)((len >> 8) & 0xFF);
        d[30] = (uint8_t)((len >> 16) & 0xFF);
        d[31] = (uint8_t)((len >> 24) & 0xFF);
        /* 时间戳不必改; 只清掉"归档"以外的状态位意义不大, 保持原样 */
    }
    if (storage_hal_write(dir_lba, 1, dir_sec) != 0) return -6;

    vga_printf("[FAT32] write %s len=%u (was %u) ok\n",
               name83, (unsigned)len, (unsigned)old_size);
    return 0;
}

/* 自检: 读一遍 HEPBOOT.CFG 的前若干字节并打印, 用于确认 ESP 通路 */
int hs_fat32_selftest(void)
{
    uint8_t buf[128];
    int n = hs_fat32_read("HEPBOOT CFG", buf, sizeof(buf) - 1);
    if (n <= 0) {
        vga_printf("[FAT32] selftest read HEPBOOT.CFG rc=%d\n", n);
        return n;
    }
    buf[n] = 0;
    for (int i = 0; i < n; i++) if (buf[i] == '\n') buf[i] = '|';
    vga_printf("[FAT32] HEPBOOT.CFG(%d): %s\n", n, (const char *)buf);
    return n;
}
