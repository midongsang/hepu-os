/* ============================================================================
 * 赫普OS - 设备树 (DTB) 解析与硬件自动配置
 * ========================================================================== */

#include <stdint.h>
#include "dtb.h"
#include "vga.h"
#include "string.h"
#include "kstdio.h"

static dtb_node_t nodes[DTB_MAX_NODES];
static int node_count = 0;
static int dtb_parsed = 0;

static uint32_t be32(uint32_t val)
{
    return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) |
           ((val >> 8) & 0xFF00) | ((val >> 24) & 0xFF);
}

static uint64_t be64(uint64_t val)
{
    return ((uint64_t)be32(val & 0xFFFFFFFF) << 32) | be32(val >> 32);
}

int dtb_verify(void *dtb_data)
{
    if (!dtb_data) return -1;
    dtb_header_t *hdr = (dtb_header_t *)dtb_data;
    if (be32(hdr->magic) != DTB_MAGIC) {
        vga_puts("[DTB] error: magic mismatch \n");
        return -1;
    }
    /* V3.25: 完整结构校验——修复 V3.20 记录"仅查魔数"限制 */
    uint32_t ts = be32(hdr->total_size);
    uint32_t off_struct = be32(hdr->off_dt_struct);
    uint32_t off_strings = be32(hdr->off_dt_strings);
    uint32_t off_rsv = be32(hdr->off_mem_rsvmap);
    uint32_t sz_struct = be32(hdr->size_dt_struct);
    uint32_t sz_strings = be32(hdr->size_dt_strings);
    uint32_t ver = be32(hdr->version);
    /* 1. totalsize 至少容纳头部（40 字节） */
    if (ts < sizeof(dtb_header_t)) {
        vga_puts("[DTB] error: total_size too small \n");
        return -1;
    }
    /* 2. 版本范围（FDT 支持 1~17；内核解析按常见 v16/v17 语义） */
    if (ver < 1 || ver > 17) {
        vga_printf("[DTB] error: unsupported version %u\n", ver);
        return -1;
    }
    /* 3. 三段偏移必须在数据区内（64 位中间运算防 32 位溢出绕过） */
    if ((uint64_t)off_struct + sz_struct > ts ||
        (uint64_t)off_strings + sz_strings > ts ||
        off_rsv > ts - sizeof(dtb_header_t)) {
        vga_puts("[DTB] error: section out of range \n");
        return -1;
    }
    /* 4. 结构体段必须位于字符串段之前（FDT 布局约定） */
    if (off_struct >= off_strings) {
        vga_puts("[DTB] error: struct/strings layout invalid \n");
        return -1;
    }
    return 0;
}

