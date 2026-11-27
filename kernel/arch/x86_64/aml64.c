/* ============================================================================
 * 赫普OS - 极简 ACPI AML 解释器（M2）
 * 设计边界见 aml64.h 顶部注释。这里只实现电池读数需要的那一小块。
 * ========================================================================== */

#include "aml64.h"
#include "ec64.h"

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t v);
extern void *memset(void *dst, int c, unsigned long n);
extern void *memcpy(void *dst, const void *src, unsigned long n);
extern int memcmp(const void *a, const void *b, unsigned long n);
extern void *kmalloc(uint32_t size);

#define AML_MAX_NODES    1400
#define AML_PATH_MAX     96
#define AML_MAX_SEGS     8
#define AML_MAX_DEPTH    8
#define AML_STEP_BUDGET  6000
#define AML_PERSIST_SIZE (512u * 1024u)
#define AML_EVAL_SIZE    (384u * 1024u)

enum { NS_EMPTY = 0, NS_SCOPE = 1, NS_NAME = 2, NS_METHOD = 3, NS_FIELD = 4,
       NS_BFIELD = 5 };

/* 字段容器类型：普通 Field / IndexField / BankField */
enum { FIELD_PLAIN = 0, FIELD_INDEX = 1, FIELD_BANK = 2 };

typedef struct {
    char path[AML_PATH_MAX];
    char region_path[AML_PATH_MAX];
    char ctrl_path[AML_PATH_MAX];   /* IndexName / BankName（控制寄存器） */
    char data_path[AML_PATH_MAX];   /* DataName（仅 IndexField） */
    uint8_t kind;
    uint8_t ftype;
    uint8_t argc;
    uint8_t space;
    uint8_t flags;                  /* FieldFlags 原始字节 */
    int16_t region_node;
    int16_t ctrl_node;
    int16_t data_node;
    int32_t bit_off;
    int32_t bit_len;
    uint64_t io_off;
    uint64_t io_len;
    uint64_t bank_val;              /* BankField 的 BankValue 常量 */
    const uint8_t *code;
    uint32_t code_len;
    aml_value_t *val;
} aml_node_t;

typedef struct {
    uint8_t *mem;
    uint32_t size;
    uint32_t used;
} pool_t;

typedef struct {
    int absolute;
    int parents;
    int nseg;
    char seg[AML_MAX_SEGS][5];
} namestr_t;

static aml_node_t *g_nodes;
static int g_nnodes;
static int g_ready;
static int g_degraded;
static pool_t g_persist;
static pool_t g_eval;
static uint32_t g_steps;
static int g_unknown_skipped;   /* 按 PkgLength 跳过的未识别外层对象数 */
static int g_stop_off;          /* 首个硬中止偏移（相对表体，-1=未中止） */
static int g_stop_rc;           /* 中止原因错误码 */
static const uint8_t *g_table_base;
static int g_build_nnodes;      /* 建库结束时的节点数（运行期 BFIELD 节点可在此后截断） */

static unsigned long astr_len(const char *s)
{
    unsigned long n = 0;
    while (s && s[n]) n++;
    return n;
}

