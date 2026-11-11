/* ============================================================================
 * 赫普OS - PNG 解码器实现 (V0.54.13 P0)
 * 详见 png64.h 的能力说明。
 * ========================================================================== */
#include "png64.h"

#ifdef PNG64_HOST
#include <stdlib.h>
#include <string.h>
#define P_ALLOC(n) malloc(n)
#define P_FREE(p)  free(p)
#else
extern uint64_t pmm64_alloc_pages(uint64_t n);
static void *p_alloc(uint32_t n)
{
    uint64_t phys = pmm64_alloc_pages(((uint64_t)n + 4095) / 4096);
    return (void *)(uintptr_t)phys;
}
#define P_ALLOC(n) p_alloc(n)
#define P_FREE(p)  ((void)(p))
#endif

static void p_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}
static void p_memset(void *dst, int c, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)c;
}

/* ===================== DEFLATE / inflate ===================== */
#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define MAXCODES (MAXLCODES + MAXDCODES)
#define FIXLCODES 288

typedef struct {
    const uint8_t *in;      /* 输入指针 */
    uint32_t inlen;         /* 输入长度 */
    uint32_t incnt;         /* 已读字节 */
    uint32_t inpos;         /* 当前位 */
    uint8_t *out;
    uint32_t outlen;        /* 容量 */
    uint32_t outcnt;        /* 已写 */
    int err;
} inf_t;

typedef struct {
    uint16_t counts[MAXBITS + 1];
    uint16_t symbols[MAXCODES];
} huff_t;

static int inf_bits(inf_t *s, int need)
{
    int val = 0;
    for (int i = 0; i < need; i++) {
        if (s->inpos >= 8) { s->inpos = 0; s->incnt++; }
        if (s->incnt >= s->inlen) { s->err = 1; return 0; }
        val |= ((s->in[s->incnt] >> s->inpos) & 1) << i;
        s->inpos++;
    }
    return val;
}

static int inf_stored(inf_t *s)
{
    s->inpos = 0; s->incnt++;
    if (s->incnt + 4 > s->inlen) { s->err = 1; return -1; }
    uint32_t n = s->in[s->incnt] | (s->in[s->incnt + 1] << 8);
    uint32_t nlen = s->in[s->incnt + 2] | (s->in[s->incnt + 3] << 8);
    s->incnt += 4;
    if ((n ^ 0xFFFF) != nlen) { s->err = 1; return -1; }
    if (s->incnt + n > s->inlen) { s->err = 1; return -1; }
    if (s->outcnt + n > s->outlen) { s->err = 1; return -1; }
    p_memcpy(s->out + s->outcnt, s->in + s->incnt, n);
    s->outcnt += n;
    s->incnt += n;
    return 0;
}

static void huff_build(huff_t *h, const uint8_t *lens, int n)
{
    int offs[MAXBITS + 1];
    for (int i = 0; i <= MAXBITS; i++) h->counts[i] = 0;
    for (int i = 0; i < n; i++) h->counts[lens[i]]++;
    h->counts[0] = 0;
    offs[0] = 0;
    for (int i = 1; i <= MAXBITS; i++) offs[i] = offs[i - 1] + h->counts[i - 1];
    for (int i = 0; i < n; i++)
        if (lens[i]) h->symbols[offs[lens[i]]++] = (uint16_t)i;
}

static int huff_decode(inf_t *s, const huff_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= inf_bits(s, 1);
        if (s->err) return -1;
        int count = h->counts[len];
        if (code - first < count) return h->symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* RFC1951 长度/距离表 */
static const uint16_t len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static int inf_codes(inf_t *s, const huff_t *lencode, const huff_t *distcode)
{
    for (;;) {
        int sym = huff_decode(s, lencode);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->outcnt >= s->outlen) { s->err = 1; return -1; }
            s->out[s->outcnt++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            int len = len_base[sym] + inf_bits(s, len_extra[sym]);
            int dsym = huff_decode(s, distcode);
            if (dsym < 0 || dsym >= 30) return -1;
            int dist = dist_base[dsym] + inf_bits(s, dist_extra[dsym]);
            if (s->err) return -1;
            if ((uint32_t)dist > s->outcnt) return -1;
            if (s->outcnt + (uint32_t)len > s->outlen) { s->err = 1; return -1; }
            for (int i = 0; i < len; i++) {
                s->out[s->outcnt] = s->out[s->outcnt - dist];
                s->outcnt++;
            }
        }
    }
}