int dtb_parse(void *dtb_data)
{
    if (dtb_verify(dtb_data) != 0) return -1;

    dtb_header_t *hdr = (dtb_header_t *)dtb_data;
    uint32_t struct_off = be32(hdr->off_dt_struct);
    uint32_t strings_off = be32(hdr->off_dt_strings);
    uint32_t sz_struct = be32(hdr->size_dt_struct);
    uint32_t sz_strings = be32(hdr->size_dt_strings);
    uint8_t *struct_base = (uint8_t *)dtb_data + struct_off;
    char *strings_base = (char *)dtb_data + strings_off;

    node_count = 0;
    int depth = 0;
    int parent_stack[32];
    uint32_t offset = 0;

    /* V7.73安全修复: DTB解析边界检查
     * 漏洞T: 嵌套深度无上限 → parent_stack越界写栈
     * 漏洞U: struct段遍历无边界 → 越界读任意内核内存
     * 漏洞V: 节点名/字符串表偏移无校验 → 越界读/死循环 */
    #define DTB_MAX_DEPTH 32
    #define DTB_MAX_NAME_LEN 256

    while (offset + 4 <= sz_struct) {
        uint32_t token = be32(*(uint32_t *)(struct_base + offset));
        offset += 4;

        if (token == DTB_BEGIN_NODE) {
            /* 漏洞T: 检查嵌套深度上限 */
            if (depth >= DTB_MAX_DEPTH) {
                vga_printf("[DTB] error: nesting too deep (%d)\n", depth);
                return -1;
            }
            if (node_count >= DTB_MAX_NODES) break;
            dtb_node_t *node = &nodes[node_count];
            memset(node, 0, sizeof(dtb_node_t));

            /* 漏洞V: 节点名安全读取，防止无终止字符串越界读 */
            char *name = (char *)(struct_base + offset);
            uint32_t remaining = sz_struct - offset;
            int name_len = 0;
            while (name_len < (int)remaining && name_len < DTB_MAX_NAME_LEN && name[name_len] != '\0') {
                name_len++;
            }
            if (name_len >= (int)remaining || name_len >= DTB_MAX_NAME_LEN) {
                vga_puts("[DTB] error: node name not terminated\n");
                return -1;
            }
            strncpy(node->name, name, 63);
            node->name[63] = '\0';
            offset += (name_len + 1 + 3) & ~3;

            node->depth = depth;
            node->parent = (depth > 0) ? parent_stack[depth - 1] : -1;
            if (node->parent >= 0) {
                nodes[node->parent].children[nodes[node->parent].child_count++] = node_count;
            }

            /* 构建路径 */
            if (node->parent >= 0) {
                snprintf(node->path, 128, "%s/%s", nodes[node->parent].path, node->name);
            } else {
                strncpy(node->path, node->name, 127);
                node->path[127] = '\0';
            }

            parent_stack[depth] = node_count;
            depth++;
            node_count++;
        }
        else if (token == DTB_END_NODE) {
            if (depth > 0) depth--;
        }
        else if (token == DTB_PROP) {
            /* 漏洞U: 检查prop头是否在struct段内 */
            if (offset + 8 > sz_struct) {
                vga_puts("[DTB] error: prop header out of range\n");
                return -1;
            }
            uint32_t prop_len = be32(*(uint32_t *)(struct_base + offset));
            uint32_t name_off = be32(*(uint32_t *)(struct_base + offset + 4));
            offset += 8;

            /* 漏洞V: 字符串表偏移校验 */
            if (name_off >= sz_strings) {
                vga_printf("[DTB] error: string offset 0x%x out of range\n", name_off);
                return -1;
            }

            /* 漏洞V: prop_len溢出检查，防止(prop_len+3)&~3溢出为0导致死循环 */
            if (prop_len > 0xFFFFFFFC) {
                vga_printf("[DTB] error: prop_len 0x%x too large\n", prop_len);
                return -1;
            }
            uint32_t prop_aligned = (prop_len + 3) & ~3;

            /* 漏洞U: 检查prop数据是否在struct段内 */
            if (offset + prop_aligned > sz_struct) {
                vga_puts("[DTB] error: prop data out of range\n");
                return -1;
            }

            if (depth > 0 && node_count > 0) {
                dtb_node_t *node = &nodes[parent_stack[depth - 1]];
                if (node->prop_count < 16) {
                    dtb_prop_t *prop = &node->props[node->prop_count++];
                    strncpy(prop->name, strings_base + name_off, 63);
                    prop->name[63] = '\0';
                    prop->length = prop_len;
                    prop->data = struct_base + offset;
                }
            }
            offset += prop_aligned;
        }
        else if (token == DTB_NOP) {
            continue;
        }
        else if (token == DTB_END) {
            break;
        }
        else {
            vga_printf("[DTB] error: unknown token 0x%x at offset 0x%x\n", token, offset - 4);
            return -1;
        }
    }

    /* 漏洞U: 检查是否正常结束（遇到DTB_END），而不是越界后退出 */
    if (offset + 4 > sz_struct) {
        vga_puts("[DTB] warning: reached end of struct block without END token\n");
    }

    dtb_parsed = 1;
    vga_printf("[DTB] parsed: %d node \n", node_count);
    return 0;
}

dtb_node_t *dtb_find_node(const char *path)
{
    if (!dtb_parsed || !path) return NULL;
    for (int i = 0; i < node_count; i++) {
        if (strcmp(nodes[i].path, path) == 0) return &nodes[i];
    }
    return NULL;
}

dtb_prop_t *dtb_find_prop(dtb_node_t *node, const char *name)
{
    if (!node || !name) return NULL;
    for (int i = 0; i < node->prop_count; i++) {
        if (strcmp(node->props[i].name, name) == 0) return &node->props[i];
    }
    return NULL;
}

