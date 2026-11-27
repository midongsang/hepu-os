/* ============================================================================
 * ACPI 语料兼容性探针（只读审计，不修改内核源码）。
 *
 * 用途：对每张 DSDT/SSDT 二进制表，用 HEAD 快照编译出的极简 AML 解释器
 *   1) 建命名空间，记录 aml_ns_build 返回码、节点数、ready 状态；
 *   2) 枚举 PNP0C0A / ACPI0003 / PNP0C09 设备；
 *   3) 对电池设备尝试求值 _STA / _BIF / _BIX / _BST，记录返回码与值形态。
 *
 * 编译（由 tools/regress/run_aml_corpus_probe.sh 完成）：
 *   gcc -DAML_HOST_TEST -I <snapshot> -I kernel/arch/x86_64 \
 *       tools/host-tests/aml_corpus_probe.c <snapshot>/aml64.c <snapshot>/ec64.c
 *
 * 说明：HEAD 公开 API 没有暴露“建库失败偏移 / degraded 计数”，探针只记录
 * 接口能拿到的字段；首个未支持 opcode 的偏移由离线普查脚本 aml_census.py
 * 另行给出，二者口径在证据 README 里分开标注。
 * ========================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "aml64.h"

/* ---- 内核侧依赖桩（主机测试，不碰真实端口/堆） ---- */
void vga_puts(const char *s) { (void)s; }
void vga_put_hex(uint64_t v) { (void)v; }
void *kmalloc(uint32_t size) { return calloc(1, size ? size : 1); }
uint64_t vmm64_phys_to_ptr(uint64_t phys) { return phys; }

/* 主机模拟 EC：无真实 EC，IBF 永远置位 → EC 读超时 fail-closed。 */
uint8_t ec_host_inb(uint16_t port) { (void)port; return 0x01; }
void ec_host_outb(uint16_t port, uint8_t val) { (void)port; (void)val; }

#define BAT_EISA 0x41D00C0Au
#define AC_EISA  0x04700003u
#define EC_EISA  0x41D00C09u

static const char *rc_name(int rc)
{
    switch (rc) {
    case AML_E_OK: return "OK";
    case AML_E_NOTFOUND: return "NOTFOUND";
    case AML_E_BADCODE: return "BADCODE";
    case AML_E_UNSUP: return "UNSUP";
    case AML_E_LIMIT: return "LIMIT";
    case AML_E_ARG: return "ARG";
    default: return "?";
    }
}

static const char *type_name(uint8_t t)
{
    switch (t) {
    case AML_V_INT: return "INT";
    case AML_V_STR: return "STR";
    case AML_V_BUF: return "BUF";
    case AML_V_PKG: return "PKG";
    case AML_V_REF: return "REF";
    default: return "?";
    }
}

static int load_file(const char *path, uint8_t **out)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *b;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 36) { fclose(f); return -1; }
    b = malloc((size_t)n);
    if (!b) { fclose(f); return -1; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return -1; }
    fclose(f);
    *out = b;
    return (int)n;
}

static void dump_value(const char *tag, aml_value_t *v)
{
    if (!v) { printf("  %s rc-val=null\n", tag); return; }
    printf("  %s type=%s", tag, type_name(aml_value_type(v)));
    if (v->type == AML_V_INT) {
        printf(" val=%lld", (long long)v->ival);
    } else if (v->type == AML_V_STR) {
        printf(" str=\"%.*s\"", (int)v->blen, v->buf ? (char *)v->buf : "");
    } else if (v->type == AML_V_BUF) {
        printf(" blen=%u", v->blen);
    } else if (v->type == AML_V_PKG) {
        int n = aml_value_count(v);
        int i;
        printf(" count=%d elems=[", n);
        for (i = 0; i < n && i < 8; i++) {
            const aml_value_t *e = aml_value_at(v, i);
            int64_t x = 0;
            if (e && aml_value_int(e, &x) == 0) printf("%lld", (long long)x);
            else if (e) printf("%s", type_name(aml_value_type(e)));
            else printf("null");
            if (i + 1 < n && i + 1 < 8) printf(",");
        }
        printf("]");
    }
    printf("\n");
}

static void eval_one(const char *dev, const char *obj)
{
    char path[160];
    aml_value_t *v = 0;
    int rc;
    snprintf(path, sizeof(path), "%s.%s", dev, obj);
    aml_sample_begin();
    rc = aml_eval_path(path, &v);
    aml_sample_end();
    printf("  %s rc=%d(%s)", path, rc, rc_name(rc));
    if (rc == 0) dump_value("rc-val", v);
    else printf("\n");
}

static void find_all(uint32_t eisa, const char *hid, char out[][96], int *n)
{
    int cursor = 0;
    char dev[96];
    *n = 0;
    while (*n < 8 &&
           aml_find_device_next(eisa, hid, dev, sizeof(dev), &cursor) == 0) {
        snprintf(out[*n], 96, "%s", dev);
        (*n)++;
    }
}

int main(int argc, char **argv)
{
    int i;
    int tables_seen = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <table.aml> [table2.aml ...]\n", argv[0]);
        return 2;
    }

    for (i = 1; i < argc; i++) {
        uint8_t *buf = 0;
        int n = load_file(argv[i], &buf);
        const uint8_t *tp[1];
        uint32_t tl[1];
        int rc, nodes, ready;
        char bat[8][96], ac[8][96], ec[8][96];
        int nbat, nac, nec, k;
        uint32_t hdr_len;
        const char *sig;

        printf("=== TABLE %s\n", argv[i]);
        if (n < 36) {
            printf("  status=read-fail size=%d\n", n);
            free(buf);
            continue;
        }
        sig = (const char *)buf;
        hdr_len = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                  ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
        if (memcmp(sig, "DSDT", 4) != 0 && memcmp(sig, "SSDT", 4) != 0) {
            printf("  status=skip-not-aml sig=%c%c%c%c size=%d\n",
                   buf[0], buf[1], buf[2], buf[3], n);
            free(buf);
            continue;
        }
        if (hdr_len >= 36 && hdr_len <= (uint32_t)n) n = (int)hdr_len;

        aml_ns_reset();
        tp[0] = buf;
        tl[0] = (uint32_t)n;
        rc = aml_ns_build(tp, tl, 1);
        nodes = aml_ns_nodes();
        ready = aml_ns_ready();
        tables_seen++;
        printf("  sig=%.*s size=%d build_rc=%d(%s) ready=%d nodes=%d\n",
               4, sig, n, rc, rc_name(rc), ready, nodes);

        find_all(BAT_EISA, "PNP0C0A", bat, &nbat);
        find_all(AC_EISA, "ACPI0003", ac, &nac);
        find_all(EC_EISA, "PNP0C09", ec, &nec);
        printf("  devices: PNP0C0A=%d ACPI0003=%d PNP0C09=%d\n", nbat, nac, nec);
        for (k = 0; k < nbat; k++) printf("    bat[%d]=%s\n", k, bat[k]);
        for (k = 0; k < nac; k++) printf("    ac[%d]=%s\n", k, ac[k]);
        for (k = 0; k < nec; k++) printf("    ec[%d]=%s\n", k, ec[k]);

        for (k = 0; k < nbat; k++) {
            eval_one(bat[k], "_STA");
            eval_one(bat[k], "_BIF");
            eval_one(bat[k], "_BIX");
            eval_one(bat[k], "_BST");
        }
        for (k = 0; k < nac; k++) {
            eval_one(ac[k], "_PSR");
        }
        free(buf);
        printf("\n");
    }
    printf("PROBE DONE tables=%d\n", tables_seen);
    return 0;
}
