/* ============================================================================
 * 赫普OS - 强制访问控制 (MAC, SELinux 式)
 * V4.18-E63
 *
 * 模型:
 *   subject(进程标签=进程名) x object(文件/路径前缀) x perm(R/W/X)
 * 语义:
 *   - 规则命中且 perms 含请求权限 -> allow
 *   - 无规则命中 -> enforcing 拒绝 / permissive 放行(计数)
 * 挂钩: kernel/fs/ext2_dir.c ext2_vfs_read/ext2_vfs_write
 * ========================================================================== */

#include <stdint.h>
#include "mac.h"
#include "vga.h"
#include "string.h"

#include "ext2.h"
#include "vfs.h"
#include "kstdio.h"

static mac_rule_t mac_rules[MAC_MAX_RULES];

/* V4.43-E88: 策略文件保护前置声明 (mac_check 先于定义处使用) */
static int mac_protect_policy_file(const char *subject, const char *object, uint32_t perm);
static int  mac_enforcing = 1;       /* 1=enforcing 0=permissive */
static int  mac_initialized = 0;
static uint32_t mac_allow_hits = 0;  /* 命中允许 */
static uint32_t mac_deny_hits  = 0;  /* enforcing 拒绝 / permissive 日志 */

/* V4.53-E98: 环形审计日志——最近 16 条 MAC 决策 */
#define MAC_AUDIT_SIZE 16
typedef struct {
    char subject[24];
    char object[40];
    char perm;       /* 'r'/'w'/'x' */
    char result;     /* 'A'=allow 'D'=deny */
} mac_audit_entry_t;
static mac_audit_entry_t mac_audit_log[MAC_AUDIT_SIZE];
static int mac_audit_head = 0;
static int mac_audit_count = 0;

static void mac_audit_record(const char *subj, const char *obj, char perm, char res)
{
    mac_audit_entry_t *e = &mac_audit_log[mac_audit_head];
    int i = 0;
    while (subj[i] && i < 23) { e->subject[i] = subj[i]; i++; }
    e->subject[i] = 0;
    i = 0;
    while (obj[i] && i < 39) { e->object[i] = obj[i]; i++; }
    e->object[i] = 0;
    e->perm = perm;
    e->result = res;
    mac_audit_head = (mac_audit_head + 1) % MAC_AUDIT_SIZE;
    if (mac_audit_count < MAC_AUDIT_SIZE) mac_audit_count++;
}

void mac_init(void)
{
    memset(mac_rules, 0, sizeof(mac_rules));
    mac_enforcing = 1;
    mac_allow_hits = 0;
    mac_deny_hits = 0;
    mac_initialized = 1;
    vga_puts("[MAC] mandatory access control init (enforcing=1)\n");
}

static mac_rule_t *mac_find(const char *sub, const char *obj)
{
    for (int i = 0; i < MAC_MAX_RULES; i++) {
        if (!mac_rules[i].enabled) continue;
        if (strcmp(mac_rules[i].subject, sub) != 0) continue;
        /* object 前缀匹配: 规则 object 是路径/文件前缀 */
        if (strncmp(obj, mac_rules[i].object,
                    strlen(mac_rules[i].object)) != 0) continue;
        return &mac_rules[i];
    }
    return NULL;
}

int mac_add_rule(const char *subject, const char *object, uint32_t perms)
{
    if (!mac_initialized) return -1;
    if (!subject || !object) return -1;
    for (int i = 0; i < MAC_MAX_RULES; i++) {
        if (mac_rules[i].enabled) continue;
        strncpy(mac_rules[i].subject, subject, MAC_LABEL_MAX - 1);
        strncpy(mac_rules[i].object, object, MAC_LABEL_MAX - 1);
        mac_rules[i].subject[MAC_LABEL_MAX - 1] = '\0';
        mac_rules[i].object[MAC_LABEL_MAX - 1]  = '\0';
        mac_rules[i].perms = perms;
        mac_rules[i].enabled = 1;
        vga_printf("[MAC] rule: %s -> %s perm=0x%x\n", subject, object, perms);
        return 0;
    }
    return -1;   /* 表满 */
}

