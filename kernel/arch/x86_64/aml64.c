/* ============================================================================
 * 赫普OS - 极简 ACPI AML 解释器（M2）
 * 设计边界见 aml64.h 顶部注释。这里只实现电池读数需要的那一小块。
 * ========================================================================== */

#include "aml64.h"

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

enum { NS_EMPTY = 0, NS_SCOPE = 1, NS_NAME = 2, NS_METHOD = 3, NS_FIELD = 4 };

typedef struct {
    char path[AML_PATH_MAX];
    char region_path[AML_PATH_MAX];
    uint8_t kind;
    uint8_t argc;
    uint8_t space;
    int16_t region_node;
    int32_t bit_off;
    int32_t bit_len;
    uint64_t io_off;
    uint64_t io_len;
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
                           const char *region_path)
{
    const uint8_t *p = *pp;
    int32_t bit = 0;
    while (p < body_end) {
        uint8_t b0 = *p;
        if (b0 == 0x00) {
            uint32_t gap = 0;
            const uint8_t *ls = p;
            p++;
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
            g_nodes[idx].bit_off = bit;
            g_nodes[idx].bit_len = bits;
            astr_cpy(g_nodes[idx].region_path, region_path, AML_PATH_MAX);
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
                if (parse_fieldlist(&p, body_end, scope, region) != 0) return -1;
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
            } else if (ext == 0x86 || ext == 0x87) {   /* IndexField / BankField */
                const uint8_t *body_end = 0;
                if (pkg_length(&p, end, 0, &body_end) != 0) return -1;
                p = body_end;
            } else {
                /* 其它扩展对象（CreateField/CondRefOf 等）不在建库期出现则跳过 */
                return -1;
            }
        } else {
            /* 未识别的顶层对象：停止本表扫描，保住宅已解析的部分 */
            return -1;
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

static uint8_t aml_port_in(uint16_t port)
{
    uint8_t v;
    __asm__ __volatile__("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static int field_read(exec_t *e, int idx, aml_value_t **out)
{
    (void)e;
    aml_node_t *f = &g_nodes[idx];
    int ridx;
    aml_node_t *r;
    uint64_t val = 0;
    uint32_t bytes, i;
    uint32_t shift;

    if (f->bit_len <= 0 || f->bit_len > 64) return AML_E_UNSUP;
    ridx = ns_find(f->region_path);
    if (ridx < 0 && f->region_node >= 0) ridx = f->region_node;
    if (ridx < 0) return AML_E_NOTFOUND;
    r = &g_nodes[ridx];
    if (r->kind != NS_NAME || r->io_len == 0) return AML_E_BADCODE;

    bytes = (uint32_t)((f->bit_off & 7) + f->bit_len + 7) / 8;
    if (bytes > 8) return AML_E_UNSUP;
    shift = (uint32_t)(f->bit_off & 7);

    if (r->space == 1) {   /* SystemIO */
        uint64_t port = r->io_off + (uint64_t)(f->bit_off / 8);
        if (port > 0xFFFF) return AML_E_BADCODE;
        for (i = 0; i < bytes; i++)
            val |= (uint64_t)aml_port_in((uint16_t)(port + i)) << (8 * i);
    } else if (r->space == 0) {   /* SystemMemory */
        extern uint64_t vmm64_phys_to_ptr(uint64_t phys);
        uint64_t phys = r->io_off + (uint64_t)(f->bit_off / 8);
        const uint8_t *p = (const uint8_t *)vmm64_phys_to_ptr(phys);
        if (!p) return AML_E_BADCODE;
        for (i = 0; i < bytes; i++) val |= (uint64_t)p[i] << (8 * i);
    } else {
        return AML_E_UNSUP;   /* EmbeddedControl 等留给 M2-full */
    }

    val >>= shift;
    if (f->bit_len < 64) val &= ((1ULL << f->bit_len) - 1ULL);
    *out = v_int(&g_eval, (int64_t)val);
    return *out ? AML_E_OK : AML_E_LIMIT;
}

static int node_read(exec_t *e, int idx, aml_value_t **out)
{
    aml_node_t *n;
    if (idx < 0 || idx >= g_nnodes) return AML_E_NOTFOUND;
    n = &g_nodes[idx];
    if (n->kind == NS_METHOD) return call_method(e, idx, 0, 0, out);
    if (n->kind == NS_FIELD) return field_read(e, idx, out);
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
enum { PL_LOCAL = 0, PL_ARG, PL_NODE, PL_BUF, PL_PKG };
typedef struct {
    int kind;
    int idx;
    aml_value_t *base;
    uint32_t eidx;
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
        default: return AML_E_ARG;
    }
}

static void place_set_local(exec_t *e, place_t *pl, aml_value_t *v)
{
    if (pl->kind == PL_LOCAL) e->locals[pl->idx] = v;
    else if (pl->kind == PL_ARG) e->args[pl->idx] = v;
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
        body = p + 36;
        end = p + lens[i];
        (void)parse_termlist(&body, end, "", 0);
    }

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

int aml_find_device(uint32_t eisa_id, const char *hid_str, char *out, int cap)
{
    int i;
    if (!g_ready) return AML_E_NOTFOUND;
    for (i = 0; i < g_nnodes; i++) {
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
            return 0;
        }
        if (v->type == AML_V_STR && hid_str && v->buf && astr_cmp((const char *)v->buf, hid_str) == 0) {
            astr_cpy(out, g_nodes[i].path, cap);
            return 0;
        }
    }
    return AML_E_NOTFOUND;
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
    if (g_eval.mem) g_eval.used = 0;
    g_overlay_n = 0;
    g_steps = 0;
    return 0;
}

void aml_sample_end(void)
{
    g_overlay_n = 0;
}