static void astr_cpy(char *dst, const char *src, int cap)
{
    int i = 0;
    if (cap <= 0) return;
    while (src && src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int astr_cmp(const char *a, const char *b)
{
    while (a && b && *a && *a == *b) { a++; b++; }
    if (!a || !b) return (a == b) ? 0 : (a ? 1 : -1);
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int namestr_last_is(const namestr_t *ns, const char *name)
{
    if (!ns || ns->nseg <= 0) return 0;
    return astr_cmp(ns->seg[ns->nseg - 1], name) == 0;
}

static void *pool_alloc(pool_t *p, uint32_t n)
{
    void *r;
    n = (n + 7u) & ~7u;
    if (!p->mem || p->used + n > p->size) return 0;
    r = p->mem + p->used;
    p->used += n;
    memset(r, 0, n);
    return r;
}

/* ------------------------------------------------------------------------- */
/* 值构造                                                                     */
/* ------------------------------------------------------------------------- */

static aml_value_t *v_alloc(pool_t *pool, int type)
{
    aml_value_t *v = (aml_value_t *)pool_alloc(pool, (uint32_t)sizeof(aml_value_t));
    if (!v) return 0;
    v->type = (uint8_t)type;
    return v;
}

static aml_value_t *v_int(pool_t *pool, int64_t x)
{
    aml_value_t *v = v_alloc(pool, AML_V_INT);
    if (v) v->ival = x;
    return v;
}

static aml_value_t *v_str(pool_t *pool, const uint8_t *s, int len)
{
    aml_value_t *v = v_alloc(pool, AML_V_STR);
    if (!v) return 0;
    if (len < 0) len = 0;
    v->buf = (uint8_t *)pool_alloc(pool, (uint32_t)len + 1);
    if (!v->buf) return 0;
    if (s && len) memcpy(v->buf, s, (unsigned long)len);
    v->buf[len] = 0;
    v->blen = (uint32_t)len;
    return v;
}

static aml_value_t *v_buf(pool_t *pool, uint32_t len)
{
    aml_value_t *v = v_alloc(pool, AML_V_BUF);
    if (!v) return 0;
    if (len > 4096) len = 4096;
    if (len) {
        v->buf = (uint8_t *)pool_alloc(pool, len);
        if (!v->buf) return 0;
        v->blen = len;
    }
    return v;
}

static aml_value_t *v_pkg(pool_t *pool, uint32_t count)
{
    aml_value_t *v = v_alloc(pool, AML_V_PKG);
    if (!v) return 0;
    if (count > 256) count = 256;
    if (count) {
        v->items = (aml_value_t **)pool_alloc(pool, count * (uint32_t)sizeof(aml_value_t *));
        if (!v->items) return 0;
        v->count = count;
    }
    return v;
}

static aml_value_t *v_ref(pool_t *pool, aml_value_t *base, uint32_t index)
{
    aml_value_t *v = v_alloc(pool, AML_V_REF);
    if (!v) return 0;
    v->base = base;
    v->index = index;
    return v;
}

/* ------------------------------------------------------------------------- */
/* 路径与 NameString                                                          */
/* ------------------------------------------------------------------------- */

static int path_strip_last(char *path)
{
    int n = (int)astr_len(path);
    if (n <= 0) return 0;
    while (n > 0 && path[n - 1] != '.') n--;
    if (n > 0) n--;
    path[n] = 0;
    return n;
}

static int path_apply(const char *scope, const namestr_t *ns, char *out, int cap)
{
    char base[AML_PATH_MAX];
    int i;
    astr_cpy(base, scope, AML_PATH_MAX);
    if (ns->absolute) base[0] = 0;
    else for (i = 0; i < ns->parents; i++) path_strip_last(base);

    out[0] = 0;
    astr_cpy(out, base, cap);
    for (i = 0; i < ns->nseg; i++) {
        int used = (int)astr_len(out);
        if (used && used < cap - 1) { out[used++] = '.'; out[used] = 0; }
        if ((int)astr_len(ns->seg[i]) + used >= cap - 1) return -1;
        astr_cpy(out + used, ns->seg[i], cap - used);
    }
    return 0;
}

static int name_seg_ok(const uint8_t *p)
{
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t c = p[i];
        if (i == 0) {
            if (!((c >= 'A' && c <= 'Z') || c == '_')) return 0;
        } else {
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return 0;
        }
    }
    return 1;
}

static int parse_namestr(const uint8_t **pp, const uint8_t *end, namestr_t *ns)
{
    const uint8_t *p = *pp;
    int i, n = 0;

    ns->absolute = 0;
    ns->parents = 0;
    ns->nseg = 0;

    while (p < end && (*p == 0x5C || *p == 0x5E)) {
        if (*p == 0x5C) { ns->absolute = 1; p++; break; }
        ns->parents++;
        p++;
    }
    if (p >= end) return -1;

    if (*p == 0x2E) {
        p++;
        n = 2;
    } else if (*p == 0x2F) {
        p++;
        if (p >= end) return -1;
        n = *p++;
        if (n < 1 || n > AML_MAX_SEGS) return -1;
    } else {
        n = 1;
    }

    for (i = 0; i < n; i++) {
        if (p + 4 > end || !name_seg_ok(p)) return -1;
        memcpy(ns->seg[i], p, 4);
        ns->seg[i][4] = 0;
        p += 4;
    }
    ns->nseg = n;
    *pp = p;
    return 0;
}

static int pkg_length(const uint8_t **pp, const uint8_t *end, uint32_t *out,
                      const uint8_t **body_end)
{
    const uint8_t *start = *pp;
    const uint8_t *p = start;
    uint8_t b0;
    uint32_t len;
    int lead, i;

    if (p >= end) return -1;
    b0 = *p++;
    lead = b0 >> 6;
    if (lead == 0) {
        len = b0 & 0x3Fu;
    } else {
        if (p + lead > end) return -1;
        len = b0 & 0x0Fu;
        for (i = 1; i <= lead; i++) len |= (uint32_t)p[i - 1] << (4 + 8 * (i - 1));
        p += lead;
    }
    if (len < (uint32_t)(p - start)) return -1;
    if (start + len > end) return -1;
    *pp = p;
    if (body_end) *body_end = start + len;
    if (out) *out = len;
    return 0;
}

/* FieldList 里的 PkgLength 表示位宽，不是字节跨度，不能按表体边界检查。 */
static int pkg_length_raw(const uint8_t **pp, const uint8_t *end, uint32_t *out)
{
    const uint8_t *p = *pp;
    uint8_t b0;
    uint32_t len;
    int lead, i;

    if (p >= end) return -1;
    b0 = *p++;
    lead = b0 >> 6;
    if (lead == 0) {
        len = b0 & 0x3Fu;
    } else {
        if (p + lead > end) return -1;
        len = b0 & 0x0Fu;
        for (i = 1; i <= lead; i++) len |= (uint32_t)p[i - 1] << (4 + 8 * (i - 1));
        p += lead;
    }
    *pp = p;
    if (out) *out = len;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* 命名空间：建表                                                             */
/* ------------------------------------------------------------------------- */

static int ns_find(const char *path)
{
    int i;
    for (i = 0; i < g_nnodes; i++)
        if (g_nodes[i].kind != NS_EMPTY && astr_cmp(g_nodes[i].path, path) == 0) return i;
    return -1;
}

static int ns_add(const char *path, int kind)
{
    int idx = ns_find(path);
    if (idx >= 0) {
        /* Scope/Device 可以重复打开；同名 Name/Method 以先到为准 */
        if (kind == NS_SCOPE && g_nodes[idx].kind == NS_SCOPE) return idx;
        if (g_nodes[idx].kind == kind) return idx;
        return idx;
    }
    if (g_nnodes >= AML_MAX_NODES) return -1;
    idx = g_nnodes++;
    memset(&g_nodes[idx], 0, sizeof(g_nodes[idx]));
    astr_cpy(g_nodes[idx].path, path, AML_PATH_MAX);
    g_nodes[idx].kind = (uint8_t)kind;
    g_nodes[idx].region_node = -1;
    g_nodes[idx].ctrl_node = -1;
    g_nodes[idx].data_node = -1;
    return idx;
}

static aml_value_t *node_value(int idx)
{
    if (idx < 0 || idx >= g_nnodes) return 0;
    return g_nodes[idx].val;
}

static int parse_const_int(const uint8_t **pp, const uint8_t *end, uint64_t *out);

static int parse_data(const uint8_t **pp, const uint8_t *end, aml_value_t **out)
{
    const uint8_t *p = *pp;
    aml_value_t *v = 0;

    if (p >= end) return -1;
    switch (p[0]) {
        case 0x00: v = v_int(&g_persist, 0); p++; break;
        case 0x01: v = v_int(&g_persist, 1); p++; break;
        case 0xFF: v = v_int(&g_persist, -1); p++; break;
        case 0x0A:
            if (p + 2 > end) return -1;
            v = v_int(&g_persist, p[1]);
            p += 2;
            break;
        case 0x0B:
            if (p + 3 > end) return -1;
            v = v_int(&g_persist, (int64_t)(p[1] | (p[2] << 8)));
            p += 3;
            break;
        case 0x0C:
            if (p + 5 > end) return -1;
            v = v_int(&g_persist, (int64_t)((uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                                            ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24)));
            p += 5;
            break;
        case 0x0E:
            if (p + 9 > end) return -1;
            {
                uint64_t q = 0;
                int i;
                for (i = 0; i < 8; i++) q |= (uint64_t)p[1 + i] << (8 * i);
                v = v_int(&g_persist, (int64_t)q);
            }
            p += 9;
            break;
        case 0x0D:
            {
                int len;
                if (p + 2 > end) return -1;
                len = p[1];
                if (p + 3 + len > end) return -1;
                v = v_str(&g_persist, p + 2, len);
                p += 3 + len;   /* StringPrefix + length + bytes + 结尾 NUL */
            }
            break;
        case 0x11:   /* Buffer */
            {
                const uint8_t *body_end = 0;
                uint64_t want = 0;
                uint32_t avail;
                uint32_t n;
                p++;
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                if (parse_const_int(&p, body_end, &want) != 0) return -1;
                n = (uint32_t)want;
                if (n > 4096) n = 4096;
                avail = (uint32_t)(body_end - p);
                v = v_buf(&g_persist, n);
                if (!v) return -1;
                if (avail && v->buf) {
                    uint32_t c = avail < n ? avail : n;
                    memcpy(v->buf, p, c);
                }
                p = body_end;
            }
            break;
        case 0x12:   /* Package */
            {
                const uint8_t *body_end = 0;
                uint32_t count;
                uint32_t i;
                p++;
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                if (p >= body_end) return -1;
                count = *p++;
                v = v_pkg(&g_persist, count);
                if (!v) return -1;
                for (i = 0; i < count && p < body_end; i++) {
                    aml_value_t *e = 0;
                    if (parse_data(&p, body_end, &e) != 0) {
                        e = v_int(&g_persist, 0);
                        g_degraded++;
                    }
                    if (v->items) v->items[i] = e;
                }
                p = body_end;
            }
            break;
        case 0x13:   /* VarPackage */
            {
                const uint8_t *body_end = 0;
                uint64_t count64 = 0;
                uint32_t i;
                p++;
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                if (parse_const_int(&p, body_end, &count64) != 0) return -1;
                if (count64 > 256) count64 = 256;
                v = v_pkg(&g_persist, (uint32_t)count64);
                if (!v) return -1;
                for (i = 0; i < (uint32_t)count64 && p < body_end; i++) {
                    aml_value_t *e = 0;
                    if (parse_data(&p, body_end, &e) != 0) {
                        e = v_int(&g_persist, 0);
                        g_degraded++;
                    }
                    if (v->items) v->items[i] = e;
                }
                p = body_end;
            }
            break;
        default:
            if ((p[0] >= 'A' && p[0] <= 'Z') || p[0] == '_' ||
                p[0] == 0x5C || p[0] == 0x5E || p[0] == 0x2E || p[0] == 0x2F) {
                /* Package 里的名字引用：建库期解析不了就记 0，不中断整表扫描 */
                namestr_t ns;
                char path[AML_PATH_MAX];
                int idx;
                if (parse_namestr(&p, end, &ns) != 0) return -1;
                path_apply("", &ns, path, AML_PATH_MAX);
                idx = ns_find(path);
                v = idx >= 0 ? node_value(idx) : 0;
                if (!v) { v = v_int(&g_persist, 0); g_degraded++; }
            } else {
                return -1;
            }
            break;
    }
    if (!v) return -1;
    *pp = p;
    *out = v;
    return 0;
}

static int parse_const_int(const uint8_t **pp, const uint8_t *end, uint64_t *out)
{
    const uint8_t *p = *pp;
    uint64_t a = 0, b = 0;

    if (p >= end) return -1;
    switch (p[0]) {
        case 0x00: *out = 0; *pp = p + 1; return 0;
        case 0x01: *out = 1; *pp = p + 1; return 0;
        case 0xFF: *out = ~0ULL; *pp = p + 1; return 0;
        case 0x0A:
            if (p + 2 > end) return -1;
            *out = p[1]; *pp = p + 2; return 0;
        case 0x0B:
            if (p + 3 > end) return -1;
            *out = (uint64_t)(p[1] | (p[2] << 8)); *pp = p + 3; return 0;
        case 0x0C:
            if (p + 5 > end) return -1;
            *out = (uint64_t)((uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                              ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24));
            *pp = p + 5; return 0;
        case 0x0E:
            if (p + 9 > end) return -1;
            {
                uint64_t q = 0;
                int i;
                for (i = 0; i < 8; i++) q |= (uint64_t)p[1 + i] << (8 * i);
                *out = q;
            }
            *pp = p + 9; return 0;
        default:
            break;
    }
    if (p[0] >= 0x72 && p[0] <= 0x7F) {
        uint8_t op = p[0];
        *pp = p + 1;
        if (parse_const_int(pp, end, &a) != 0) return -1;
        if (parse_const_int(pp, end, &b) != 0) return -1;
        switch (op) {
            case 0x72: *out = a + b; return 0;
            case 0x74: *out = a - b; return 0;
            case 0x77: *out = a * b; return 0;
            case 0x79: *out = (b >= 64) ? 0 : (a << b); return 0;
            case 0x7A: *out = (b >= 64) ? 0 : (a >> b); return 0;
            case 0x7B: *out = a & b; return 0;
            case 0x7D: *out = a | b; return 0;
            case 0x7F: *out = a ^ b; return 0;
            default: return -1;
        }
    }
    return -1;
}

static int parse_termlist(const uint8_t **pp, const uint8_t *end, const char *scope, int depth);

static int parse_fieldlist(const uint8_t **pp, const uint8_t *body_end, const char *scope,
                           const char *region_path, int ftype,
                           const char *ctrl_path, const char *data_path,
                           uint64_t bank_val, uint8_t flags)
{
    const uint8_t *p = *pp;
    int32_t bit = 0;
    while (p < body_end) {
        uint8_t b0 = *p;
        if (b0 == 0x00) {
            uint32_t gap = 0;
            const uint8_t *ls;
            p++;
            ls = p;
            if (pkg_length_raw(&p, body_end, &gap) != 0) return -1;
            bit += (int32_t)(gap - (uint32_t)(p - ls));
        } else if (b0 == 0x01) {
            p += 3;
        } else if (b0 == 0x03) {
            p += 4;
        } else {
            char name[5];
            char full[AML_PATH_MAX];
            namestr_t ns;
            uint32_t width = 0;
            const uint8_t *ls;
            int32_t bits;
            int idx;
            if (p + 4 > body_end || !name_seg_ok(p)) return -1;
            memcpy(name, p, 4);
            name[4] = 0;
            p += 4;
            ls = p;
            if (pkg_length_raw(&p, body_end, &width) != 0) return -1;
            bits = (int32_t)(width - (uint32_t)(p - ls));
            if (bits <= 0) return -1;
            ns.absolute = 0; ns.parents = 0; ns.nseg = 1;
            astr_cpy(ns.seg[0], name, 5);
            if (path_apply(scope, &ns, full, AML_PATH_MAX) != 0) return -1;
            idx = ns_add(full, NS_FIELD);
            if (idx < 0) return -1;
            g_nodes[idx].ftype = (uint8_t)ftype;
            g_nodes[idx].bit_off = bit;
            g_nodes[idx].bit_len = bits;
            g_nodes[idx].flags = flags;
            astr_cpy(g_nodes[idx].region_path, region_path, AML_PATH_MAX);
            astr_cpy(g_nodes[idx].ctrl_path, ctrl_path ? ctrl_path : "", AML_PATH_MAX);
            astr_cpy(g_nodes[idx].data_path, data_path ? data_path : "", AML_PATH_MAX);
            g_nodes[idx].bank_val = bank_val;
            bit += bits;
            if (bits > 64) g_degraded++;
        }
    }
    *pp = p;
    return 0;
}

static int parse_termlist(const uint8_t **pp, const uint8_t *end, const char *scope, int depth)
{
    const uint8_t *p = *pp;

    if (depth > 16) return -1;

    while (p < end) {
        uint8_t op = *p;
        if (op == 0x10 || op == 0x14 || op == 0x5B) {
            /* handled below */
        }

        if (op == 0x10) {   /* Scope */
            const uint8_t *body_end = 0;
            namestr_t ns;
            char path[AML_PATH_MAX];
            p++;
            if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
            if (parse_namestr(&p, body_end, &ns) != 0) return -1;
            if (path_apply(scope, &ns, path, AML_PATH_MAX) != 0) return -1;
            if (ns_add(path, NS_SCOPE) < 0) return -1;
            if (parse_termlist(&p, body_end, path, depth + 1) != 0) return -1;
            p = body_end;
        } else if (op == 0x14) {   /* Method */
            const uint8_t *body_end = 0;
            namestr_t ns;
            char path[AML_PATH_MAX];
            uint8_t flags;
            int idx;
            p++;
            if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
            if (parse_namestr(&p, body_end, &ns) != 0) return -1;
            if (path_apply(scope, &ns, path, AML_PATH_MAX) != 0) return -1;
            if (p >= body_end) return -1;
            flags = *p++;
            idx = ns_add(path, NS_METHOD);
            if (idx < 0) return -1;
            g_nodes[idx].argc = flags & 0x07;
            g_nodes[idx].code = p;
            g_nodes[idx].code_len = (uint32_t)(body_end - p);
            p = body_end;
        } else if (op == 0x08) {   /* Name */
            namestr_t ns;
            char path[AML_PATH_MAX];
            aml_value_t *v = 0;
            int idx;
            p++;
            if (parse_namestr(&p, end, &ns) != 0) return -1;
            if (path_apply(scope, &ns, path, AML_PATH_MAX) != 0) return -1;
            if (parse_data(&p, end, &v) != 0) return -1;
            idx = ns_add(path, NS_NAME);
            if (idx < 0) return -1;
            g_nodes[idx].val = v;
        } else if (op == 0x06) {   /* Alias */
            namestr_t a, b;
            p++;
            if (parse_namestr(&p, end, &a) != 0) return -1;
            if (parse_namestr(&p, end, &b) != 0) return -1;
        } else if (op == 0x15) {   /* External */
            namestr_t a;
            p++;
            if (parse_namestr(&p, end, &a) != 0) return -1;
            p += 2;
        } else if (op == 0xA0) {   /* If：建库期不求值条件，递归登记 then/else 声明 */
            const uint8_t *then_end = 0, *else_end = 0;
            const uint8_t *q;
            p++;
            if (pkg_length(&p, end, 0, &then_end) != 0) {
                g_stop_off = (int)(p - g_table_base); g_stop_rc = AML_E_BADCODE; return -1;
            }
            q = p;
            (void)parse_termlist(&q, then_end, scope, depth + 1);
            p = then_end;
            if (p < end && *p == 0xA1) {
                const uint8_t *r;
                q = p + 1;
                if (pkg_length(&q, end, 0, &else_end) == 0) {
                    r = q;
                    (void)parse_termlist(&r, else_end, scope, depth + 1);
                    p = else_end;
                }
            }
            g_unknown_skipped++;
            g_degraded++;
        } else if (op == 0xA1) {   /* 孤立 Else：按长度跳过 */
            const uint8_t *body_end = 0;
            p++;
            if (pkg_length(&p, end, 0, &body_end) != 0) {
                g_stop_off = (int)(p - g_table_base); g_stop_rc = AML_E_BADCODE; return -1;
            }
            p = body_end;
            g_unknown_skipped++;
            g_degraded++;
        } else if (op == 0xA2) {   /* While：建库期跳过 body，不求值循环条件 */
            const uint8_t *body_end = 0;
            p++;
            if (pkg_length(&p, end, 0, &body_end) != 0) {
                g_stop_off = (int)(p - g_table_base); g_stop_rc = AML_E_BADCODE; return -1;
            }
            p = body_end;
            g_unknown_skipped++;
            g_degraded++;
        } else if (op == 0x5B) {
            if (p + 1 >= end) return -1;
            uint8_t ext = p[1];
            p += 2;
            if (ext == 0x82) {   /* Device */
                const uint8_t *body_end = 0;
                namestr_t ns;
                char path[AML_PATH_MAX];
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                if (parse_namestr(&p, body_end, &ns) != 0) return -1;
                if (path_apply(scope, &ns, path, AML_PATH_MAX) != 0) return -1;
                if (ns_add(path, NS_SCOPE) < 0) return -1;
                if (parse_termlist(&p, body_end, path, depth + 1) != 0) return -1;
                p = body_end;
            } else if (ext == 0x83) {   /* Processor */
                const uint8_t *body_end = 0;
                namestr_t ns;
                char path[AML_PATH_MAX];
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                if (parse_namestr(&p, body_end, &ns) != 0) return -1;
                if (path_apply(scope, &ns, path, AML_PATH_MAX) != 0) return -1;
                if (ns_add(path, NS_SCOPE) < 0) return -1;
                p += 6;   /* ProcId(1) + PblkAddr(4) + PblkLen(1) */
                if (p > body_end) return -1;
                if (parse_termlist(&p, body_end, path, depth + 1) != 0) return -1;
                p = body_end;
            } else if (ext == 0x84) {   /* PowerRes */
                const uint8_t *body_end = 0;
                namestr_t ns;
                char path[AML_PATH_MAX];
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                if (parse_namestr(&p, body_end, &ns) != 0) return -1;
                if (path_apply(scope, &ns, path, AML_PATH_MAX) != 0) return -1;
                if (ns_add(path, NS_SCOPE) < 0) return -1;
                p += 3;   /* SystemLevel(1) + ResOrder(2) */
                if (p > body_end) return -1;
                if (parse_termlist(&p, body_end, path, depth + 1) != 0) return -1;
                p = body_end;
            } else if (ext == 0x85) {   /* ThermalZone */
                const uint8_t *body_end = 0;
                namestr_t ns;
                char path[AML_PATH_MAX];
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                if (parse_namestr(&p, body_end, &ns) != 0) return -1;
                if (path_apply(scope, &ns, path, AML_PATH_MAX) != 0) return -1;
                if (ns_add(path, NS_SCOPE) < 0) return -1;
                if (parse_termlist(&p, body_end, path, depth + 1) != 0) return -1;
                p = body_end;
            } else if (ext == 0x80) {   /* OperationRegion */
                const uint8_t *body_end = 0;
                namestr_t ns;
                char path[AML_PATH_MAX];
                uint64_t off = 0, len = 0;
                uint8_t space;
                int idx;
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                if (parse_namestr(&p, body_end, &ns) != 0) return -1;
                if (path_apply(scope, &ns, path, AML_PATH_MAX) != 0) return -1;
                if (p >= body_end) return -1;
                space = *p++;
                if (parse_const_int(&p, body_end, &off) != 0) return -1;
                if (parse_const_int(&p, body_end, &len) != 0) { len = 0; g_degraded++; }
                idx = ns_add(path, NS_NAME);
                if (idx < 0) return -1;
                g_nodes[idx].space = space;
                g_nodes[idx].io_off = off;
                g_nodes[idx].io_len = len;
                p = body_end;
            } else if (ext == 0x81) {   /* Field */
                const uint8_t *body_end = 0;
                namestr_t ns;
                char region[AML_PATH_MAX];
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                if (parse_namestr(&p, body_end, &ns) != 0) return -1;
                if (path_apply(scope, &ns, region, AML_PATH_MAX) != 0) return -1;
                if (p >= body_end) return -1;
                p++;   /* FieldFlags */
                if (parse_fieldlist(&p, body_end, scope, region, FIELD_PLAIN,
                                    "", "", 0, 0) != 0) return -1;
                p = body_end;
            } else if (ext == 0x01) {   /* Mutex */
                namestr_t ns;
                char path[AML_PATH_MAX];
                if (parse_namestr(&p, end, &ns) != 0) return -1;
                if (path_apply(scope, &ns, path, AML_PATH_MAX) != 0) return -1;
                p++;
                (void)ns_add(path, NS_NAME);
            } else if (ext == 0x02) {   /* Event */
                namestr_t ns;
                char path[AML_PATH_MAX];
                if (parse_namestr(&p, end, &ns) != 0) return -1;
                if (path_apply(scope, &ns, path, AML_PATH_MAX) != 0) return -1;
                (void)ns_add(path, NS_NAME);
            } else if (ext == 0x86) {   /* IndexField(IndexName, DataName, Flags) */
                const uint8_t *body_end = 0;
                namestr_t idxns, dns;
                char idxpath[AML_PATH_MAX], dpath[AML_PATH_MAX];
                uint8_t flags;
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                if (parse_namestr(&p, body_end, &idxns) != 0) return -1;
                if (parse_namestr(&p, body_end, &dns) != 0) return -1;
                if (path_apply(scope, &idxns, idxpath, AML_PATH_MAX) != 0) return -1;
                if (path_apply(scope, &dns, dpath, AML_PATH_MAX) != 0) return -1;
                if (p >= body_end) return -1;
                flags = *p++;
                if (parse_fieldlist(&p, body_end, scope, dpath, FIELD_INDEX,
                                    idxpath, dpath, 0, flags) != 0) return -1;
                p = body_end;
            } else if (ext == 0x87) {   /* BankField(Region, BankName, BankValue, Flags) */
                const uint8_t *body_end = 0;
                namestr_t rns, bns;
                char rpath[AML_PATH_MAX], bpath[AML_PATH_MAX];
                uint64_t bank_val = 0;
                uint8_t flags;
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                if (parse_namestr(&p, body_end, &rns) != 0) return -1;
                if (parse_namestr(&p, body_end, &bns) != 0) return -1;
                if (path_apply(scope, &rns, rpath, AML_PATH_MAX) != 0) return -1;
                if (path_apply(scope, &bns, bpath, AML_PATH_MAX) != 0) return -1;
                if (parse_const_int(&p, body_end, &bank_val) != 0) return -1;
                if (p >= body_end) return -1;
                flags = *p++;
                if (parse_fieldlist(&p, body_end, scope, rpath, FIELD_BANK,
                                    bpath, "", bank_val, flags) != 0) return -1;
                p = body_end;
            } else {
                /* 未识别扩展对象：带合法 PkgLength 则按长度跳过并记降级；
                 * 无法定位长度才硬中止，偏移/原因透出到诊断接口。 */
                const uint8_t *body_end = 0;
                const uint8_t *q = p;
                if (pkg_length(&q, end, 0, &body_end) == 0) {
                    g_unknown_skipped++;
                    g_degraded++;
                    p = body_end;
                } else {
                    g_stop_off = (int)(p - 2 - g_table_base);
                    g_stop_rc = AML_E_UNSUP;
                    return -1;
                }
            }
        } else {
            /* 未识别单字节外层对象：带合法 PkgLength 则跳过；否则硬中止。 */
            const uint8_t *body_end = 0;
            const uint8_t *q = p;
            if (pkg_length(&q, end, 0, &body_end) == 0) {
                g_unknown_skipped++;
                g_degraded++;
                p = body_end;
            } else {
                g_stop_off = (int)(p - g_table_base);
                g_stop_rc = AML_E_BADCODE;
                return -1;
            }
        }
    }
    *pp = p;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* 求值                                                                       */
/* ------------------------------------------------------------------------- */

typedef struct {
    aml_value_t *locals[8];
    aml_value_t *args[7];
    int argc;
    char scope[AML_PATH_MAX];
    int depth;
    int err;
    int returned;
    aml_value_t *ret;
} exec_t;

typedef struct {
    int node;
    aml_value_t *val;
} overlay_t;

#define AML_OVERLAY_MAX 32
static overlay_t g_overlay[AML_OVERLAY_MAX];
static int g_overlay_n;

static aml_value_t *zero_val(void)
{
    static aml_value_t z;
    z.type = AML_V_INT;
    z.ival = 0;
    return &z;
}

static aml_value_t *node_value_cur(int idx)
{
    int i;
    for (i = g_overlay_n - 1; i >= 0; i--)
        if (g_overlay[i].node == idx) return g_overlay[i].val;
    return node_value(idx);
}

static int ev_expr(exec_t *e, const uint8_t **pp, const uint8_t *end, aml_value_t **out);
static int exec_terms(exec_t *e, const uint8_t *p, const uint8_t *end);

static int to_int(aml_value_t *v, int64_t *out)
{
    if (!v) { *out = 0; return 0; }
    if (v->type == AML_V_INT) { *out = v->ival; return 0; }
    if (v->type == AML_V_BUF && v->buf) {
        uint64_t q = 0;
        uint32_t i, n = v->blen < 8 ? v->blen : 8;
        for (i = 0; i < n; i++) q |= (uint64_t)v->buf[i] << (8 * i);
        *out = (int64_t)q;
        return 0;
    }
    return -1;
}

static int resolve_node(exec_t *e, const namestr_t *ns)
{
    char path[AML_PATH_MAX];
    int idx;
    char scope[AML_PATH_MAX];

    astr_cpy(scope, e->scope, AML_PATH_MAX);
    while (1) {
        if (path_apply(scope, ns, path, AML_PATH_MAX) == 0) {
            idx = ns_find(path);
            if (idx >= 0) return idx;
        }
        if (ns->absolute) return -1;
        if (astr_len(scope) == 0) return -1;
        path_strip_last(scope);
    }
}

static int node_read(exec_t *e, int idx, aml_value_t **out);
static int bfield_extract(aml_value_t *buf, int32_t bo, int32_t bl, uint64_t *out);
static int bfield_write(aml_value_t *buf, int32_t bo, int32_t bl, int64_t x);

static int call_method(exec_t *caller, int idx, aml_value_t **args, int nargs,
                       aml_value_t **out)
{
    aml_node_t *m = &g_nodes[idx];
    exec_t e;
    char scope[AML_PATH_MAX];
    int i;
    aml_value_t *ret = 0;

    if (caller->depth + 1 > AML_MAX_DEPTH) return AML_E_LIMIT;
    if (!m->code) return AML_E_BADCODE;

    memset(&e, 0, sizeof(e));
    astr_cpy(scope, m->path, AML_PATH_MAX);
    path_strip_last(scope);
    astr_cpy(e.scope, scope, AML_PATH_MAX);
    e.depth = caller->depth + 1;
    e.argc = m->argc;
    for (i = 0; i < 7; i++) e.args[i] = (i < nargs) ? args[i] : zero_val();
    for (i = 0; i < 8; i++) e.locals[i] = zero_val();

    exec_terms(&e, m->code, m->code + m->code_len);
    if (e.err && e.err != AML_E_OK) return e.err;
    ret = e.returned ? e.ret : zero_val();
    if (!ret) ret = zero_val();
    *out = ret;
    return AML_E_OK;
}

#ifdef AML_HOST_TEST
/* 主机测试注入的端口模拟（弱定义兜底，既有测试未提供时按 0xFF/丢弃处理）。 */
__attribute__((weak)) uint8_t aml_host_inb(uint16_t port) { (void)port; return 0xFF; }
__attribute__((weak)) void aml_host_outb(uint16_t port, uint8_t val) { (void)port; (void)val; }
/* 主机测试内存回调：返回 0=已接管；-1=未接管，回退 vmm64_phys_to_ptr。 */
__attribute__((weak)) int aml_host_mem_read(uint64_t phys, uint8_t *out) { (void)phys; (void)out; return -1; }
__attribute__((weak)) int aml_host_mem_write(uint64_t phys, uint8_t val) { (void)phys; (void)val; return -1; }
static uint8_t aml_port_in(uint16_t port) { return aml_host_inb(port); }
static void aml_port_out(uint16_t port, uint8_t val) { aml_host_outb(port, val); }
#else
static uint8_t aml_port_in(uint16_t port)
{
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void aml_port_out(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}
#endif

/* 字段位宽 -> granule 字节数；只接受 8/16/32 位寄存器，其余返回 0。 */
static uint32_t fld_width_bytes(int32_t bit_len)
{
    if (bit_len == 8) return 1;
    if (bit_len == 16) return 2;
    if (bit_len == 32) return 4;
    return 0;
}

/* SystemMemory 单字节读/写：主机测试可接管，生产路径走 vmm64 映射。 */
static int mem_rd8(uint64_t phys, uint8_t *out)
{
#ifdef AML_HOST_TEST
    if (aml_host_mem_read(phys, out) == 0) return 0;
#endif
    extern uint64_t vmm64_phys_to_ptr(uint64_t phys);
    const uint8_t *p = (const uint8_t *)vmm64_phys_to_ptr(phys);
    if (!p) return -1;
    *out = p[0];
    return 0;
}

static int mem_wr8(uint64_t phys, uint8_t val)
{
#ifdef AML_HOST_TEST
    if (aml_host_mem_write(phys, val) == 0) return 0;
#endif
    extern uint64_t vmm64_phys_to_ptr(uint64_t phys);
    uint8_t *p = (uint8_t *)vmm64_phys_to_ptr(phys);
    if (!p) return -1;
    p[0] = val;
    return 0;
}

/* 解析字段所属 OperationRegion；返回 region 节点。 */
static int field_region(aml_node_t *f, aml_node_t **reg_out)
{
    int ridx = ns_find(f->region_path);
    if (ridx < 0 && f->region_node >= 0) ridx = f->region_node;
    if (ridx < 0) return AML_E_NOTFOUND;
    *reg_out = &g_nodes[ridx];
    if ((*reg_out)->kind != NS_NAME || (*reg_out)->io_len == 0) return AML_E_BADCODE;
    return AML_E_OK;
}

/* 从普通字段寄存器原始读取 nbytes 字节（小端拼整数，不做位抽取/掩码）。
 * 边界：端口 ≤0xFFFF、io_len 越界、内存空指针、EC 超时均明确失败。 */
static int reg_read_raw(aml_node_t *f, aml_node_t *r, uint32_t nbytes, uint64_t *out)
{
    uint64_t val = 0;
    uint32_t i;

    if (nbytes == 0 || nbytes > 8) return AML_E_UNSUP;

    if (r->space == 1) {   /* SystemIO */
        uint64_t port = r->io_off + (uint64_t)(f->bit_off / 8);
        if (port > 0xFFFF) return AML_E_BADCODE;
        if (r->io_len && port - r->io_off + nbytes > r->io_len) return AML_E_BADCODE;
        for (i = 0; i < nbytes; i++)
            val |= (uint64_t)aml_port_in((uint16_t)(port + i)) << (8 * i);
    } else if (r->space == 0) {   /* SystemMemory */
        uint64_t phys = r->io_off + (uint64_t)(f->bit_off / 8);
        if (r->io_len && phys - r->io_off + nbytes > r->io_len) return AML_E_BADCODE;
        for (i = 0; i < nbytes; i++) {
            uint8_t b = 0;
            if (mem_rd8(phys + i, &b) != 0) return AML_E_BADCODE;
            val |= (uint64_t)b << (8 * i);
        }
    } else if (r->space == 3) {   /* EmbeddedControl：只读，逐字节走 EC 事务 */
        uint64_t ea = r->io_off + (uint64_t)(f->bit_off / 8);
        if (ea + nbytes > 0x100) return AML_E_UNSUP;
        if (r->io_len && ea + nbytes > r->io_len) return AML_E_UNSUP;
        for (i = 0; i < nbytes; i++) {
            uint8_t b = 0;
            if (ec64_read((uint8_t)(ea + i), &b) != 0) return AML_E_UNSUP;
            val |= (uint64_t)b << (8 * i);
        }
    } else {
        return AML_E_UNSUP;
    }
    *out = val;
    return AML_E_OK;
}

/* 仅服务 IndexField/BankField 的 index/bank 寄存器内部写。
 * 只支持 SystemIO/SystemMemory；EC 写事务未实现，明确 UNSUP，绝不写旧值/0 假装成功。 */
static int reg_write_raw(aml_node_t *f, aml_node_t *r, uint32_t nbytes, uint64_t value)
{
    uint32_t i;

    if (nbytes == 0 || nbytes > 4) return AML_E_UNSUP;
    if (r->space == 3) return AML_E_UNSUP;
    if (r->space != 1 && r->space != 0) return AML_E_UNSUP;
    if (nbytes == 1 && value > 0xFF) return AML_E_BADCODE;
    if (nbytes == 2 && value > 0xFFFF) return AML_E_BADCODE;
    if (nbytes == 4 && value > 0xFFFFFFFFULL) return AML_E_BADCODE;

    if (r->space == 1) {
        uint64_t port = r->io_off + (uint64_t)(f->bit_off / 8);
        if (port > 0xFFFF) return AML_E_BADCODE;
        if (r->io_len && port - r->io_off + nbytes > r->io_len) return AML_E_BADCODE;
        for (i = 0; i < nbytes; i++)
            aml_port_out((uint16_t)(port + i), (uint8_t)(value >> (8 * i)));
    } else {
        uint64_t phys = r->io_off + (uint64_t)(f->bit_off / 8);
        if (r->io_len && phys - r->io_off + nbytes > r->io_len) return AML_E_BADCODE;
        for (i = 0; i < nbytes; i++) {
            if (mem_wr8(phys + i, (uint8_t)(value >> (8 * i))) != 0) return AML_E_BADCODE;
        }
    }
    return AML_E_OK;
}

/* 普通 Field：从已解析的 region 读取并按 bit 偏移/宽度抽取。 */
static int plain_field_read_at(aml_node_t *f, aml_node_t *r, aml_value_t **out)
{
    uint64_t val = 0;
    uint32_t bytes, shift;
    int rc;

    bytes = (uint32_t)((f->bit_off & 7) + f->bit_len + 7) / 8;
    if (bytes > 8) return AML_E_UNSUP;
    shift = (uint32_t)(f->bit_off & 7);

    rc = reg_read_raw(f, r, bytes, &val);
    if (rc != 0) return rc;

    val >>= shift;
    if (f->bit_len < 64) val &= ((1ULL << f->bit_len) - 1ULL);
    *out = v_int(&g_eval, (int64_t)val);
    return *out ? AML_E_OK : AML_E_LIMIT;
}

static int plain_field_read(aml_node_t *f, aml_value_t **out)
{
    aml_node_t *r;
    int rc = field_region(f, &r);
    if (rc != 0) return rc;
    return plain_field_read_at(f, r, out);
}

/* IndexField：先写 IndexName = granule 序号，再读 DataName，逐 granule 拼装抽取。 */
static int index_field_read(aml_node_t *f, aml_value_t **out)
{
    aml_node_t *df, *dr, *cf, *cr;
    int didx, cidx;
    uint32_t dgb, cgb, dbits;
    int32_t bo = f->bit_off, bl = f->bit_len;
    uint32_t gran0, off_in, ngran, g;
    uint64_t accum = 0;
    int rc;

    didx = ns_find(f->data_path);
    if (didx < 0) return AML_E_NOTFOUND;
    df = &g_nodes[didx];
    if (df->kind != NS_FIELD) return AML_E_BADCODE;
    cidx = ns_find(f->ctrl_path);
    if (cidx < 0) return AML_E_NOTFOUND;
    cf = &g_nodes[cidx];
    if (cf->kind != NS_FIELD) return AML_E_BADCODE;

    /* granule 宽度由 DataName / IndexName 字段自身宽度决定（8/16/32 位）。 */
    dgb = fld_width_bytes(df->bit_len);
    if (dgb == 0) return AML_E_UNSUP;
    cgb = fld_width_bytes(cf->bit_len);
    if (cgb == 0) return AML_E_UNSUP;
    dbits = dgb * 8;

    rc = field_region(df, &dr);
    if (rc != 0) return rc;
    rc = field_region(cf, &cr);
    if (rc != 0) return rc;
    /* EC 上没有写事务：index/data 寄存器落在 EC 一律 fail-closed。 */
    if (dr->space == 3 || cr->space == 3) return AML_E_UNSUP;
    if (dr->space != 1 && dr->space != 0) return AML_E_UNSUP;
    if (cr->space != 1 && cr->space != 0) return AML_E_UNSUP;

    gran0 = (uint32_t)(bo / dbits);
    off_in = (uint32_t)(bo % dbits);
    ngran = (uint32_t)((off_in + (uint32_t)bl + dbits - 1) / dbits);
    if ((uint64_t)ngran * dbits > 64) return AML_E_UNSUP;

    for (g = 0; g < ngran; g++) {
        uint64_t idxval = (uint64_t)gran0 + g;
        uint64_t dval = 0;
        rc = reg_write_raw(cf, cr, cgb, idxval);
        if (rc != 0) return rc;
        rc = reg_read_raw(df, dr, dgb, &dval);
        if (rc != 0) return rc;
        dval >>= (uint32_t)(df->bit_off & 7);
        if (dbits < 64) dval &= (1ULL << dbits) - 1ULL;
        accum |= dval << (g * dbits);
    }
    accum >>= off_in;
    if (bl < 64) accum &= ((1ULL << bl) - 1ULL);
    *out = v_int(&g_eval, (int64_t)accum);
    return *out ? AML_E_OK : AML_E_LIMIT;
}

/* BankField：先写 BankName = BankValue，再按 RegionName + 偏移普通读取。 */
static int bank_field_read(aml_node_t *f, aml_value_t **out)
{
    aml_node_t *bf, *br, *rr;
    int bidx;
    uint32_t bgb;
    int rc;

    bidx = ns_find(f->ctrl_path);
    if (bidx < 0) return AML_E_NOTFOUND;
    bf = &g_nodes[bidx];
    if (bf->kind != NS_FIELD) return AML_E_BADCODE;

    bgb = fld_width_bytes(bf->bit_len);
    if (bgb == 0) return AML_E_UNSUP;

    rc = field_region(f, &rr);
    if (rc != 0) return rc;
    rc = field_region(bf, &br);
    if (rc != 0) return rc;
    /* bank 寄存器在 EC 上同样没有写事务，fail-closed。 */
    if (br->space == 3) return AML_E_UNSUP;
    if (br->space != 1 && br->space != 0) return AML_E_UNSUP;

    rc = reg_write_raw(bf, br, bgb, f->bank_val);
    if (rc != 0) return rc;

    /* region 自身可为 SystemIO/SystemMemory/EC（EC 只读路径已支持）。 */
    return plain_field_read_at(f, rr, out);
}

static int field_read(exec_t *e, int idx, aml_value_t **out)
{
    (void)e;
    aml_node_t *f = &g_nodes[idx];

    if (f->bit_len <= 0 || f->bit_len > 64) return AML_E_UNSUP;

    if (f->ftype == FIELD_INDEX) return index_field_read(f, out);
    if (f->ftype == FIELD_BANK)  return bank_field_read(f, out);
    return plain_field_read(f, out);
}

static int node_read(exec_t *e, int idx, aml_value_t **out)
{
    aml_node_t *n;
    if (idx < 0 || idx >= g_nnodes) return AML_E_NOTFOUND;
    n = &g_nodes[idx];
    if (n->kind == NS_METHOD) return call_method(e, idx, 0, 0, out);
    if (n->kind == NS_FIELD) return field_read(e, idx, out);
    if (n->kind == NS_BFIELD) {
        uint64_t q = 0;
        int rc = bfield_extract(n->val ? n->val->base : 0,
                                n->bit_off, n->bit_len, &q);
        if (rc != 0) return rc;
        *out = v_int(&g_eval, (int64_t)q);
        return *out ? AML_E_OK : AML_E_LIMIT;
    }
    if (n->kind == NS_NAME) {
        aml_value_t *v = node_value_cur(idx);
        if (!v) {
            /* OperationRegion 本身不是数值 */
            if (n->io_len) return AML_E_ARG;
            return AML_E_NOTFOUND;
        }
        *out = v;
        return AML_E_OK;
    }
    return AML_E_ARG;
}

/* 目标引用 */
enum { PL_LOCAL = 0, PL_ARG, PL_NODE, PL_BUF, PL_PKG, PL_BFIELD };
typedef struct {
    int kind;
    int idx;
    aml_value_t *base;
    uint32_t eidx;
    uint32_t fbit_off;
    uint32_t fbit_len;
} place_t;

static int ev_place(exec_t *e, const uint8_t **pp, const uint8_t *end, place_t *pl)
{
    const uint8_t *p = *pp;

    if (p >= end) return AML_E_BADCODE;
    if (p[0] >= 0x60 && p[0] <= 0x67) {
        pl->kind = PL_LOCAL; pl->idx = p[0] - 0x60; *pp = p + 1; return AML_E_OK;
    }
    if (p[0] >= 0x68 && p[0] <= 0x6E) {
        pl->kind = PL_ARG; pl->idx = p[0] - 0x68; *pp = p + 1; return AML_E_OK;
    }
    if (p[0] == 0x00) {   /* Null target: 允许，丢弃 */
        pl->kind = PL_NODE; pl->idx = -1; *pp = p + 1; return AML_E_OK;
    }
    if (p[0] == 0x88) {   /* Index */
        aml_value_t *base = 0, *ix = 0, *ref = 0;
        int64_t i64 = 0;
        p++;
        if (ev_expr(e, &p, end, &base) != 0) return AML_E_BADCODE;
        if (ev_expr(e, &p, end, &ix) != 0) return AML_E_BADCODE;
        if (to_int(ix, &i64) != 0 || i64 < 0) return AML_E_ARG;
        if (base && base->type == AML_V_BUF) {
            if ((uint32_t)i64 >= base->blen) return AML_E_ARG;
            pl->kind = PL_BUF; pl->base = base; pl->eidx = (uint32_t)i64;
            *pp = p; return AML_E_OK;
        }
        if (base && base->type == AML_V_PKG) {
            if ((uint32_t)i64 >= base->count) return AML_E_ARG;
            pl->kind = PL_PKG; pl->base = base; pl->eidx = (uint32_t)i64;
            *pp = p; return AML_E_OK;
        }
        (void)ref;
        return AML_E_ARG;
    }
    if (p[0] == 0x83) {   /* DerefOf */
        aml_value_t *operand = 0;
        p++;
        if (ev_expr(e, &p, end, &operand) != 0) return AML_E_BADCODE;
        if (!operand || operand->type != AML_V_REF || !operand->base) return AML_E_ARG;
        if (operand->base->type == AML_V_BUF) {
            pl->kind = PL_BUF; pl->base = operand->base; pl->eidx = operand->index;
            *pp = p; return AML_E_OK;
        }
        if (operand->base->type == AML_V_PKG) {
            pl->kind = PL_PKG; pl->base = operand->base; pl->eidx = operand->index;
            *pp = p; return AML_E_OK;
        }
        return AML_E_ARG;
    }
    if ((p[0] >= 'A' && p[0] <= 'Z') || p[0] == '_' ||
        p[0] == 0x5C || p[0] == 0x5E || p[0] == 0x2E || p[0] == 0x2F) {
        namestr_t ns;
        int idx;
        if (parse_namestr(&p, end, &ns) != 0) return AML_E_BADCODE;
        idx = resolve_node(e, &ns);
        if (idx < 0) return AML_E_NOTFOUND;
        if (g_nodes[idx].kind == NS_FIELD) return AML_E_UNSUP;
        if (g_nodes[idx].kind == NS_BFIELD) {
            pl->kind = PL_BFIELD;
            pl->idx = idx;
            pl->base = g_nodes[idx].val ? g_nodes[idx].val->base : 0;
            pl->eidx = (uint32_t)g_nodes[idx].bit_off;
            pl->fbit_off = (uint32_t)g_nodes[idx].bit_off;
            pl->fbit_len = (uint32_t)g_nodes[idx].bit_len;
            *pp = p;
            return AML_E_OK;
        }
        pl->kind = PL_NODE; pl->idx = idx;
        *pp = p;
        return AML_E_OK;
    }
    return AML_E_BADCODE;
}

static int place_store(place_t *pl, aml_value_t *v)
{
    switch (pl->kind) {
        case PL_LOCAL: return AML_E_OK;
        case PL_ARG: return AML_E_OK;
        case PL_NODE:
            if (pl->idx < 0) return AML_E_OK;
            if (g_overlay_n >= AML_OVERLAY_MAX) return AML_E_LIMIT;
            g_overlay[g_overlay_n].node = pl->idx;
            g_overlay[g_overlay_n].val = v;
            g_overlay_n++;
            return AML_E_OK;
        case PL_BUF: {
            int64_t x = 0;
            if (to_int(v, &x) != 0) return AML_E_ARG;
            pl->base->buf[pl->eidx] = (uint8_t)x;
            return AML_E_OK;
        }
        case PL_PKG:
            if (pl->base->items) pl->base->items[pl->eidx] = v;
            return AML_E_OK;
        case PL_BFIELD: {
            int64_t x = 0;
            if (to_int(v, &x) != 0) return AML_E_ARG;
            return bfield_write(pl->base, (int32_t)pl->fbit_off,
                                 (int32_t)pl->fbit_len, x);
        }
        default: return AML_E_ARG;
    }
}

static void place_set_local(exec_t *e, place_t *pl, aml_value_t *v)
{
    if (pl->kind == PL_LOCAL) e->locals[pl->idx] = v;
    else if (pl->kind == PL_ARG) e->args[pl->idx] = v;
}

/* ---- 运行期 Create*Field 绑定的 Buffer 位段引用 -------------------------- */

static int bfield_bounds(aml_value_t *buf, int32_t bo, int32_t bl)
{
    if (!buf || buf->type != AML_V_BUF || !buf->buf) return AML_E_ARG;
    if (bo < 0 || bl <= 0) return AML_E_ARG;
    if (bl > 64) return AML_E_UNSUP;
    if ((uint64_t)bo + (uint64_t)bl > (uint64_t)buf->blen * 8) return AML_E_ARG;
    /* 抽取/回写落在单个 64 位窗口内；非对齐跨 64 位窗口的位段拒绝。 */
    if ((bo & 7) + bl > 64) return AML_E_UNSUP;
    return AML_E_OK;
}

static int bfield_extract(aml_value_t *buf, int32_t bo, int32_t bl, uint64_t *out)
{
    uint32_t first, last, i;
    uint64_t q = 0;
    if (bfield_bounds(buf, bo, bl) != 0) return AML_E_ARG;
    first = (uint32_t)bo / 8;
    last = (uint32_t)(bo + bl - 1) / 8;
    if (last >= buf->blen) return AML_E_ARG;
    for (i = first; i <= last; i++)
        q |= (uint64_t)buf->buf[i] << (8 * (i - first));
    q >>= (bo & 7);
    if (bl < 64) q &= (1ULL << bl) - 1ULL;
    *out = q;
    return AML_E_OK;
}

static int bfield_write(aml_value_t *buf, int32_t bo, int32_t bl, int64_t x)
{
    uint32_t first, last, i;
    uint64_t mask = (bl < 64) ? ((1ULL << bl) - 1ULL) : ~0ULL;
    uint64_t val = (uint64_t)x & mask;
    uint64_t q = 0;
    if (bfield_bounds(buf, bo, bl) != 0) return AML_E_ARG;
    first = (uint32_t)bo / 8;
    last = (uint32_t)(bo + bl - 1) / 8;
    if (last >= buf->blen) return AML_E_ARG;
    for (i = first; i <= last; i++)
        q |= (uint64_t)buf->buf[i] << (8 * (i - first));
    q &= ~(mask << (bo & 7));
    q |= val << (bo & 7);
    for (i = first; i <= last; i++)
        buf->buf[i] = (uint8_t)(q >> (8 * (i - first)));
    return AML_E_OK;
}

static int bfield_bind(exec_t *e, aml_value_t *buf, int64_t bit_off, int32_t bit_len,
                       const namestr_t *ns)
{
    char path[AML_PATH_MAX];
    int idx;
    aml_node_t *nd;
    if (!buf || buf->type != AML_V_BUF) return AML_E_ARG;
    if (bit_off < 0 || bit_len <= 0) return AML_E_ARG;
    if (bit_len > 64) return AML_E_UNSUP;
    if ((uint64_t)bit_off + (uint64_t)bit_len > (uint64_t)buf->blen * 8) return AML_E_ARG;
    if ((bit_off & 7) + bit_len > 64) return AML_E_UNSUP;
    if (path_apply(e->scope, ns, path, AML_PATH_MAX) != 0) return AML_E_BADCODE;
    idx = ns_find(path);
    if (idx < 0) idx = ns_add(path, NS_BFIELD);
    if (idx < 0) return AML_E_LIMIT;
    nd = &g_nodes[idx];
    nd->kind = NS_BFIELD;
    nd->bit_off = (int32_t)bit_off;
    nd->bit_len = bit_len;
    nd->val = v_ref(&g_eval, buf, (uint32_t)bit_off);
    if (!nd->val) return AML_E_LIMIT;
    return AML_E_OK;
}

static int ref_get(aml_value_t *ref, aml_value_t **out)
{
    if (!ref || ref->type != AML_V_REF || !ref->base) return AML_E_ARG;
    if (ref->base->type == AML_V_BUF) {
        if (ref->index >= ref->base->blen) return AML_E_ARG;
        *out = v_int(&g_eval, ref->base->buf[ref->index]);
        return *out ? AML_E_OK : AML_E_LIMIT;
    }
    if (ref->base->type == AML_V_PKG) {
        if (ref->index >= ref->base->count) return AML_E_ARG;
        *out = ref->base->items ? ref->base->items[ref->index] : zero_val();
        if (!*out) *out = zero_val();
        return AML_E_OK;
    }
    return AML_E_ARG;
}

static int truthy(aml_value_t *v)
{
    int64_t x = 0;
    if (!v) return 0;
    if (v->type == AML_V_INT) return v->ival != 0;
    if (v->type == AML_V_BUF) return v->blen != 0;
    (void)x;
    return 1;
}

static int ev_expr(exec_t *e, const uint8_t **pp, const uint8_t *end, aml_value_t **out)
{
    const uint8_t *p = *pp;
    aml_value_t *a = 0, *b = 0;
    int64_t x = 0, y = 0;

    if (p >= end) return AML_E_BADCODE;
    if (++g_steps > AML_STEP_BUDGET) return AML_E_LIMIT;

    switch (p[0]) {
        case 0x00: *out = v_int(&g_eval, 0); *pp = p + 1; return *out ? 0 : AML_E_LIMIT;
        case 0x01: *out = v_int(&g_eval, 1); *pp = p + 1; return *out ? 0 : AML_E_LIMIT;
        case 0xFF: *out = v_int(&g_eval, -1); *pp = p + 1; return *out ? 0 : AML_E_LIMIT;
        case 0x0A:
            if (p + 2 > end) return AML_E_BADCODE;
            *out = v_int(&g_eval, p[1]); *pp = p + 2; return *out ? 0 : AML_E_LIMIT;
        case 0x0B:
            if (p + 3 > end) return AML_E_BADCODE;
            *out = v_int(&g_eval, (int64_t)(p[1] | (p[2] << 8))); *pp = p + 3;
            return *out ? 0 : AML_E_LIMIT;
        case 0x0C:
            if (p + 5 > end) return AML_E_BADCODE;
            *out = v_int(&g_eval, (int64_t)((uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                                           ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24)));
            *pp = p + 5; return *out ? 0 : AML_E_LIMIT;
        case 0x0D:
            {
                int len;
                if (p + 2 > end) return AML_E_BADCODE;
                len = p[1];
                if (p + 3 + len > end) return AML_E_BADCODE;
                *out = v_str(&g_eval, p + 2, len);
                *pp = p + 3 + len;
                return *out ? 0 : AML_E_LIMIT;
            }
        case 0x60: case 0x61: case 0x62: case 0x63:
        case 0x64: case 0x65: case 0x66: case 0x67:
            *out = e->locals[p[0] - 0x60] ? e->locals[p[0] - 0x60] : zero_val();
            *pp = p + 1;
            return 0;
        case 0x68: case 0x69: case 0x6A: case 0x6B:
        case 0x6C: case 0x6D: case 0x6E:
            if (p[0] - 0x68 >= e->argc) {
                *out = zero_val(); *pp = p + 1; return 0;
            }
            *out = e->args[p[0] - 0x68] ? e->args[p[0] - 0x68] : zero_val();
            *pp = p + 1;
            return 0;
        case 0x70:   /* Store：先 Source，再 Destination */
            {
                place_t pl;
                int rc;
                p++;
                rc = ev_expr(e, &p, end, &a);
                if (rc != 0) return rc;
                rc = ev_place(e, &p, end, &pl);
                if (rc != 0) return rc;
                if (pl.kind == PL_LOCAL || pl.kind == PL_ARG) place_set_local(e, &pl, a ? a : zero_val());
                else {
                    rc = place_store(&pl, a ? a : zero_val());
                    if (rc != 0) return rc;
                }
                *out = a ? a : zero_val();
                *pp = p;
                return 0;
            }
        case 0x9D:   /* CopyObject */
            {
                place_t pl;
                int rc;
                p++;
                rc = ev_expr(e, &p, end, &a);
                if (rc != 0) return rc;
                rc = ev_place(e, &p, end, &pl);
                if (rc != 0) return rc;
                if (pl.kind == PL_LOCAL || pl.kind == PL_ARG) place_set_local(e, &pl, a);
                else if ((rc = place_store(&pl, a)) != 0) return rc;
                *out = a ? a : zero_val();
                *pp = p;
                return 0;
            }
        case 0x11:   /* Buffer */
            {
                const uint8_t *body_end = 0;
                uint32_t want = 0;
                uint32_t avail, n;
                aml_value_t *sizev = 0;
                p++;
                if (pkg_length(&p, end, 0, &body_end) != 0) return AML_E_BADCODE;
                if (ev_expr(e, &p, body_end, &sizev) != 0) return AML_E_BADCODE;
                {
                    int64_t sz = 0;
                    if (to_int(sizev, &sz) != 0 || sz < 0) return AML_E_ARG;
                    want = (uint32_t)sz;
                }
                if (want > 4096) want = 4096;
                avail = (uint32_t)(body_end - p);
                n = avail < want ? avail : want;
                a = v_buf(&g_eval, want);
                if (!a) return AML_E_LIMIT;
                if (n && a->buf) memcpy(a->buf, p, n);
                *out = a;
                *pp = body_end;
                return 0;
            }
        case 0x12:   /* Package */
        case 0x13:   /* VarPackage */
            {
                const uint8_t *body_end = 0;
                uint32_t count = 0;
                uint32_t i;
                aml_value_t *pv = 0;
                uint8_t pop = p[0];
                p++;
                if (pkg_length(&p, end, 0, &body_end) != 0) return AML_E_BADCODE;
                if (pop == 0x12) {
                    if (p >= body_end) return AML_E_BADCODE;
                    count = *p++;
                } else {
                    aml_value_t *cntv = 0;
                    int64_t cv = 0;
                    if (ev_expr(e, &p, body_end, &cntv) != 0) return AML_E_BADCODE;
                    if (to_int(cntv, &cv) != 0 || cv < 0) return AML_E_ARG;
                    count = (uint32_t)cv;
                }
                pv = v_pkg(&g_eval, count);
                if (!pv) return AML_E_LIMIT;
                for (i = 0; i < count && p < body_end; i++) {
                    aml_value_t *ev = 0;
                    if (ev_expr(e, &p, body_end, &ev) != 0) return AML_E_BADCODE;
                    if (pv->items) pv->items[i] = ev;
                }
                *out = pv;
                *pp = body_end;
                return 0;
            }
        case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8F:
            {
                int32_t bw;
                uint8_t opc = p[0];
                aml_value_t *buf = 0, *ix = 0;
                int64_t i64 = 0;
                namestr_t ns;
                switch (opc) {
                case 0x8A: bw = 32; break;
                case 0x8B: bw = 16; break;
                case 0x8C: bw = 8; break;
                case 0x8D: bw = 1; break;
                default:   bw = 64; break;
                }
                p++;
                if (ev_expr(e, &p, end, &buf) != 0) return AML_E_BADCODE;
                if (ev_expr(e, &p, end, &ix) != 0) return AML_E_BADCODE;
                if (to_int(ix, &i64) != 0 || i64 < 0) return AML_E_ARG;
                if (parse_namestr(&p, end, &ns) != 0) return AML_E_BADCODE;
                /* DWord/Word/Byte/QWord 索引是字节偏移；BitField 索引是位偏移。 */
                {
                    int brc = bfield_bind(e, buf, opc == 0x8D ? i64 : i64 * 8, bw, &ns);
                    if (brc != 0) return brc;
                }
                *out = buf;
                *pp = p;
                return AML_E_OK;
            }
        case 0x83:   /* DerefOf */
            {
                aml_value_t *ref = 0;
                p++;
                if (ev_expr(e, &p, end, &ref) != 0) return AML_E_BADCODE;
                if (ref && ref->type == AML_V_REF) {
                    int rc = ref_get(ref, out);
                    if (rc != 0) return rc;
                } else {
                    *out = ref ? ref : zero_val();
                }
                *pp = p;
                return 0;
            }
        case 0x87:   /* SizeOf */
            p++;
            if (ev_expr(e, &p, end, &a) != 0) return AML_E_BADCODE;
            if (a && a->type == AML_V_PKG) x = a->count;
            else if (a && (a->type == AML_V_BUF || a->type == AML_V_STR)) x = a->blen;
            else x = 0;
            *out = v_int(&g_eval, x);
            *pp = p;
            return *out ? 0 : AML_E_LIMIT;
        case 0x88:   /* Index */
            {
                int64_t i64 = 0;
                p++;
                if (ev_expr(e, &p, end, &a) != 0) return AML_E_BADCODE;
                if (ev_expr(e, &p, end, &b) != 0) return AML_E_BADCODE;
                if (to_int(b, &i64) != 0 || i64 < 0) return AML_E_ARG;
                if (a && (a->type == AML_V_BUF || a->type == AML_V_PKG)) {
                    *out = v_ref(&g_eval, a, (uint32_t)i64);
                    *pp = p;
                    return *out ? 0 : AML_E_LIMIT;
                }
                return AML_E_ARG;
            }
        case 0x72: case 0x74: case 0x77: case 0x78: case 0x79: case 0x7A:
        case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: case 0x85:
            {
                uint8_t op = p[0];
                p++;
                if (op == 0x78) {   /* Divide: 被除数、除数、余数目标、商目标 */
                    place_t pr, pq;
                    if (ev_expr(e, &p, end, &a) != 0) return AML_E_BADCODE;
                    if (ev_expr(e, &p, end, &b) != 0) return AML_E_BADCODE;
                    if (ev_place(e, &p, end, &pr) != 0) return AML_E_BADCODE;
                    if (ev_place(e, &p, end, &pq) != 0) return AML_E_BADCODE;
                    if (to_int(a, &x) != 0 || to_int(b, &y) != 0) return AML_E_ARG;
                    if (y == 0) return AML_E_ARG;
                    if (pr.kind != PL_NODE || pr.idx >= 0) {
                        aml_value_t *rv = v_int(&g_eval, x % y);
                        if (rv) {
                            if (pr.kind == PL_LOCAL || pr.kind == PL_ARG) place_set_local(e, &pr, rv);
                            else (void)place_store(&pr, rv);
                        }
                    }
                    *out = v_int(&g_eval, x / y);
                    *pp = p;
                    return *out ? 0 : AML_E_LIMIT;
                }
                if (ev_expr(e, &p, end, &a) != 0) return AML_E_BADCODE;
                if (ev_expr(e, &p, end, &b) != 0) return AML_E_BADCODE;
                if (to_int(a, &x) != 0 || to_int(b, &y) != 0) return AML_E_ARG;
                switch (op) {
                    case 0x72: x = x + y; break;
                    case 0x74: x = x - y; break;
                    case 0x77: x = x * y; break;
                    case 0x79: x = (y >= 64 || y < 0) ? 0 : (x << y); break;
                    case 0x7A: x = (y >= 64 || y < 0) ? 0 : (x >> y); break;
                    case 0x7B: x = x & y; break;
                    case 0x7C: x = ~(x & y); break;
                    case 0x7D: x = x | y; break;
                    case 0x7E: x = ~(x | y); break;
                    case 0x7F: x = x ^ y; break;
                    case 0x85: if (y == 0) return AML_E_ARG; x = x % y; break;
                    default: break;
                }
                *out = v_int(&g_eval, x);
                *pp = p;
                return *out ? 0 : AML_E_LIMIT;
            }
        case 0x80:   /* Not */
            p++;
            if (ev_expr(e, &p, end, &a) != 0) return AML_E_BADCODE;
            if (to_int(a, &x) != 0) return AML_E_ARG;
            *out = v_int(&g_eval, ~x);
            *pp = p;
            return *out ? 0 : AML_E_LIMIT;
        case 0x75:   /* Increment */
            {
                place_t pl;
                p++;
                if (ev_place(e, &p, end, &pl) != 0) return AML_E_BADCODE;
                a = (pl.kind == PL_LOCAL) ? e->locals[pl.idx] :
                    (pl.kind == PL_ARG) ? e->args[pl.idx] :
                    (pl.kind == PL_NODE && pl.idx >= 0) ? node_value_cur(pl.idx) : zero_val();
                if (to_int(a, &x) != 0) return AML_E_ARG;
                b = v_int(&g_eval, x + 1);
                if (!b) return AML_E_LIMIT;
                if (pl.kind == PL_LOCAL || pl.kind == PL_ARG) place_set_local(e, &pl, b);
                else if (place_store(&pl, b) != 0) return AML_E_ARG;
                *out = b;
                *pp = p;
                return 0;
            }
        case 0x76:   /* Decrement */
            {
                place_t pl;
                p++;
                if (ev_place(e, &p, end, &pl) != 0) return AML_E_BADCODE;
                a = (pl.kind == PL_LOCAL) ? e->locals[pl.idx] :
                    (pl.kind == PL_ARG) ? e->args[pl.idx] :
                    (pl.kind == PL_NODE && pl.idx >= 0) ? node_value_cur(pl.idx) : zero_val();
                if (to_int(a, &x) != 0) return AML_E_ARG;
                b = v_int(&g_eval, x - 1);
                if (!b) return AML_E_LIMIT;
                if (pl.kind == PL_LOCAL || pl.kind == PL_ARG) place_set_local(e, &pl, b);
                else if (place_store(&pl, b) != 0) return AML_E_ARG;
                *out = b;
                *pp = p;
                return 0;
            }
        case 0x90: case 0x91:   /* LAnd / LOr */
            {
                uint8_t lop = p[0];
                p++;
                if (ev_expr(e, &p, end, &a) != 0) return AML_E_BADCODE;
                if (ev_expr(e, &p, end, &b) != 0) return AML_E_BADCODE;
                {
                    int ta = truthy(a), tb = truthy(b);
                    int r = (lop == 0x90) ? (ta && tb) : (ta || tb);
                    *out = v_int(&g_eval, r ? 1 : 0);
                }
            }
            *pp = p;
            return *out ? 0 : AML_E_LIMIT;
        case 0x92:   /* LNot */
            p++;
            if (ev_expr(e, &p, end, &a) != 0) return AML_E_BADCODE;
            *out = v_int(&g_eval, truthy(a) ? 0 : 1);
            *pp = p;
            return *out ? 0 : AML_E_LIMIT;
        case 0x93: case 0x94: case 0x95:   /* LEqual / LGreater / LLess */
            {
                uint8_t op = p[0];
                int r = 0;
                p++;
                if (ev_expr(e, &p, end, &a) != 0) return AML_E_BADCODE;
                if (ev_expr(e, &p, end, &b) != 0) return AML_E_BADCODE;
                if (a && b && a->type == AML_V_STR && b->type == AML_V_STR) {
                    int cl = (int)(a->blen < b->blen ? a->blen : b->blen);
                    int cmp = 0;
                    int i;
                    for (i = 0; i < cl; i++) {
                        if (a->buf[i] != b->buf[i]) { cmp = (int)a->buf[i] - (int)b->buf[i]; break; }
                    }
                    if (!cmp) cmp = (int)a->blen - (int)b->blen;
                    if (op == 0x93) r = (cmp == 0);
                    else if (op == 0x94) r = (cmp > 0);
                    else r = (cmp < 0);
                } else {
                    if (to_int(a, &x) != 0 || to_int(b, &y) != 0) return AML_E_ARG;
                    if (op == 0x93) r = (x == y);
                    else if (op == 0x94) r = (x > y);
                    else r = (x < y);
                }
                *out = v_int(&g_eval, r ? 1 : 0);
                *pp = p;
                return *out ? 0 : AML_E_LIMIT;
            }
        case 0x99:   /* ToInteger */
            p++;
            if (ev_expr(e, &p, end, &a) != 0) return AML_E_BADCODE;
            if (a && a->type == AML_V_STR && a->buf) {
                int i = 0;
                uint64_t q = 0;
                while (i < (int)a->blen && a->buf[i] >= '0' && a->buf[i] <= '9') {
                    q = q * 10 + (uint64_t)(a->buf[i] - '0');
                    i++;
                }
                *out = v_int(&g_eval, (int64_t)q);
            } else {
                if (to_int(a, &x) != 0) return AML_E_ARG;
                *out = v_int(&g_eval, x);
            }
            *pp = p;
            return *out ? 0 : AML_E_LIMIT;
        case 0x5B:
            if (p + 1 >= end) return AML_E_BADCODE;
            if (p[1] == 0x12) {   /* CondRefOf */
                namestr_t ns;
                place_t pl;
                int idx;
                p += 2;
                if (parse_namestr(&p, end, &ns) != 0) return AML_E_BADCODE;
                idx = resolve_node(e, &ns);
                if (ev_place(e, &p, end, &pl) != 0) return AML_E_BADCODE;
                *out = v_int(&g_eval, idx >= 0 ? 1 : 0);
                *pp = p;
                return *out ? 0 : AML_E_LIMIT;
            }
            if (p[1] == 0x30) {   /* Revision */
                *out = v_int(&g_eval, 2);
                *pp = p + 2;
                return *out ? 0 : AML_E_LIMIT;
            }
            if (p[1] == 0x31) {   /* Debug */
                *out = v_int(&g_eval, 0);
                *pp = p + 2;
                return *out ? 0 : AML_E_LIMIT;
            }
            if (p[1] == 0x13) {   /* CreateField(SourceBuf, BitIndex, BitWidth, Name) */
                aml_value_t *buf = 0, *ix = 0, *bw = 0;
                int64_t i64 = 0, w64 = 0;
                namestr_t ns;
                p += 2;
                if (ev_expr(e, &p, end, &buf) != 0) return AML_E_BADCODE;
                if (ev_expr(e, &p, end, &ix) != 0) return AML_E_BADCODE;
                if (ev_expr(e, &p, end, &bw) != 0) return AML_E_BADCODE;
                if (to_int(ix, &i64) != 0 || to_int(bw, &w64) != 0 ||
                    i64 < 0 || w64 <= 0) return AML_E_ARG;
                if (parse_namestr(&p, end, &ns) != 0) return AML_E_BADCODE;
                {
                    int brc = bfield_bind(e, buf, i64, (int32_t)w64, &ns);
                    if (brc != 0) return brc;
                }
                *out = buf;
                *pp = p;
                return AML_E_OK;
            }
            if (p[1] == 0x21 || p[1] == 0x22) {   /* Stall / Sleep：有界占位 */
                aml_value_t *d = 0;
                p += 2;
                if (ev_expr(e, &p, end, &d) != 0) return AML_E_BADCODE;
                /* 单线程采样路径不做真实墙钟等待，避免阻塞调度；参数仅解析不等待。 */
                *out = v_int(&g_eval, 0);
                *pp = p;
                return *out ? AML_E_OK : AML_E_LIMIT;
            }
            if (p[1] == 0x23) {   /* Acquire (Mutex, Timeout) */
                namestr_t ns;
                uint64_t timeout = 0;
                int idx;
                p += 2;
                if (parse_namestr(&p, end, &ns) != 0) return AML_E_BADCODE;
                idx = resolve_node(e, &ns);
                /* Timeout 是 WordData；这里只要求语法完整，不把它当墙钟等待。 */
                if (parse_const_int(&p, end, &timeout) != 0) return AML_E_BADCODE;
                (void)timeout;
                /* 单线程采样路径没有并发竞争者；Acquire 作为本地互斥占位，
                 * 返回 1（成功）而不是假装真的等到了超时。 */
                *out = v_int(&g_eval, idx >= 0 ? 1 : 0);
                *pp = p;
                return *out ? 0 : AML_E_LIMIT;
            }
            if (p[1] == 0x27) {   /* Release (Mutex) */
                namestr_t ns;
                int idx;
                p += 2;
                if (parse_namestr(&p, end, &ns) != 0) return AML_E_BADCODE;
                idx = resolve_node(e, &ns);
                (void)idx;
                *out = v_int(&g_eval, 1);
                *pp = p;
                return *out ? 0 : AML_E_LIMIT;
            }
            return AML_E_UNSUP;
        default:
            if ((p[0] >= 'A' && p[0] <= 'Z') || p[0] == '_' ||
                p[0] == 0x5C || p[0] == 0x5E || p[0] == 0x2E || p[0] == 0x2F) {
                namestr_t ns;
                int idx;
                aml_value_t *args[7];
                int nargs = 0;
                aml_node_t *n;
                if (parse_namestr(&p, end, &ns) != 0) return AML_E_BADCODE;
                /* _OSI 是 ACPI 预定义方法，不一定要在 DSDT 里显式定义。
                 * 本机不冒充 Windows：对所有查询返回 0，让固件走通用/非 Windows
                 * 分支；至少不会因为命名空间缺 _OSI 让整个 _BST 失败。 */
                if (namestr_last_is(&ns, "_OSI")) {
                    aml_value_t *osi_arg = 0;
                    if (ev_expr(e, &p, end, &osi_arg) != 0) return AML_E_BADCODE;
                    (void)osi_arg;
                    *out = v_int(&g_eval, 0);
                    *pp = p;
                    return *out ? 0 : AML_E_LIMIT;
                }
                idx = resolve_node(e, &ns);
                if (idx < 0) return AML_E_NOTFOUND;
                n = &g_nodes[idx];
                if (n->kind == NS_METHOD) {
                    nargs = n->argc;
                    if (nargs > 7) return AML_E_BADCODE;
                    for (int i = 0; i < nargs; i++) {
                        if (ev_expr(e, &p, end, &args[i]) != 0) return AML_E_BADCODE;
                    }
                    if (call_method(e, idx, args, nargs, out) != 0) return AML_E_BADCODE;
                } else {
                    int rc = node_read(e, idx, out);
                    if (rc != 0) return rc;
                }
                *pp = p;
                return 0;
            }
            return AML_E_UNSUP;
    }
}

static int exec_terms(exec_t *e, const uint8_t *p, const uint8_t *end)
{
    while (p < end) {
        if (e->returned || e->err) break;

        if (*p == 0x08) {   /* 方法内动态 Name：不支持，明确失败 */
            e->err = AML_E_UNSUP;
            break;
        }
        if (*p == 0xA0) {   /* If */
            const uint8_t *body_end = 0;
            aml_value_t *cond = 0;
            p++;
            if (pkg_length(&p, end, 0, &body_end) != 0) { e->err = AML_E_BADCODE; break; }
            if (ev_expr(e, &p, end, &cond) != 0) { e->err = AML_E_BADCODE; break; }
            if (truthy(cond)) {
                exec_terms(e, p, body_end);
                if (e->err) break;
            } else {
                if (body_end < end && *body_end == 0xA1) {
                    const uint8_t *else_end = 0;
                    const uint8_t *q = body_end + 1;
                    if (pkg_length(&q, end, 0, &else_end) != 0) { e->err = AML_E_BADCODE; break; }
                    exec_terms(e, q, else_end);
                }
            }
            /* If 为真但没进 else：后面紧跟的 Else 也要吃掉 */
            if (body_end < end && *body_end == 0xA1) {
                const uint8_t *else_end = 0;
                const uint8_t *q = body_end + 1;
                if (pkg_length(&q, end, 0, &else_end) != 0) { e->err = AML_E_BADCODE; break; }
                p = else_end;
            } else {
                p = body_end;
            }
            continue;
        }
        if (*p == 0xA1) {   /* 孤立的 Else：跳过 */
            const uint8_t *body_end = 0;
            p++;
            if (pkg_length(&p, end, 0, &body_end) != 0) { e->err = AML_E_BADCODE; break; }
            p = body_end;
            continue;
        }
        if (*p == 0xA2) {   /* While */
            const uint8_t *body_end = 0;
            aml_value_t *cond = 0;
            int guard = 0;
            p++;
            if (pkg_length(&p, end, 0, &body_end) != 0) { e->err = AML_E_BADCODE; break; }
            while (1) {
                const uint8_t *cp = p;
                if (ev_expr(e, &cp, body_end, &cond) != 0) { e->err = AML_E_BADCODE; break; }
                if (!truthy(cond)) break;
                exec_terms(e, cp, body_end);
                if (e->err || e->returned) break;
                if (++guard > 64) { e->err = AML_E_LIMIT; break; }
            }
            p = body_end;
            continue;
        }
        if (*p == 0xA4) {   /* Return */
            aml_value_t *v = 0;
            p++;
            if (ev_expr(e, &p, end, &v) != 0) { e->err = AML_E_BADCODE; break; }
            e->ret = v ? v : zero_val();
            e->returned = 1;
            continue;
        }
        if (*p == 0xA5) {   /* Break：没有 While 支持，直接判不支持 */
            e->err = AML_E_UNSUP;
            break;
        }
        if (*p == 0xA3 || *p == 0xCC) { p++; continue; }   /* Noop / BreakPoint */

        {
            aml_value_t *v = 0;
            const uint8_t *q = p;
            int rc = ev_expr(e, &q, end, &v);
            if (rc != 0) { e->err = rc; break; }
            p = q;
        }
    }
    return e->err;
}

/* ------------------------------------------------------------------------- */
/* 公共 API                                                                   */
/* ----------------------------------------------------------------- */

#ifdef AML_HOST_TEST
/* 仅主机测试：允许在同一进程里重建命名空间，验证不同夹具组合。
 * 生产内核只建一次表，不暴露这个入口。 */
extern void free(void *ptr);

void aml_ns_reset(void)
{
    if (g_nodes) free(g_nodes);
    if (g_persist.mem) free(g_persist.mem);
    if (g_eval.mem) free(g_eval.mem);
    g_ready = 0;
    g_nnodes = 0;
    g_nodes = 0;
    g_persist.mem = 0;
    g_persist.size = 0;
    g_persist.used = 0;
    g_eval.mem = 0;
    g_eval.size = 0;
    g_eval.used = 0;
    g_build_nnodes = 0;
    g_unknown_skipped = 0;
    g_stop_off = -1;
    g_stop_rc = 0;
}
#endif

int aml_ns_build(const uint8_t *const *tables, const uint32_t *lens, int count)
{
    int i;

    if (g_ready) return 0;
    if (!tables || !lens || count <= 0) return -1;

    g_nodes = (aml_node_t *)kmalloc((uint32_t)(sizeof(aml_node_t) * AML_MAX_NODES));
    if (!g_nodes) return -2;
    memset(g_nodes, 0, sizeof(aml_node_t) * AML_MAX_NODES);
    g_nnodes = 0;
    g_degraded = 0;
    g_unknown_skipped = 0;
    g_stop_off = -1;
    g_stop_rc = 0;

    g_persist.mem = (uint8_t *)kmalloc(AML_PERSIST_SIZE);
    g_persist.size = AML_PERSIST_SIZE;
    g_persist.used = 0;
    g_eval.mem = (uint8_t *)kmalloc(AML_EVAL_SIZE);
    g_eval.size = AML_EVAL_SIZE;
    g_eval.used = 0;
    if (!g_persist.mem || !g_eval.mem) return -2;

    for (i = 0; i < count && i < 24; i++) {
        const uint8_t *p = tables[i];
        const uint8_t *body;
        const uint8_t *end;
        if (!p || lens[i] < 36) continue;
        if (memcmp(p, "DSDT", 4) != 0 && memcmp(p, "SSDT", 4) != 0) continue;
        g_table_base = p;
        body = p + 36;
        end = p + lens[i];
        (void)parse_termlist(&body, end, "", 0);
    }

    g_build_nnodes = g_nnodes;
    g_ready = 1;
    vga_puts("[AML] tables=");
    vga_put_hex((uint64_t)count);
    vga_puts(" nodes=");
    vga_put_hex((uint64_t)g_nnodes);
    vga_puts(" degraded=");
    vga_put_hex((uint64_t)g_degraded);
    vga_puts("\n");
    return 0;
}

int aml_ns_ready(void) { return g_ready; }
int aml_ns_nodes(void) { return g_nnodes; }

int aml_ns_diag(int *degraded, int *unknown_skipped, int *stop_off)
{
    if (degraded) *degraded = g_degraded;
    if (unknown_skipped) *unknown_skipped = g_unknown_skipped;
    if (stop_off) *stop_off = g_stop_off;
    return 0;
}

int aml_find_device_next(uint32_t eisa_id, const char *hid_str, char *out, int cap,
                         int *cursor)
{
    int i;
    if (!g_ready) return AML_E_NOTFOUND;
    if (!cursor || *cursor < 0) return AML_E_NOTFOUND;
    for (i = *cursor; i < g_nnodes; i++) {
        char hid_path[AML_PATH_MAX];
        int used, j;
        aml_value_t *v;
        if (g_nodes[i].kind != NS_SCOPE) continue;
        used = (int)astr_len(g_nodes[i].path);
        if (used + 5 >= AML_PATH_MAX) continue;
        astr_cpy(hid_path, g_nodes[i].path, AML_PATH_MAX);
        hid_path[used] = '.';
        hid_path[used + 1] = '_';
        hid_path[used + 2] = 'H';
        hid_path[used + 3] = 'I';
        hid_path[used + 4] = 'D';
        hid_path[used + 5] = 0;
        j = ns_find(hid_path);
        if (j < 0) continue;
        v = node_value(j);
        if (!v) continue;
        if (v->type == AML_V_INT && (uint32_t)v->ival == eisa_id) {
            astr_cpy(out, g_nodes[i].path, cap);
            *cursor = i + 1;
            return 0;
        }
        if (v->type == AML_V_STR && hid_str && v->buf && astr_cmp((const char *)v->buf, hid_str) == 0) {
            astr_cpy(out, g_nodes[i].path, cap);
            *cursor = i + 1;
            return 0;
        }
    }
    *cursor = g_nnodes;
    return AML_E_NOTFOUND;
}

int aml_find_device(uint32_t eisa_id, const char *hid_str, char *out, int cap)
{
    int cursor = 0;
    return aml_find_device_next(eisa_id, hid_str, out, cap, &cursor);
}

int aml_eval_path(const char *path, aml_value_t **out)
{
    char clean[AML_PATH_MAX];
    int idx;
    exec_t e;

    if (!g_ready || !path || !out) return AML_E_NOTFOUND;
    if (path[0] == '\\') path++;
    astr_cpy(clean, path, AML_PATH_MAX);
    idx = ns_find(clean);
    if (idx < 0) return AML_E_NOTFOUND;

    memset(&e, 0, sizeof(e));
    astr_cpy(e.scope, clean, AML_PATH_MAX);
    path_strip_last(e.scope);
    g_steps = 0;
    {
        int rc = node_read(&e, idx, out);
        if (rc != 0) return rc;
    }
    return AML_E_OK;
}

int aml_eval_path_ints(const char *path, const int64_t *args, int nargs,
                       aml_value_t **out)
{
    char clean[AML_PATH_MAX];
    aml_value_t *argv[7];
    int idx, i;
    exec_t e;

    if (!g_ready || !path || !out) return AML_E_NOTFOUND;
    if (nargs < 0 || nargs > 7 || (nargs && !args)) return AML_E_ARG;
    if (path[0] == '\\') path++;
    astr_cpy(clean, path, AML_PATH_MAX);
    idx = ns_find(clean);
    if (idx < 0) return AML_E_NOTFOUND;
    if (g_nodes[idx].kind != NS_METHOD) return AML_E_BADCODE;

    for (i = 0; i < nargs; i++) {
        argv[i] = v_int(&g_eval, args[i]);
        if (!argv[i]) return AML_E_LIMIT;
    }
    memset(&e, 0, sizeof(e));
    astr_cpy(e.scope, clean, AML_PATH_MAX);
    path_strip_last(e.scope);
    g_steps = 0;
    return call_method(&e, idx, argv, nargs, out);
}

uint8_t aml_value_type(const aml_value_t *v) { return v ? v->type : 0xFF; }

int aml_value_int(const aml_value_t *v, int64_t *out)
{
    if (!v || v->type != AML_V_INT) return -1;
    if (out) *out = v->ival;
    return 0;
}

int aml_value_count(const aml_value_t *v)
{
    if (!v || v->type != AML_V_PKG) return -1;
    return (int)v->count;
}

const aml_value_t *aml_value_at(const aml_value_t *v, int i)
{
    if (!v || v->type != AML_V_PKG || !v->items) return 0;
    if (i < 0 || (uint32_t)i >= v->count) return 0;
    return v->items[i];
}

int aml_value_str(const aml_value_t *v, const char **s, int *len)
{
    if (!v || v->type != AML_V_STR || !v->buf) return -1;
    if (s) *s = (const char *)v->buf;
    if (len) *len = (int)v->blen;
    return 0;
}

int aml_sample_begin(void)
{
    if (!g_ready) return -1;
    /* 运行期 Create*Field 绑定的 BFIELD 节点只属于本次采样；截断回建库基线，
     * 避免上一轮采样的 Buffer 引用悬留给下一轮。 */
    if (g_build_nnodes > 0 && g_nnodes > g_build_nnodes) g_nnodes = g_build_nnodes;
    if (g_eval.mem) g_eval.used = 0;
    g_overlay_n = 0;
    g_steps = 0;
    return 0;
}

void aml_sample_end(void)
{
    g_overlay_n = 0;
}