int mac_check(const char *subject, const char *object, uint32_t perm)
{
    if (!mac_initialized) return 0;   /* 未初始化放行 */
    if (!subject || !object) return 0;
    /* V4.42-E87: 内核管理域特权——系统自身管理操作(规则持久化/配置读写)
     * 不受用户态 MAC 策略约束 (SELinux 中 kernel_t 域同语义) */
    if (strcmp(subject, "kernel") == 0) return 0;
    /* V4.43-E88: 策略文件只允许内核域访问 (先于规则匹配) */
    {
        int pr = mac_protect_policy_file(subject, object, perm);
        if (pr != 0) return pr;
    }
    mac_rule_t *r = mac_find(subject, object);
    char pc = (perm == MAC_PERM_R) ? 'r' : (perm == MAC_PERM_X) ? 'x' : 'w';
    if (r && (r->perms & perm)) {
        mac_allow_hits++;
        mac_audit_record(subject, object, pc, 'A');   /* V4.53-E98 */
        return 0;
    }
    if (r && !(r->perms & perm)) {
        /* 规则命中但缺权限 */
        mac_deny_hits++;
        mac_audit_record(subject, object, pc, 'D');   /* V4.53-E98 */
        vga_printf("[MAC] deny: %s wants %c on %s (rule has 0x%x)\n",
                   subject, pc, object, r->perms);
        return (mac_enforcing) ? -3 : 0;   /* EACCES 语义 */
    }
    /* 无规则 */
    if (!mac_enforcing) {
        mac_deny_hits++;
        mac_audit_record(subject, object, pc, 'P');   /* permissive 记录 */
        return 0;   /* permissive: 放行 */
    }
    mac_deny_hits++;
    mac_audit_record(subject, object, pc, 'D');   /* V4.53-E98 */
    vga_printf("[MAC] deny: %s wants %c on %s (no rule)\n",
               subject, pc, object);
    return -3;   /* enforcing: 拒绝 */
}

/* V4.43-E88: 删除规则 (subject+object 精确匹配) */
int mac_rule_del(const char *subject, const char *object)
{
    if (!subject || !object) return -1;
    for (int i = 0; i < MAC_MAX_RULES; i++) {
        if (!mac_rules[i].enabled) continue;
        if (strcmp(mac_rules[i].subject, subject) == 0 &&
            strcmp(mac_rules[i].object, object) == 0) {
            memset(&mac_rules[i], 0, sizeof(mac_rule_t));
            vga_printf("[MAC] del rule: %s -> %s\n", subject, object);
            return 0;
        }
    }
    vga_printf("[MAC] del: no match for %s -> %s\n", subject, object);
    return -1;
}

/* V4.43-E88: 规则文件权限保护——策略文件只能内核域读写,
 * 用户态进程即使有规则命中也不得访问 (防止越权改策略) */
static int mac_protect_policy_file(const char *subject, const char *object, uint32_t perm)
{
    if (strcmp(subject, "kernel") == 0) return 0;
    if (!object) return 0;
    if (strcmp(object, "selinux.rules") == 0 ||
        strncmp(object, "/selinux.rules", 14) == 0 ||
        strncmp(object, "selinux", 7) == 0) {
        mac_deny_hits++;
        vga_printf("[MAC] deny: %s wants %c on policy file %s (protected)\n",
                   subject, (perm == MAC_PERM_R) ? 'r' : 'w', object);
        return (mac_enforcing) ? -3 : 0;
    }
    return 0;
}

void mac_show_all(void)
{
    if (!mac_initialized) { vga_puts("[MAC] not init\n"); return; }
    vga_printf("[MAC] enforcing=%d allow=%d deny=%d\n",
               mac_enforcing, mac_allow_hits, mac_deny_hits);
    vga_puts("subject              object               perm\n");
    for (int i = 0; i < MAC_MAX_RULES; i++) {
        if (!mac_rules[i].enabled) continue;
        vga_printf("%-20s %-20s %c%c%c\n",
                   mac_rules[i].subject, mac_rules[i].object,
                   (mac_rules[i].perms & MAC_PERM_R) ? 'r' : '-',
                   (mac_rules[i].perms & MAC_PERM_W) ? 'w' : '-',
                   (mac_rules[i].perms & MAC_PERM_X) ? 'x' : '-');
    }
}

void mac_setenforce(int on)
{
    mac_enforcing = (on ? 1 : 0);
    vga_printf("[MAC] enforcing=%d\n", mac_enforcing);
}

int mac_enforcing_get(void) { return mac_enforcing; }

void mac_stats(uint32_t *allow, uint32_t *deny)
{
    if (allow) *allow = mac_allow_hits;
    if (deny)  *deny  = mac_deny_hits;
}

