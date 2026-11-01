/* ============================================================================
 * 赫普OS - .hug 真机安装器 (V3.87 阶段 D)
 *
 * 功能: 把 initrd 应用区(/app/)的 .hug 包"安装"到 ext2 数据盘 /app/ 目录,
 *       实现持久安装 (重启仍在, 数据盘不随系统镜像刷写而丢失).
 *       反向操作: 卸载 / 列出已安装应用.
 *
 * 依赖: initrd (应用源区) + ide_ext2 (数据盘 ext2, 见 DESIGN.md 数据盘章节)
 * 命令: shell `hug install <name>` / `hug uninstall <name>` / `hug list`
 * ========================================================================== */

#include "hug_loader.h"
#include "ide_ext2.h"
#include "initrd.h"
#include "vga.h"
#include "string.h"

/* 前向声明 (hug_list_apps 在 hug_disk_version 之前使用) */
static int hug_disk_version(const char *full, char *ver, int cap);

/* 自动补 .hug 后缀 (与 hug_run 一致) */
static void hug_full_name(const char *name, char *full, int cap)
{
    int plen = 0;
    while (name[plen] && plen < cap - 6) plen++;
    int has_ext = (plen > 4) && (name[plen-4]=='.') && (name[plen-3]=='h')
               && (name[plen-2]=='u') && (name[plen-1]=='g');
    if (has_ext) {
        for (int i = 0; i < plen; i++) full[i] = name[i];
        full[plen] = 0;
    } else {
        for (int i = 0; i < plen; i++) full[i] = name[i];
        full[plen] = '.'; full[plen+1] = 'h'; full[plen+2] = 'u'; full[plen+3] = 'g';
        full[plen+4] = 0;
    }
}

/* 安装: initrd /app/<name>.hug → 数据盘 /app/<name>.hug (持久) */
int hug_install(const char *name)
{
    if (!name || !*name) return -1;

    char full[64];
    hug_full_name(name, full, sizeof(full));

    /* 1. 从 initrd 应用区取 .hug 包 */
    int fd = initrd_open(full);
    if (fd < 0) {
        vga_printf("[HUG-INSTALL] error: not found in initrd: %s\n", full);
        return -1;
    }
    int size = initrd_size(fd);
    int index = initrd_find(full);
    initrd_close(fd);
    if (index < 0) {
        vga_puts("[HUG-INSTALL] error: initrd index lookup failed\n");
        return -1;
    }
    const void *data = initrd_get_data(index);
    if (!data || size < (int)sizeof(hug_header_t)) {
        vga_puts("[HUG-INSTALL] error: bad package data\n");
        return -1;
    }
    /* 校验 .hug 头 (防损坏包写入数据盘) */
    if (hug_verify(data, size) != 0) {
        vga_puts("[HUG-INSTALL] error: .hug verify failed, aborted\n");
        return -1;
    }

    /* 2. 挂数据盘 + 空间预检 + 确保 /app 目录 */
    if (ide_ext2_mount() != 0) {
        vga_puts("[HUG-INSTALL] error: ext2 data disk not present\n");
        return -1;
    }
    /* V3.87-D2: 空间不足提前检查, 避免 write_data 中途块耗尽截断 */
    int freeb = ide_ext2_free_bytes();
    if (freeb >= 0 && (uint32_t)freeb < (uint32_t)size + 64) {
        vga_printf("[HUG-INSTALL] error: no space on data disk (free=%d B, need=%d B)\n",
                   freeb, size + 64);
        return -1;
    }
    ide_ext2_mkdir("app");   /* 已存在则忽略 */

    /* 3. 写入 + 落盘 (write_raw 成功返回写入字节数, 必须等于包大小) */
    int w = ide_ext2_write_raw("/app", full, data, size);
    if (w != size) {
        vga_printf("[HUG-INSTALL] error: write /app/%s failed (wrote=%d/%d)\n", full, w, size);
        return -1;
    }
    ide_ext2_sync();
    vga_printf("[HUG-INSTALL] ok: %s (%d B) -> /app/ on data disk\n", full, size);
    return 0;
}