/* 动态 huffman 块 */
static int inf_fixed(inf_t *s)
{
    uint8_t lens[FIXLCODES + MAXDCODES];
    uint32_t i;
    for (i = 0; i < 144; i++) lens[i] = 8;
    for (; i < 256; i++) lens[i] = 9;
    for (; i < 280; i++) lens[i] = 7;
    for (; i < FIXLCODES; i++) lens[i] = 8;
    for (i = 0; i < MAXDCODES; i++) lens[FIXLCODES + i] = 5;

    huff_t lencode, distcode;
    huff_build(&lencode, lens, FIXLCODES);
    huff_build(&distcode, lens + FIXLCODES, MAXDCODES);
    return inf_codes(s, &lencode, &distcode);
}

static const int clc_order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

static int inf_dynamic(inf_t *s)
{
    uint8_t lens[MAXCODES];
    int nlen = inf_bits(s, 5) + 257;
    int ndist = inf_bits(s, 5) + 1;
    int ncode = inf_bits(s, 4) + 4;
    if (s->err || nlen > MAXLCODES || ndist > MAXDCODES) return -1;

    uint8_t clens[19];
    p_memset(clens, 0, sizeof(clens));
    for (int i = 0; i < ncode; i++) clens[clc_order[i]] = (uint8_t)inf_bits(s, 3);
    if (s->err) return -1;

    huff_t clcode;
    huff_build(&clcode, clens, 19);

    /* 展开 literal/dist 长度表 */
    int idx = 0;
    while (idx < nlen + ndist) {
        int sym = huff_decode(s, &clcode);
        if (sym < 0) return -1;
        if (sym < 16) {
            lens[idx++] = (uint8_t)sym;
        } else {
            int rep, val = 0;
            if (sym == 16) {
                if (idx == 0) return -1;
                val = lens[idx - 1];
                rep = 3 + inf_bits(s, 2);
            } else if (sym == 17) {
                rep = 3 + inf_bits(s, 3);
            } else {
                rep = 11 + inf_bits(s, 7);
            }
            if (s->err || idx + rep > nlen + ndist) return -1;
            while (rep--) lens[idx++] = (uint8_t)val;
        }
    }
    if (lens[256] == 0) return -1;

    huff_t lencode, distcode;
    huff_build(&lencode, lens, nlen);
    huff_build(&distcode, lens + nlen, ndist);
    return inf_codes(s, &lencode, &distcode);
}

/* zlib 流解压（跳过 2 字节头，忽略 adler 校验） */
static int inflate_zlib(const uint8_t *src, uint32_t srclen,
                        uint8_t *dst, uint32_t dstcap, uint32_t *dstlen)
{
    if (srclen < 2) return -1;
    /* CMF/FLG: 只接受 deflate(8) 且无预设字典 */
    if ((src[0] & 0x0F) != 8) return -1;
    if ((src[0] >> 4) > 7) return -1;
    if (((src[0] << 8) | src[1]) % 31 != 0) return -1;
    if (src[1] & 0x20) return -1;

    inf_t s;
    p_memset(&s, 0, sizeof(s));
    s.in = src + 2;
    s.inlen = srclen - 2;
    s.out = dst;
    s.outlen = dstcap;

    int last;
    do {
        last = inf_bits(&s, 1);
        int type = inf_bits(&s, 2);
        if (s.err) return -1;
        if (type == 0) {
            if (inf_stored(&s) < 0) return -1;
        } else if (type == 1) {
            if (inf_fixed(&s) < 0) return -1;
        } else if (type == 2) {
            if (inf_dynamic(&s) < 0) return -1;
        } else {
            return -1;
        }
    } while (!last);

    *dstlen = s.outcnt;
    return 0;
}

/* ===================== PNG ===================== */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