/* V4.53-E98: dump 环形审计日志 */
void mac_audit_dump(void)
{
    vga_printf("[MAC] audit log (%d entries):\n", mac_audit_count);
    for (int i = 0; i < mac_audit_count; i++) {
        int idx = (mac_audit_head - mac_audit_count + i + MAC_AUDIT_SIZE) % MAC_AUDIT_SIZE;
        mac_audit_entry_t *e = &mac_audit_log[idx];
        vga_printf("  %s %c on %s -> %s\n",
                   e->subject, e->perm, e->object,
                   e->result == 'A' ? "allow" :
                   e->result == 'P' ? "permissive-log" : "deny");
    }
}

/* ============================================================================
 * V4.42-E87: 规则持久化 —— 落盘 / 读取 / 启动自动加载
 * 格式: 每行 "sub=<subject> obj=<object> perms=<rwx>" 或 "enforce=<0|1>"
 * 存储路径默认 /selinux.rules (数据盘)
 * ========================================================================== */

/* 内部 helper: 把规则文件写入 ext2 数据盘 (直调 ext2 VFS, 与 shell fwrite 同路线) */
static int mac_file_write(const char *path, const char *buf, int len)
{
    extern int ext2_path_create_v2(const char *, uint16_t);
    extern int ext2_vfs_write(vfs_node_t *, uint32_t, const void *, uint32_t);
    /* V4.43-E88: 先删旧文件再重建——ext2_write_data 不截断 i_size,
     * 短写覆盖长文件会残留旧尾部数据 (del 后 save 重启规则又回来的根因) */
    extern int ext2_path_unlink_v2(const char *);
    ext2_path_unlink_v2(path);
    int cr = ext2_path_create_v2(path, 0x1A4);
    if (cr <= 0) {
        extern int ext2_resolve_path(const char *, uint32_t *, char *, int);
        extern int ext2_read_inode(uint32_t, ext2_inode_t *);
        extern int ext2_lookup(ext2_inode_t *, const char *, uint32_t *);
        uint32_t dir_inode = 0;
        char fname[256];
        if (ext2_resolve_path(path, &dir_inode, fname, 256) == 0) {
            ext2_inode_t dir;
            if (ext2_read_inode(dir_inode, &dir) == 0) {
                uint32_t existing = 0;
                if (ext2_lookup(&dir, fname, &existing) == 0) cr = (int)existing;
            }
        }
    }
    if (cr <= 0) return -1;
    static vfs_node_t tmp_node;
    memset(&tmp_node, 0, sizeof(tmp_node));
    strncpy(tmp_node.name, path + 1, VFS_MAX_NAME - 1);
    tmp_node.inode_num = (uint32_t)cr;
    return ext2_vfs_write(&tmp_node, 0, buf, (uint32_t)len);
}

/* 内部 helper: 从 ext2 数据盘读规则文件 (不存在返回 -1) */
static int mac_file_read(const char *path, char *buf, int max)
{
    extern int ext2_resolve_path(const char *, uint32_t *, char *, int);
    extern int ext2_read_inode(uint32_t, ext2_inode_t *);
    extern int ext2_lookup(ext2_inode_t *, const char *, uint32_t *);
    uint32_t dir_inode = 0;
    char fname[256];
    if (ext2_resolve_path(path, &dir_inode, fname, 256) != 0) return -1;
    ext2_inode_t dir;
    if (ext2_read_inode(dir_inode, &dir) != 0) return -1;
    uint32_t ino = 0;
    if (ext2_lookup(&dir, fname, &ino) != 0) return -1;
    ext2_inode_t ino_st;
    if (ext2_read_inode(ino, &ino_st) != 0) return -1;
    /* 用公开 API ext2_read_data 读文件内容 (ext2_vfs_read 为 static) */
    extern int ext2_read_data(ext2_inode_t *, uint32_t, void *, uint32_t);
    return ext2_read_data(&ino_st, 0, buf, (uint32_t)max);
}

/* 导出规则枚举 (save 用) */
int mac_rule_get(int idx, mac_rule_t *out)
{
    if (idx < 0 || idx >= MAC_MAX_RULES) return -1;
    if (!out) return -1;
    *out = mac_rules[idx];
    return (mac_rules[idx].enabled) ? 0 : -1;
}