/* 卸载: 从数据盘删除已装应用 */
int hug_uninstall(const char *name)
{
    if (!name || !*name) return -1;
    if (ide_ext2_mount() != 0) {
        vga_puts("[HUG-INSTALL] error: ext2 data disk not present\n");
        return -1;
    }
    char full[64];
    hug_full_name(name, full, sizeof(full));
    if (ide_ext2_delete("/app", full) != 0) {
        vga_printf("[HUG-INSTALL] error: %s not installed\n", full);
        return -1;
    }
    ide_ext2_sync();
    vga_printf("[HUG-INSTALL] removed: %s\n", full);
    return 0;
}

/* 列出数据盘已安装应用 */
int hug_list_apps(void)
{
    if (ide_ext2_mount() != 0) {
        vga_puts("[HUG-INSTALL] error: ext2 data disk not present\n");
        return -1;
    }
    fm_entry_t entries[FM_MAX_ENTRIES];
    int n = ide_ext2_list_entries("/app", entries, FM_MAX_ENTRIES);
    vga_printf("[HUG-INSTALL] installed apps on data disk: %d\n", n < 0 ? 0 : n);
    if (n <= 0) {
        vga_puts(" (none — use `hug install <name>`)\n");
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (entries[i].is_dir) continue;
        /* V3.87-D4: 显示版本 (从数据盘包 manifest 提取) */
        char ver[32];
        if (hug_disk_version(entries[i].name, ver, sizeof(ver)) == 0 && ver[0])
            vga_printf("  %s  v%s  (%u B)\n", entries[i].name, ver, entries[i].size);
        else
            vga_printf("  %s  (%u B)\n", entries[i].name, entries[i].size);
    }
    return 0;
}

/* ============================================================================
 * V3.87 阶段 D-4: 应用版本/升级/详情
 * ========================================================================== */

/* 从 manifest JSON 提取 "version": "x.y.z" (找不到返回 0) */
int hug_extract_version(const char *manifest, uint32_t msize, char *out, int cap)
{
    if (!manifest || cap <= 0) return -1;
    out[0] = 0;
    uint32_t i = 0;
    while (i + 9 <= msize) {
        if (manifest[i] == '"' && msize - i >= 9 &&
            manifest[i+1]=='v' && manifest[i+2]=='e' && manifest[i+3]=='r' &&
            manifest[i+4]=='s' && manifest[i+5]=='i' && manifest[i+6]=='o' &&
            manifest[i+7]=='n' && manifest[i+8]=='"') {
            uint32_t j = i + 9;
            while (j < msize && (manifest[j]==' ' || manifest[j]==':' || manifest[j]=='\t')) j++;
            if (j < msize && manifest[j] == '"') {
                j++;
                int n = 0;
                while (j < msize && manifest[j] != '"' && n < cap - 1) out[n++] = manifest[j++];
                out[n] = 0;
                return 0;
            }
        }
        i++;
    }
    return -1;
}

/* 从数据盘读 .hug 并提取版本 (成功返回 0, ver 填充) */
static int hug_disk_version(const char *full, char *ver, int cap)
{
    static char buf[32768];
    int n = ide_ext2_read_file("/app", full, buf, (int)sizeof(buf));
    if (n <= 0) return -1;
    if (hug_verify(buf, (uint32_t)n) != 0) return -1;
    hug_app_t app;
    if (hug_parse(buf, (uint32_t)n, &app) != 0) return -1;
    return hug_extract_version(app.manifest, app.header.manifest_size, ver, cap);
}