uint32_t dtb_get_prop_u32(dtb_node_t *node, const char *name, uint32_t def)
{
    dtb_prop_t *prop = dtb_find_prop(node, name);
    if (!prop || prop->length < 4) return def;
    return be32(*(uint32_t *)prop->data);
}

uint64_t dtb_get_prop_u64(dtb_node_t *node, const char *name, uint64_t def)
{
    dtb_prop_t *prop = dtb_find_prop(node, name);
    if (!prop || prop->length < 8) return def;
    return be64(*(uint64_t *)prop->data);
}

const char *dtb_get_prop_string(dtb_node_t *node, const char *name)
{
    dtb_prop_t *prop = dtb_find_prop(node, name);
    if (!prop || prop->length == 0) return NULL;
    return (const char *)prop->data;
}

uint32_t dtb_get_reg(dtb_node_t *node, int index)
{
    dtb_prop_t *prop = dtb_find_prop(node, "reg");
    if (!prop || prop->length < (uint32_t)(index + 1) * 8) return 0;
    return be32(*(uint32_t *)(prop->data + index * 8));
}

int dtb_get_irq(dtb_node_t *node, int index)
{
    dtb_prop_t *prop = dtb_find_prop(node, "interrupts");
    if (!prop || prop->length < (uint32_t)(index + 1) * 4) return -1;
    return (int)be32(*(uint32_t *)(prop->data + index * 4));
}

const char *dtb_get_compatible(dtb_node_t *node)
{
    return dtb_get_prop_string(node, "compatible");
}

int dtb_for_each_node(int (*callback)(dtb_node_t *node, void *data), void *data)
{
    if (!callback) return -1;
    for (int i = 0; i < node_count; i++) {
        int ret = callback(&nodes[i], data);
        if (ret != 0) return ret;
    }
    return 0;
}

int dtb_node_count(void)
{
    return node_count;
}

dtb_node_t *dtb_node_at(int i)
{
    if (i < 0 || i >= node_count) return NULL;
    return &nodes[i];
}

void dtb_dump(void)
{
    vga_puts("[DTB] DTB dump:\n");
    for (int i = 0; i < node_count; i++) {
        for (int d = 0; d < nodes[i].depth; d++) vga_puts("  ");
        vga_printf("%s (%d props)\n", nodes[i].name, nodes[i].prop_count);
    }
}

/* 硬件自动配置: 根据设备树初始化驱动 */
int dtb_auto_config(void)
{
    if (!dtb_parsed) return -1;
    vga_puts("[DTB] start HW autoconfig ...\n");

    int configured = 0;
    for (int i = 0; i < node_count; i++) {
        dtb_node_t *node = &nodes[i];
        const char *compatible = dtb_get_compatible(node);
        if (!compatible) continue;

        if (strcmp(compatible, "ns16550a") == 0) {
            uint32_t base = dtb_get_reg(node, 0);
            int irq = dtb_get_irq(node, 0);
            vga_printf("[DTB] UART @ 0x%x IRQ %d\n", base, irq);
            configured++;
        }
        else if (strcmp(compatible, "gdrom") == 0 || strcmp(compatible, "mmc") == 0) {
            uint32_t base = dtb_get_reg(node, 0);
            vga_printf("[DTB] storage controller @ 0x%x\n", base);
            configured++;
        }
        else if (compatible[0] == 'u' && compatible[1] == 's' && compatible[2] == 'b') {
            uint32_t base = dtb_get_reg(node, 0);
            int irq = dtb_get_irq(node, 0);
            vga_printf("[DTB] USB controller @ 0x%x IRQ %d\n", base, irq);
            configured++;
        }
    }

    vga_printf("[DTB] autoconfig done: %d device \n", configured);
    return configured;
}

int dtb_init(void *dtb_data)
{
    if (dtb_data) {
        if (dtb_parse(dtb_data) != 0) return -1;
    } else {
        dtb_build_builtin();
    }
    dtb_auto_config();
    return 0;
}

/* ============================================================================
 * V4.20-E65: 内置硬件平台设备树 (Hepu 参考平台)
 * dtb_init(NULL) 时构建——描述 CPU/内存/定时器/输入/显示/存储/RTC/电源/触屏
 * ========================================================================== */