int png64_decode(const uint8_t *data, int len, png64_image_t *out)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!data || len < 8 || !out) return -1;
    for (int i = 0; i < 8; i++) if (data[i] != sig[i]) return -2;

    uint32_t w = 0, h = 0;
    int bitdepth = 0, coltype = 0, interlace = 0;
    uint8_t palette[256 * 3];
    uint8_t trans[256];
    int have_plte = 0, have_trns = 0;
    p_memset(palette, 0, sizeof(palette));
    p_memset(trans, 0xFF, sizeof(trans));

    /* 收集 IDAT 原始数据: 先扫描求得总长, 再拼接 */
    int pos = 8;
    uint32_t idat_total = 0;
    int idat_chunks = 0;
    while (pos + 8 <= len) {
        uint32_t clen = be32(data + pos);
        const uint8_t *ctype = data + pos + 4;
        if (pos + 12 + (int)clen > len) return -6;

        if (ctype[0] == 'I' && ctype[1] == 'H' && ctype[2] == 'D' && ctype[3] == 'R') {
            if (clen < 13) return -6;
            w = be32(data + pos + 8);
            h = be32(data + pos + 12);
            bitdepth = data[pos + 16];
            coltype = data[pos + 17];
            interlace = data[pos + 20];
            if (w == 0 || h == 0 || w > 8192 || h > 8192) return -3;
            if (interlace != 0) return -3;   /* 不支持 Adam7 */
            if (bitdepth != 1 && bitdepth != 2 && bitdepth != 4 &&
                bitdepth != 8 && bitdepth != 16) return -3;
            if (coltype != 0 && coltype != 2 && coltype != 3 &&
                coltype != 4 && coltype != 6) return -3;
            if (coltype == 3 && bitdepth == 16) return -3;
        } else if (ctype[0] == 'P' && ctype[1] == 'L' && ctype[2] == 'T' && ctype[3] == 'E') {
            if (clen > 256 * 3) return -6;
            p_memcpy(palette, data + pos + 8, clen);
            have_plte = 1;
        } else if (ctype[0] == 't' && ctype[1] == 'R' && ctype[2] == 'N' && ctype[3] == 'S') {
            if (clen > 256) return -6;
            p_memcpy(trans, data + pos + 8, clen);
            have_trns = 1;
        } else if (ctype[0] == 'I' && ctype[1] == 'D' && ctype[2] == 'A' && ctype[3] == 'T') {
            idat_total += clen;
            idat_chunks++;
        } else if (ctype[0] == 'I' && ctype[1] == 'E' && ctype[2] == 'N' && ctype[3] == 'D') {
            break;
        }
        pos += 12 + (int)clen;
    }
    if (w == 0 || h == 0 || idat_chunks == 0) return -6;
    if (coltype == 3 && !have_plte) return -6;

    /* 拼接 IDAT */
    uint8_t *idat = (uint8_t *)P_ALLOC(idat_total ? idat_total : 1);
    if (!idat) return -5;
    {
        uint32_t off = 0;
        pos = 8;
        while (pos + 8 <= len) {
            uint32_t clen = be32(data + pos);
            const uint8_t *ctype = data + pos + 4;
            if (pos + 12 + (int)clen > len) { P_FREE(idat); return -6; }
            if (ctype[0] == 'I' && ctype[1] == 'D' && ctype[2] == 'A' && ctype[3] == 'T') {
                p_memcpy(idat + off, data + pos + 8, clen);
                off += clen;
            }
            if (ctype[0] == 'I' && ctype[1] == 'E' && ctype[2] == 'N' && ctype[3] == 'D') break;
            pos += 12 + (int)clen;
        }
    }

    /* 每像素字节数 & 每行字节数 */
    int channels = (coltype == 0) ? 1 : (coltype == 2) ? 3 :
                   (coltype == 3) ? 1 : (coltype == 4) ? 2 : 4;
    uint32_t row_bytes = ((uint32_t)w * channels * bitdepth + 7) / 8;
    uint32_t raw_size = (row_bytes + 1) * h;
    uint8_t *raw = (uint8_t *)P_ALLOC(raw_size);
    if (!raw) { P_FREE(idat); return -5; }

    uint32_t got = 0;
    if (inflate_zlib(idat, idat_total, raw, raw_size, &got) != 0) {
        P_FREE(idat); P_FREE(raw); return -4;
    }
    P_FREE(idat);
    if (got < raw_size) { P_FREE(raw); return -6; }

    uint8_t *rgba = (uint8_t *)P_ALLOC((uint32_t)w * h * 4);
    if (!rgba) { P_FREE(raw); return -5; }

    /* 逐行反滤波 + 转 RGBA */
    uint8_t *prev = (uint8_t *)P_ALLOC(row_bytes + 16);
    uint8_t *cur = (uint8_t *)P_ALLOC(row_bytes + 16);
    if (!prev || !cur) { P_FREE(rgba); P_FREE(raw); P_FREE(prev); P_FREE(cur); return -5; }
    p_memset(prev, 0, row_bytes);

    for (uint32_t y = 0; y < h; y++) {
        uint8_t *src = raw + y * (row_bytes + 1);
        int filter = src[0];
        uint8_t *in = src + 1;
        for (uint32_t i = 0; i < row_bytes; i++) {
            int a = (i >= (uint32_t)channels) ? cur[i - channels] : 0;
            int b = prev[i];
            int c = (i >= (uint32_t)channels) ? prev[i - channels] : 0;
            int v = in[i];
            switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) >> 1; break;
                case 4: v += paeth(a, b, c); break;
                default: P_FREE(rgba); P_FREE(raw); P_FREE(prev); P_FREE(cur); return -6;
            }
            cur[i] = (uint8_t)v;
        }

        uint8_t *dst = rgba + (uint32_t)y * w * 4;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t r, g, b2, al = 255;
            if (bitdepth == 8) {
                if (coltype == 6) { r = cur[x*4]; g = cur[x*4+1]; b2 = cur[x*4+2]; al = cur[x*4+3]; }
                else if (coltype == 2) { r = cur[x*3]; g = cur[x*3+1]; b2 = cur[x*3+2]; }
                else if (coltype == 0) { r = g = b2 = cur[x]; }
                else if (coltype == 4) { r = g = b2 = cur[x*2]; al = cur[x*2+1]; }
                else {  /* palette */
                    uint8_t idx = cur[x];
                    r = palette[idx*3]; g = palette[idx*3+1]; b2 = palette[idx*3+2];
                    if (have_trns) al = trans[idx];
                }
            } else if (bitdepth == 16) {
                if (coltype == 6) { r = cur[x*8]; g = cur[x*8+2]; b2 = cur[x*8+4]; al = cur[x*8+6]; }
                else if (coltype == 2) { r = cur[x*6]; g = cur[x*6+2]; b2 = cur[x*6+4]; }
                else if (coltype == 0) { r = g = b2 = cur[x*2]; }
                else { r = g = b2 = cur[x*4]; al = cur[x*4+2]; }
            } else {  /* 1/2/4 bit */
                uint32_t bitpos = x * bitdepth;
                uint32_t byte_i = bitpos / 8;
                int shift = 8 - bitdepth - (int)(bitpos % 8);
                uint8_t v = (uint8_t)((cur[byte_i] >> shift) & ((1 << bitdepth) - 1));
                if (coltype == 3) {
                    r = palette[v*3]; g = palette[v*3+1]; b2 = palette[v*3+2];
                    if (have_trns) al = trans[v];
                } else {  /* 灰度: 按位深放大到 8 位 */
                    r = g = b2 = (uint8_t)(v * 255 / ((1 << bitdepth) - 1));
                }
            }
            dst[x*4] = r; dst[x*4+1] = g; dst[x*4+2] = b2; dst[x*4+3] = al;
        }

        uint8_t *tmp = prev; prev = cur; cur = tmp;
    }

    P_FREE(raw); P_FREE(prev); P_FREE(cur);
    out->width = w;
    out->height = h;
    out->rgba = rgba;
    return 0;
}

void png64_free(png64_image_t *img)
{
    if (!img) return;
    if (img->rgba) { P_FREE(img->rgba); img->rgba = 0; }
    img->width = img->height = 0;
}