/* hug info <name>: 详情 (来源/大小/版本/manifest 头部) */
int hug_info_cmd(const char *name)
{
    if (!name || !*name) return -1;
    char full[64];
    hug_full_name(name, full, sizeof(full));

    static char buf[32768];
    const char *src = "initrd";
    int size = 0;
    const void *data = NULL;

    int n = ide_ext2_read_file("/app", full, buf, (int)sizeof(buf));
    if (n > 0) {
        data = buf;
        size = n;
        src = "data disk";
    } else if (initrd_ready()) {
        int fd = initrd_open(full);
        if (fd < 0) {
            vga_printf("[HUG] error: not found: %s\n", full);
            return -1;
        }
        size = initrd_size(fd);
        int index = initrd_find(full);
        if (index < 0) { initrd_close(fd); return -1; }
        data = initrd_get_data(index);
        initrd_close(fd);
    }
    if (!data || size <= 0) {
        vga_printf("[HUG] error: not found: %s\n", full);
        return -1;
    }
    if (hug_verify(data, (uint32_t)size) != 0) {
        vga_printf("[HUG] error: bad package: %s\n", full);
        return -1;
    }
    hug_app_t app;
    if (hug_parse(data, (uint32_t)size, &app) != 0) {
        vga_printf("[HUG] error: parse failed: %s\n", full);
        return -1;
    }
    char ver[32];
    if (hug_extract_version(app.manifest, app.header.manifest_size, ver, sizeof(ver)) != 0)
        ver[0] = 0;
    vga_printf("[HUG-INFO] %s  ver=%s  %d B  source=%s\n", full, ver[0] ? ver : "?", size, src);
    /* V3.87-D4-fix: manifest 在包内不保证 NUL 结尾, 限长打印(防 %s 越界读 → GPF 复位) */
    {
        const char *m = app.manifest ? app.manifest : "(null)";
        uint32_t mlen = app.header.manifest_size;
        if (mlen > 240) mlen = 240;
        vga_puts("  manifest: ");
        for (uint32_t i = 0; i < mlen; i++) {
            char c = m[i];
            if (c == 0 || c == '\n') break;
            if (c < 32 || c > 126) c = '.';
            vga_putc(c);
        }
        vga_puts("\n");
    }
    /* V3.87-D5: 权限声明显示 */
    {
        extern int hug_extract_permissions(const char *, uint32_t,
                                           char (*)[20], int);
        extern void hug_print_permissions(char (*)[20], int);
        char perms[4][20];
        int np = hug_extract_permissions(app.manifest, app.header.manifest_size,
                                         perms, 4);
        hug_print_permissions(perms, np);
    }
    return 0;
}

/* hug upgrade <name>: 从 initrd 覆盖更新数据盘已装应用 (含版本对比) */
int hug_upgrade(const char *name)
{
    if (!name || !*name) return -1;
    if (!initrd_ready()) {
        vga_puts("[HUG-UPGRADE] error: initrd not loaded\n");
        return -1;
    }
    char full[64];
    hug_full_name(name, full, sizeof(full));

    int fd = initrd_open(full);
    if (fd < 0) {
        vga_printf("[HUG-UPGRADE] error: not in initrd: %s\n", full);
        return -1;
    }
    int size = initrd_size(fd);
    int index = initrd_find(full);
    initrd_close(fd);
    if (index < 0 || size < (int)sizeof(hug_header_t)) return -1;
    const void *data = initrd_get_data(index);
    if (!data || hug_verify(data, (uint32_t)size) != 0) {
        vga_puts("[HUG-UPGRADE] error: bad package\n");
        return -1;
    }
    hug_app_t app;
    if (hug_parse(data, (uint32_t)size, &app) != 0) return -1;
    char newv[32];
    hug_extract_version(app.manifest, app.header.manifest_size, newv, sizeof(newv));

    /* 版本对比: 数据盘已有? */
    char oldv[32] = "none";
    if (hug_disk_version(full, oldv, sizeof(oldv)) == 0) {
        vga_printf("[HUG-UPGRADE] %s: %s -> %s\n", full, oldv, newv[0] ? newv : "?");
    } else {
        vga_printf("[HUG-UPGRADE] %s: not installed (fresh install %s)\n", full, newv[0] ? newv : "?");
    }

    /* 空间预检 + 覆盖写入 (与 hug_install 同链路) */
    if (ide_ext2_mount() != 0) {
        vga_puts("[HUG-UPGRADE] error: ext2 data disk not present\n");
        return -1;
    }
    int freeb = ide_ext2_free_bytes();
    if (freeb >= 0 && (uint32_t)freeb < (uint32_t)size + 64) {
        vga_printf("[HUG-UPGRADE] error: no space (free=%d B, need=%d B)\n", freeb, size + 64);
        return -1;
    }
    ide_ext2_mkdir("app");
    int w = ide_ext2_write_raw("/app", full, data, size);
    if (w != size) {
        vga_printf("[HUG-UPGRADE] error: write failed (wrote=%d/%d)\n", w, size);
        return -1;
    }
    ide_ext2_sync();
    vga_printf("[HUG-UPGRADE] ok: %s updated (%d B)\n", full, size);
    return 0;
}