static uint8_t dtb_prop_store[DTB_MAX_PROPS][16];
static int dtb_prop_store_used = 0;

static int dtb_add_node(const char *path, const char *name, int parent, int depth)
{
    if (node_count >= DTB_MAX_NODES) return -1;
    dtb_node_t *n = &nodes[node_count];
    memset(n, 0, sizeof(dtb_node_t));
    strncpy(n->name, name, 63);
    strncpy(n->path, path, 127);
    n->parent = parent;
    n->depth = depth;
    if (parent >= 0) {
        nodes[parent].children[nodes[parent].child_count++] = node_count;
    }
    return node_count++;
}

static void dtb_add_prop(int idx, const char *pname, const void *data, uint32_t len)
{
    if (idx < 0 || idx >= node_count) return;
    dtb_node_t *n = &nodes[idx];
    if (n->prop_count >= 16) return;
    if (dtb_prop_store_used >= DTB_MAX_PROPS) return;
    dtb_prop_t *p = &n->props[n->prop_count++];
    strncpy(p->name, pname, 63);
    p->name[63] = '\0';
    p->length = len;
    p->data = dtb_prop_store[dtb_prop_store_used];
    for (uint32_t i = 0; i < len && i < 16; i++) {
        p->data[i] = ((const uint8_t *)data)[i];
    }
    dtb_prop_store_used++;
}

static void dtb_add_prop_u32(int idx, const char *pname, uint32_t val)
{
    uint32_t be = be32(val);
    dtb_add_prop(idx, pname, &be, 4);
}

static void dtb_add_prop_u64(int idx, const char *pname, uint64_t val)
{
    uint32_t hi = be32((uint32_t)(val >> 32));
    uint32_t lo = be32((uint32_t)val);
    uint8_t buf[8];
    memcpy(buf, &hi, 4); memcpy(buf + 4, &lo, 4);
    dtb_add_prop(idx, pname, buf, 8);
}

static void dtb_add_prop_str(int idx, const char *pname, const char *s)
{
    dtb_add_prop(idx, pname, s, (uint32_t)strlen(s) + 1);
}

void dtb_build_builtin(void)
{
    dtb_prop_store_used = 0;
    node_count = 0;
    int root = dtb_add_node("/", "", -1, 0);
    /* /cpus/cpu@0 */
    int cpus = dtb_add_node("/cpus", "cpus", root, 1);
    int cpu0 = dtb_add_node("/cpus/cpu@0", "cpu@0", cpus, 2);
    dtb_add_prop_str(cpu0, "compatible", "hepu,x86-cpu");
    dtb_add_prop_u32(cpu0, "clock-frequency", 1000000000);   /* 1 GHz */
    dtb_add_prop_u32(cpu0, "cache-line-size", 64);
    /* /memory */
    int mem = dtb_add_node("/memory", "memory", root, 1);
    dtb_add_prop_str(mem, "device_type", "memory");
    {
        uint32_t reg[2] = { 0x01000000, 0x04000000 };  /* 16MB..80MB = 64MB */
        uint32_t be0 = be32(reg[0]), be1 = be32(reg[1]);
        uint8_t buf[8];
        memcpy(buf, &be0, 4); memcpy(buf + 4, &be1, 4);
        dtb_add_prop(mem, "reg", buf, 8);
    }
    /* /soc */
    int soc = dtb_add_node("/soc", "soc", root, 1);
    dtb_add_prop_str(soc, "compatible", "hepu,soc-v1");
    /* timer (PIT) */
    int timer = dtb_add_node("/soc/timer@40", "timer@40", soc, 2);
    dtb_add_prop_str(timer, "compatible", "hepu,pit");
    dtb_add_prop_u32(timer, "clock-frequency", 1193182);
    {
        uint32_t v = be32(0);
        dtb_add_prop(timer, "interrupts", &v, 4);
    }
    /* keyboard (PS/2) */
    int kbd = dtb_add_node("/soc/keyboard@60", "keyboard@60", soc, 2);
    dtb_add_prop_str(kbd, "compatible", "hepu,ps2-keyboard");
    {
        uint32_t v = be32(1);
        dtb_add_prop(kbd, "interrupts", &v, 4);
    }
    /* mouse (PS/2) */
    int mouse = dtb_add_node("/soc/mouse@60", "mouse@60", soc, 2);
    dtb_add_prop_str(mouse, "compatible", "hepu,ps2-mouse");
    {
        uint32_t v = be32(12);
        dtb_add_prop(mouse, "interrupts", &v, 4);
    }
    /* VGA 文本 */
    int vga = dtb_add_node("/soc/vga@a0000", "vga@a0000", soc, 2);
    dtb_add_prop_str(vga, "compatible", "hepu,vga-text");
    dtb_add_prop_u32(vga, "cols", 80);
    dtb_add_prop_u32(vga, "rows", 25);
    /* IDE 存储 */
    int ide = dtb_add_node("/soc/ide@1f0", "ide@1f0", soc, 2);
    dtb_add_prop_str(ide, "compatible", "hepu,ide-pio");
    {
        uint32_t v = be32(14);
        dtb_add_prop(ide, "interrupts", &v, 4);
    }
    /* RTC */
    int rtc = dtb_add_node("/soc/rtc@70", "rtc@70", soc, 2);
    dtb_add_prop_str(rtc, "compatible", "hepu,rtc-mc146818");
    {
        uint32_t v = be32(8);
        dtb_add_prop(rtc, "interrupts", &v, 4);
    }
    /* 电源管理 */
    int pm = dtb_add_node("/soc/pm", "pm", soc, 2);
    dtb_add_prop_str(pm, "compatible", "hepu,pm");
    /* 触摸屏 */
    int touch = dtb_add_node("/soc/touch", "touch", soc, 2);
    dtb_add_prop_str(touch, "compatible", "hepu,touch-ft5x06");
    dtb_add_prop_u32(touch, "max-x", 320);
    dtb_add_prop_u32(touch, "max-y", 480);

    dtb_parsed = 1;
    vga_printf("[DTB] builtin platform tree: %d nodes\n", node_count);
}