/* 清空规则表 (load 用) */
void mac_rule_clear(void)
{
    memset(mac_rules, 0, sizeof(mac_rules));
    vga_puts("[MAC] rules cleared\n");
}

/* 保存当前规则到文件 (path 默认由调用方传 /selinux.rules) */
int mac_save(const char *path)
{
    if (!path) return -1;
    char buf[1024];
    int off = 0;
    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "# HEPU MAC rules v1\n");
    for (int i = 0; i < MAC_MAX_RULES; i++) {
        if (!mac_rules[i].enabled) continue;
        off += snprintf(buf + off, sizeof(buf) - (size_t)off, "sub=%s obj=%s perms=%c%c%c\n",
                       mac_rules[i].subject, mac_rules[i].object,
                       (mac_rules[i].perms & MAC_PERM_R) ? 'r' : '-',
                       (mac_rules[i].perms & MAC_PERM_W) ? 'w' : '-',
                       (mac_rules[i].perms & MAC_PERM_X) ? 'x' : '-');
    }
    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "enforce=%d\n", mac_enforcing);
    int w = mac_file_write(path, buf, off);
    if (w <= 0) { vga_puts("[MAC] save fail\n"); return -1; }
    /* V4.42-E87: 强制回写磁盘——脏缓存落盘, 否则重启丢失 */
    extern int ide_ext2_sync(void);
    ide_ext2_sync();
    vga_printf("[MAC] saved %d bytes -> %s\n", w, path);
    return 0;
}

/* 从文件加载规则 (先清空再填入; 文件不存在返回 -1) */
int mac_load(const char *path)
{
    if (!path) return -1;
    char buf[2048];
    int n = mac_file_read(path, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';
    int loaded = 0;
    char *line = buf;
    while (line && *line) {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        if (*nl == '\n') { *nl = '\0'; nl++; } else nl = NULL;
        /* 跳过注释/空行 */
        if (line[0] == '#' || line[0] == '\0' || line[0] == '\n') { line = nl; continue; }
        if (strncmp(line, "enforce=", 8) == 0) {
            mac_setenforce(line[8] == '1' ? 1 : 0);
        } else if (strncmp(line, "sub=", 4) == 0) {
            char sub[MAC_LABEL_MAX], obj[MAC_LABEL_MAX];
            uint32_t perms = 0;
            sub[0] = obj[0] = '\0';
            char *p = line + 4;
            int i = 0;
            while (*p && *p != ' ' && i < MAC_LABEL_MAX - 1) sub[i++] = *p++;
            sub[i] = '\0';
            while (*p == ' ') p++;
            if (strncmp(p, "obj=", 4) == 0) {
                p += 4; i = 0;
                while (*p && *p != ' ' && i < MAC_LABEL_MAX - 1) obj[i++] = *p++;
                obj[i] = '\0';
            }
            while (*p == ' ') p++;
            if (strncmp(p, "perms=", 6) == 0) {
                p += 6;
                for (; *p; p++) {
                    if (*p == 'r') perms |= MAC_PERM_R;
                    else if (*p == 'w') perms |= MAC_PERM_W;
                    else if (*p == 'x') perms |= MAC_PERM_X;
                }
            }
            if (sub[0] && obj[0]) {
                if (mac_add_rule(sub, obj, perms) == 0) loaded++;
            }
        }
        line = nl;
    }
    vga_printf("[MAC] loaded %d rules from %s\n", loaded, path);
    return 0;
}

/* 启动自动加载 (文件不存在静默)
 * V4.42-E87: 先确保数据盘已挂载 (正常构建开机不自动 mount IDE ext2,
 * 懒加载——mac_autoload 在 shell 前调用时必须显式 mount, 否则读不到规则文件) */
void mac_autoload(void)
{
    extern int ide_ext2_mount(void);
    extern void serial_puts(unsigned short, const char *);
    if (ide_ext2_mount() != 0) {
        vga_puts("[MAC] autoload: disk not ready\n");
        serial_puts(0x3F8, "[MAC] AUTOLOAD: MOUNT FAIL\n");
        return;
    }
    if (mac_load("/selinux.rules") != 0) {
        vga_puts("[MAC] autoload: no rules file (default policy active)\n");
        serial_puts(0x3F8, "[MAC] AUTOLOAD: NO RULES\n");
    } else {
        serial_puts(0x3F8, "[MAC] AUTOLOAD: LOADED\n");
    }
}