/* ============================================================================
 * V3.87 阶段 D-5: 应用权限声明与沙箱校验
 * ========================================================================== */

#define HUG_PERM_MAX    4               /* 每应用最多声明权限数 */
#define HUG_PERM_NAMELEN 20             /* 单权限名字符上限 */

/* 权限白名单 (对齐 SEC 域语义: storage→app_data_t, contacts→contact_t,
 * network→网络类, notifications→通知类; 未声明=最受限) */
static const char *const hug_perm_whitelist[] = {
    "storage", "contacts", "network", "notifications"
};
#define HUG_PERM_WHITELIST_N  4

/* 从 manifest JSON 提取 "permissions": ["a","b"] 数组 (轻量字符串扫描,
 * 无 JSON 库; 找不到字段返回 0 = 无权限声明) */
int hug_extract_permissions(const char *manifest, uint32_t msize,
                            char perms[][HUG_PERM_NAMELEN], int maxn)
{
    if (!manifest || maxn <= 0) return 0;
    const char *key = "\"permissions\"";
    int klen = 13;
    uint32_t i = 0;
    while (i + klen <= msize) {
        if (manifest[i] == '"' &&
            msize - i >= (uint32_t)klen &&
            strncmp(manifest + i, key, (uint32_t)klen) == 0) break;
        i++;
    }
    if (i + klen > msize) return 0;          /* 无 permissions 字段 */

    uint32_t j = i + klen;
    while (j < msize && manifest[j] != '[') {
        if (manifest[j] == ':' || manifest[j] == ' ' || manifest[j] == '\t') j++;
        else return 0;                        /* 格式异常 */
    }
    if (j >= msize || manifest[j] != '[') return 0;
    j++;                                      /* 跳过 '[' */

    int n = 0;
    while (j < msize && n < maxn) {
        while (j < msize && (manifest[j] == ' ' || manifest[j] == '\t' ||
                             manifest[j] == '\n' || manifest[j] == '\r')) j++;
        if (j >= msize) break;
        if (manifest[j] == '"') {
            j++;
            int len = 0;
            while (j < msize && manifest[j] != '"' && len < HUG_PERM_NAMELEN - 1)
                perms[n][len++] = manifest[j++];
            perms[n][len] = 0;
            if (len > 0) n++;
            if (j < msize) j++;               /* 跳过收尾引号 */
        } else if (manifest[j] == ']') {
            break;
        } else if (manifest[j] == ',') {
            j++;
        } else {
            j++;
        }
    }
    return n;
}

/* 白名单校验: 声明了白名单外权限 → 拒绝 (沙箱校验) */
int hug_check_permissions(char perms[][HUG_PERM_NAMELEN], int n)
{
    for (int i = 0; i < n; i++) {
        int ok = 0;
        for (int w = 0; w < HUG_PERM_WHITELIST_N; w++) {
            if (strcmp(perms[i], hug_perm_whitelist[w]) == 0) { ok = 1; break; }
        }
        if (!ok) {
            vga_printf("[HUG-PERM] error: unknown permission '%s' → sandbox reject\n", perms[i]);
            return -1;
        }
    }
    return 0;
}

/* 打印权限列表 (info/run 共用) */
void hug_print_permissions(char perms[][HUG_PERM_NAMELEN], int n)
{
    if (n <= 0) {
        vga_puts("  permissions: (none)\n");
        return;
    }
    vga_puts("  permissions:");
    for (int i = 0; i < n; i++) vga_printf(" %s", perms[i]);
    vga_puts("\n");
}