/* V4.21-E66: 硬件自检——DTB 描述 vs 实际常量一致性 */
void dtb_self_check(void)
{
    if (!dtb_parsed) { vga_puts("[DTB] self-check skipped (no tree)\n"); return; }
    int ok = 0, total = 0;
    dtb_node_t *timer = dtb_find_node("/soc/timer@40");
    total++;
    if (timer && dtb_get_prop_u32(timer, "clock-frequency", 0) == 1193182) ok++;
    else vga_puts("[DTB] WARN: timer freq mismatch (expect 1193182)\n");
    dtb_node_t *vga = dtb_find_node("/soc/vga@a0000");
    total++;
    if (vga && dtb_get_prop_u32(vga, "cols", 0) == 80) ok++;
    else vga_puts("[DTB] WARN: vga cols mismatch (expect 80)\n");
    dtb_node_t *rtc = dtb_find_node("/soc/rtc@70");
    total++;
    if (rtc && dtb_get_irq(rtc, 0) == 8) ok++;
    else vga_puts("[DTB] WARN: rtc irq mismatch (expect 8)\n");
    vga_printf("[DTB] self-check: %d/%d ok\n", ok, total);
}

/* V4.20-E65: 树形显示 */
void dtb_show(void)
{
    if (!dtb_parsed) { vga_puts("[DTB] not parsed\n"); return; }
    vga_printf("[DTB] Hepu platform (%d nodes):\n", node_count);
    for (int i = 0; i < node_count; i++) {
        dtb_node_t *n = &nodes[i];
        for (int d = 0; d < n->depth; d++) vga_puts("  ");
        vga_printf("%s", (n->name[0] == '\0') ? "/" : n->name);
        /* 关键属性摘要 */
        for (int p = 0; p < n->prop_count; p++) {
            dtb_prop_t *pr = &n->props[p];
            if (strcmp(pr->name, "compatible") == 0) {
                vga_printf("  [%s]", (const char *)pr->data);
            }
        }
        /* V4.52-E97: reg 基址 + irq 摘要 */
        uint32_t r0 = dtb_get_reg(n, 0);
        int irq0 = dtb_get_irq(n, 0);
        if (r0 != 0 || irq0 >= 0) {
            vga_printf("  reg=0x%x irq=%d", r0, irq0);
        }
        vga_putc('\n');
    }
}
