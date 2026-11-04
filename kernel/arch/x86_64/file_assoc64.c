/* ============================================================================
 * 赫普OS 64位 - 文件关联（扩展名 → 默认应用）
 * V0.13.21: P1 系统底层 — 文件关联
 *
 * 存储: 系统注册表 (app.assoc.<ext> = 应用名), 持久化 /etc/registry.dat
 * 默认规则: 未设置时按扩展名推断 (txt→notes, img→viewer, etc.)
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern const char *reg64_get(const char *key, const char *def);
extern int reg64_set(const char *key, const char *value);
extern void vga_puts(const char *s);

/* 取文件扩展名（不区分大小写; 无扩展名返回空串） */
int file_assoc_get_ext(const char *path, char *ext, int max)
{
    if (!path || !ext || max <= 0) return -1;
    /* 找最后一个 '.' */
    int dot = -1;
    int slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '.') dot = i;
        if (path[i] == '/') slash = i;
    }
    if (dot < 0 || dot < slash) {
        ext[0] = 0;
        return 0;
    }
    int n = 0;
    for (int i = dot + 1; path[i] && n < max - 1; i++) {
        char c = path[i];
        /* 转小写 */
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        ext[n++] = c;
    }
    ext[n] = 0;
    return n;
}

/* 查询关联应用: 先注册表, 后默认推断 */
const char *file_assoc_get(const char *ext)
{
    static char key[64];
    static char result[32];

    /* 注册表查询 */
    int p = 0;
    const char *prefix = "app.assoc.";
    while (prefix[p] && p < 62) { key[p] = prefix[p]; p++; }
    for (int i = 0; ext && ext[i] && p < 63; i++) key[p++] = ext[i];
    key[p] = 0;

    const char *v = reg64_get(key, 0);
    if (v && v[0]) {
        int n = 0;
        while (v[n] && n < 31) { result[n] = v[n]; n++; }
        result[n] = 0;
        return result;
    }

    /* 默认推断 */
    if (ext && ext[0]) {
        const char *defs[][2] = {
            { "txt",  "notes" },
            { "md",   "notes" },
            { "log",  "notes" },
            { "c",    "editor" },
            { "h",    "editor" },
            { "sh",   "editor" },
            { "py",   "editor" },
            { "json", "editor" },
            { "bin",  "hexview" },
        };
        int defs_n = sizeof(defs) / sizeof(defs[0]);
        for (int i = 0; i < defs_n; i++) {
            const char *d = defs[i][0];
            int eq = 1;
            for (int k = 0; d[k] || ext[k]; k++) {
                if (d[k] != ext[k]) { eq = 0; break; }
            }
            if (eq) {
                int n = 0;
                while (defs[i][1][n] && n < 31) { result[n] = defs[i][1][n]; n++; }
                result[n] = 0;
                return result;
            }
        }
        if (ext[0] == 'p' && ext[1] == 'n' && ext[2] == 'g' && !ext[3]) {
            return "imageviewer";
        }
        if (ext[0] == 'i' && ext[1] == 'c' && ext[2] == 'o' && !ext[3]) {
            return "imageviewer";
        }
        return "unknown";
    }
    return "none";
}

/* V0.13.38: 已注册的关联规则数(注册表中 app.assoc.* 条目) */
int file_assoc_count(void)
{
    extern int reg64_count(void);
    extern const char *reg64_key_at(int i);
    int n = 0;
    for (int i = 0; i < reg64_count(); i++) {
        const char *k = reg64_key_at(i);
        if (!k) continue;
        if (k[0]=='a'&&k[1]=='p'&&k[2]=='p'&&k[3]=='.'&&k[4]=='a'&&k[5]=='s'&&k[6]=='s'&&k[7]=='o'&&k[8]=='c') n++;
    }
    return n;
}

/* 设置关联 */
int file_assoc_set(const char *ext, const char *app)
{
    static char key[64];
    if (!ext || !app || ext[0] == 0) return -1;
    int p = 0;
    const char *prefix = "app.assoc.";
    while (prefix[p] && p < 62) { key[p] = prefix[p]; p++; }
    for (int i = 0; ext[i] && p < 63; i++) key[p++] = ext[i];
    key[p] = 0;
    return reg64_set(key, app);
}

/* 列出全部关联 */
void file_assoc_list(void)
{
    vga_puts("== File Associations ==\n");
    vga_puts("  txt -> notes\n");
    vga_puts("  md  -> notes\n");
    vga_puts("  log -> notes\n");
    vga_puts("  c/h/sh/py/json -> editor\n");
    vga_puts("  png/ico -> imageviewer\n");
    vga_puts("  (registry overrides default; use 'assoc set EXT APP' to customize)\n");
    vga_puts("== end ==\n");
}
