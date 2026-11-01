/* ============================================================================
 * 赫普OS - 内存压缩（zram）
 * 基于《赫普OS内存、显存与存储资源管理详解》2.4节实现
 *
 * 实现：
 * - LZ4压缩算法（工业标准，压缩率和速度更优，V6.50升级）
 * - 内存压缩块设备（将压缩后的数据存储在内存中）
 * - 页级压缩/解压接口
 * - 统计信息（压缩率、错误数等）
 * - 自测验证压缩/解压正确性
 *
 * 压缩算法说明（LZ4简化版）：
 * - 最小匹配长度：4字节
 * - 最大匹配偏移：65535字节
 * - 输出格式：token字节 + 字面量 + 匹配偏移(2字节) + 匹配长度
 * - token高4位=字面量长度，低4位=匹配长度（>=15时多字节编码）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "zram.h"
#include "kheap.h"
#include "string.h"
#include "vga.h"

/* 压缩算法参数 */
#define ZRAM_PAGE_SIZE     4096  /* 页大小 */

/* zram页槽 */
typedef struct {
    uint8_t  *data;          /* 压缩后数据 */
    uint32_t  size;          /* 压缩后大小 */
    uint32_t  original_size; /* 原始大小 */
    int       used;          /* 是否使用 */
} zram_slot_t;

/* zram状态 */
static zram_slot_t *zram_slots = NULL;
static uint32_t     zram_max_pages = 0;
static uint32_t     zram_used_pages = 0;
static int          zram_initialized = 0;

/* 统计 */
static zram_stats_t zram_stats;

/* ============================================================================
 * LZ4压缩算法（简化版，工业标准，压缩率和速度更优）
 *
 * LZ4格式：
 * - 每个序列以token字节开始：
 *   - 高4位：字面量长度（=15时后续有额外字节）
 *   - 低4位：匹配长度（=15时后续有额外字节，实际长度=值+4）
 * - 字面量数据
 * - 匹配偏移（2字节，小端序）
 * - 匹配长度额外字节（如果需要）
 *
 * 简化实现：使用线性搜索（不使用哈希表），保证正确性和可维护性
 * 最小匹配长度：4字节
 * ========================================================================== */

#define LZ4_MIN_MATCH     4
#define LZ4_MAX_DISTANCE  65535
#define LZ4_TOKEN_LITERAL_SHIFT 4
#define LZ4_TOKEN_MATCH_MASK   0x0F

/* 写入长度（如果>=15，用多字节编码） */
static uint32_t lz4_write_length(uint8_t *dst, uint32_t dst_pos, uint32_t len)
{
    if (len < 15) {
        dst[dst_pos++] = (uint8_t)len;
    } else {
        len -= 15;
        dst[dst_pos++] = 255;
        while (len >= 255) {
            dst[dst_pos++] = 255;
            len -= 255;
        }
        dst[dst_pos++] = (uint8_t)len;
    }
    return dst_pos;
}

/* 读取长度 */
static uint32_t lz4_read_length(const uint8_t *src, uint32_t *src_pos, uint32_t src_len)
{
    uint32_t len = src[(*src_pos)++];
    if (len == 255) {
        len = 0;  /* 从0开始累加（255只是标记，表示有额外字节） */
        while (*src_pos < src_len && src[*src_pos] == 255) {
            len += 255;
            (*src_pos)++;
        }
        if (*src_pos < src_len) {
            len += src[*src_pos];
            (*src_pos)++;
        }
    }
    return len;
}

/* LZ4压缩 */
static uint32_t lz4_compress(const uint8_t *src, uint32_t src_len,
                               uint8_t *dst, uint32_t dst_size)
{
    if (!src || !dst || src_len == 0 || dst_size < 4) return 0;

    uint32_t src_pos = 0;
    uint32_t dst_pos = 0;
    uint32_t anchor = 0;  /* 字面量起始位置 */

    while (src_pos + LZ4_MIN_MATCH <= src_len) {
        /* 在搜索窗口中查找匹配 */
        uint32_t best_len = 0;
        uint32_t best_off = 0;
        uint32_t search_start = (src_pos > LZ4_MAX_DISTANCE) ? src_pos - LZ4_MAX_DISTANCE : 0;

        for (uint32_t i = search_start; i < src_pos; i++) {
            uint32_t len = 0;
            while (src_pos + len < src_len && src[i + len] == src[src_pos + len]) {
                len++;
            }
            if (len > best_len) {
                best_len = len;
                best_off = src_pos - i;
                if (best_len >= 65535) break;  /* 达到最大匹配长度 */
            }
        }

        if (best_len >= LZ4_MIN_MATCH) {
            /* 找到匹配，先输出字面量 */
            uint32_t literal_len = src_pos - anchor;

            /* 检查输出缓冲区（token + 字面量长度 + 字面量 + 偏移2字节 + 匹配长度） */
            if (dst_pos + 1 + (literal_len >= 15 ? 1 + literal_len / 255 : 0) +
                literal_len + 2 + (best_len >= 19 ? 1 + (best_len - 19) / 255 : 0) > dst_size) {
                return 0;
            }

            /* 写入token */
            uint8_t token = 0;
            if (literal_len < 15) {
                token |= (literal_len << LZ4_TOKEN_LITERAL_SHIFT);
            } else {
                token |= (15 << LZ4_TOKEN_LITERAL_SHIFT);
            }
            uint32_t match_enc = (best_len >= 4) ? best_len - 4 : 0;
            if (match_enc < 15) {
                token |= match_enc;
            } else {
                token |= 15;
            }
            dst[dst_pos++] = token;

            /* 写入字面量长度 */
            if (literal_len >= 15) {
                dst_pos = lz4_write_length(dst, dst_pos, literal_len);
            }

            /* 写入字面量数据 */
            if (literal_len > 0) {
                memcpy(dst + dst_pos, src + anchor, literal_len);
                dst_pos += literal_len;
            }

            /* 写入匹配偏移（2字节，小端序） */
            dst[dst_pos++] = best_off & 0xFF;
            dst[dst_pos++] = (best_off >> 8) & 0xFF;

            /* 写入匹配长度额外字节 */
            if (match_enc >= 15) {
                dst_pos = lz4_write_length(dst, dst_pos, match_enc);
            }

            src_pos += best_len;
            anchor = src_pos;
        } else {
            src_pos++;
        }
    }

    /* 输出最后一段字面量 */
    uint32_t literal_len = src_len - anchor;
    if (dst_pos + 1 + (literal_len >= 15 ? 1 + literal_len / 255 : 0) + literal_len > dst_size) {
        return 0;
    }

    /* 写入token（只有字面量，匹配长度=0） */
    uint8_t token = 0;
    if (literal_len < 15) {
        token |= (literal_len << LZ4_TOKEN_LITERAL_SHIFT);
    } else {
        token |= (15 << LZ4_TOKEN_LITERAL_SHIFT);
    }
    dst[dst_pos++] = token;

    /* 写入字面量长度 */
    if (literal_len >= 15) {
        dst_pos = lz4_write_length(dst, dst_pos, literal_len);
    }

    /* 写入字面量数据 */
    if (literal_len > 0) {
        memcpy(dst + dst_pos, src + anchor, literal_len);
        dst_pos += literal_len;
    }

    return dst_pos;
}

/* LZ4解压 */
static uint32_t lz4_decompress(const uint8_t *src, uint32_t src_len,
                                 uint8_t *dst, uint32_t dst_size)
{
    if (!src || !dst || src_len == 0 || dst_size == 0) return 0;

    uint32_t src_pos = 0;
    uint32_t dst_pos = 0;

    while (src_pos < src_len) {
        /* 读取token */
        uint8_t token = src[src_pos++];

        /* 读取字面量长度 */
        uint32_t literal_len = token >> LZ4_TOKEN_LITERAL_SHIFT;
        if (literal_len == 15) {
            literal_len += lz4_read_length(src, &src_pos, src_len);
        }

        /* 复制字面量 */
        if (literal_len > 0) {
            if (src_pos + literal_len > src_len) return 0;
            if (dst_pos + literal_len > dst_size) return 0;
            memcpy(dst + dst_pos, src + src_pos, literal_len);
            src_pos += literal_len;
            dst_pos += literal_len;
        }

        /* 如果没有更多数据，结束 */
        if (src_pos >= src_len) break;

        /* 读取匹配偏移（2字节，小端序） */
        if (src_pos + 1 >= src_len) return 0;
        uint32_t match_off = src[src_pos] | (src[src_pos + 1] << 8);
        src_pos += 2;

        if (match_off == 0 || match_off > dst_pos) return 0;

        /* 读取匹配长度 */
        uint32_t match_len = token & LZ4_TOKEN_MATCH_MASK;
        if (match_len == 15) {
            match_len += lz4_read_length(src, &src_pos, src_len);
        }
        match_len += 4;  /* 实际匹配长度 = 编码值 + 4 */

        /* 复制匹配数据（可能重叠，用memmove） */
        if (dst_pos + match_len > dst_size) return 0;
        uint8_t *src_ptr = dst + dst_pos - match_off;
        uint8_t *dst_ptr = dst + dst_pos;

        /* 逐字节复制（处理重叠情况） */
        for (uint32_t i = 0; i < match_len; i++) {
            dst_ptr[i] = src_ptr[i];
        }
        dst_pos += match_len;
    }

    return dst_pos;
}

/* 兼容旧接口名 */
#define lzss_compress lz4_compress
#define lzss_decompress lz4_decompress

/* ============================================================================
 * zram公共接口
 * ========================================================================== */

/* 初始化zram */
void zram_init(uint32_t max_pages)
{
    if (zram_initialized) return;
    if (max_pages == 0) max_pages = 1024;  /* 默认1024页 = 4MB原始数据 */

    zram_max_pages = max_pages;
    zram_slots = (zram_slot_t *)kmalloc(sizeof(zram_slot_t) * max_pages);
    if (!zram_slots) return;

    memset(zram_slots, 0, sizeof(zram_slot_t) * max_pages);
    zram_used_pages = 0;
    zram_initialized = 1;

    memset(&zram_stats, 0, sizeof(zram_stats));
    zram_stats.total_pages = max_pages;
}

/* 压缩一页 */
uint32_t zram_compress_page(const uint8_t *src, uint8_t *dst, uint32_t dst_size)
{
    if (!zram_initialized || !src || !dst) return 0;
    return lzss_compress(src, ZRAM_PAGE_SIZE, dst, dst_size);
}

/* 解压一页 */
uint32_t zram_decompress_page(const uint8_t *src, uint32_t src_size,
                                uint8_t *dst, uint32_t dst_size)
{
    if (!zram_initialized || !src || !dst) return 0;
    return lzss_decompress(src, src_size, dst, dst_size);
}

/* 存储压缩页 */
int zram_store_page(uint32_t page_idx, const uint8_t *data)
{
    if (!zram_initialized || page_idx >= zram_max_pages || !data) return -1;

    /* 压缩数据 */
    uint8_t *compressed = (uint8_t *)kmalloc(ZRAM_PAGE_SIZE);
    if (!compressed) return -2;

    uint32_t comp_size = lzss_compress(data, ZRAM_PAGE_SIZE, compressed, ZRAM_PAGE_SIZE);
    if (comp_size == 0 || comp_size >= ZRAM_PAGE_SIZE) {
        /* 压缩失败或压缩后更大，存储原始数据 */
        kfree(compressed);
        compressed = (uint8_t *)kmalloc(ZRAM_PAGE_SIZE);
        if (!compressed) return -2;
        memcpy(compressed, data, ZRAM_PAGE_SIZE);
        comp_size = ZRAM_PAGE_SIZE;
        zram_stats.compress_errors++;
    }

    /* 释放旧数据 */
    if (zram_slots[page_idx].used) {
        kfree(zram_slots[page_idx].data);
        zram_stats.original_bytes -= zram_slots[page_idx].original_size;
        zram_stats.compressed_bytes -= zram_slots[page_idx].size;
        zram_used_pages--;
    }

    zram_slots[page_idx].data = compressed;
    zram_slots[page_idx].size = comp_size;
    zram_slots[page_idx].original_size = ZRAM_PAGE_SIZE;
    zram_slots[page_idx].used = 1;
    zram_used_pages++;
    zram_stats.compressed_pages++;
    zram_stats.original_bytes += ZRAM_PAGE_SIZE;
    zram_stats.compressed_bytes += comp_size;

    return 0;
}

/* 读取压缩页并解压 */
int zram_load_page(uint32_t page_idx, uint8_t *data)
{
    if (!zram_initialized || page_idx >= zram_max_pages || !data) return -1;
    if (!zram_slots[page_idx].used) return -2;

    if (zram_slots[page_idx].size >= ZRAM_PAGE_SIZE) {
        /* 存储的是原始数据（压缩失败） */
        memcpy(data, zram_slots[page_idx].data, ZRAM_PAGE_SIZE);
    } else {
        /* 解压 */
        uint32_t decomp_size = lzss_decompress(
            zram_slots[page_idx].data, zram_slots[page_idx].size,
            data, ZRAM_PAGE_SIZE);
        if (decomp_size != ZRAM_PAGE_SIZE) {
            zram_stats.decompress_errors++;
            return -3;
        }
    }
    return 0;
}

/* 释放zram中的页 */
void zram_free_page(uint32_t page_idx)
{
    if (!zram_initialized || page_idx >= zram_max_pages) return;
    if (!zram_slots[page_idx].used) return;

    kfree(zram_slots[page_idx].data);
    zram_stats.original_bytes -= zram_slots[page_idx].original_size;
    zram_stats.compressed_bytes -= zram_slots[page_idx].size;
    zram_stats.compressed_pages--;  /* 修复：减少压缩页数统计 */
    memset(&zram_slots[page_idx], 0, sizeof(zram_slot_t));
    zram_used_pages--;
}

/* 获取统计信息 */
void zram_get_stats(zram_stats_t *stats)
{
    if (!stats) return;
    memcpy(stats, &zram_stats, sizeof(zram_stats_t));
}

/* 获取压缩率（百分比，值越大压缩越好） */
uint32_t zram_get_ratio(void)
{
    if (zram_stats.original_bytes == 0) return 0;
    return (zram_stats.compressed_bytes * 100) / zram_stats.original_bytes;
}

/* ============================================================================
 * zram自测
 * ========================================================================== */
void zram_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define ZLOG(s) serial_puts(0x3F8, s)

    ZLOG("[ZRAM] selftest: start\n");
    int pass = 0, fail = 0;

    /* 初始化zram（64页槽） */
    zram_init(64);

    /* 测试1：全零页压缩（应该压缩率很高） */
    static uint8_t zero_page[4096];
    static uint8_t comp_buf[4096];
    static uint8_t decomp_buf[4096];
    memset(zero_page, 0, 4096);
    uint32_t comp_size = zram_compress_page(zero_page, comp_buf, 4096);
    if (comp_size > 0 && comp_size < 100) {  /* RLE全零页约35字节 */
        ZLOG("[ZRAM] test1 compress zero page: PASS (size=");
        serial_puthex(0x3F8, comp_size);
        ZLOG(")\n");
        pass++;
    } else {
        ZLOG("[ZRAM] test1 compress zero page: FAIL (size=");
        serial_puthex(0x3F8, comp_size);
        ZLOG(")\n");
        fail++;
    }

    /* 测试2：全零页解压验证 */
    uint32_t decomp_size = zram_decompress_page(comp_buf, comp_size, decomp_buf, 4096);
    if (decomp_size == 4096 && memcmp(zero_page, decomp_buf, 4096) == 0) {
        ZLOG("[ZRAM] test2 decompress zero page: PASS\n");
        pass++;
    } else {
        ZLOG("[ZRAM] test2 decompress zero page: FAIL (size=");
        serial_puthex(0x3F8, decomp_size);
        ZLOG(")\n");
        fail++;
    }

    /* 测试3：半随机半重复数据压缩/解压验证（RLE对纯随机数据压缩率差，使用可压缩数据） */
    static uint8_t rand_page[4096];
    /* 生成可压缩数据：每64字节随机数据 + 64字节重复数据 */
    uint32_t seed = 12345;
    for (int i = 0; i < 4096; i++) {
        if ((i / 64) % 2 == 0) {
            seed = seed * 1103515245 + 12345;
            rand_page[i] = (seed >> 16) & 0xFF;
        } else {
            rand_page[i] = 0xAB;  /* 重复数据 */
        }
    }
    comp_size = zram_compress_page(rand_page, comp_buf, 8192);  /* 用8KB缓冲区，压缩后可能更大 */
    if (comp_size > 0) {
        decomp_size = zram_decompress_page(comp_buf, comp_size, decomp_buf, 4096);
        if (decomp_size == 4096 && memcmp(rand_page, decomp_buf, 4096) == 0) {
            ZLOG("[ZRAM] test3 random data compress/decompress: PASS (ratio=");
            serial_puthex(0x3F8, comp_size * 100 / 4096);
            ZLOG("%)\n");
            pass++;
        } else {
            ZLOG("[ZRAM] test3 random data decompress: FAIL\n");
            fail++;
        }
    } else {
        ZLOG("[ZRAM] test3 random data compress: FAIL\n");
        fail++;
    }

    /* 测试4：重复模式数据压缩（应该有较好压缩率） */
    static uint8_t pattern_page[4096];
    for (int i = 0; i < 4096; i++) {
        pattern_page[i] = (i % 64 < 32) ? 0xAA : 0x55;
    }
    comp_size = zram_compress_page(pattern_page, comp_buf, 4096);
    if (comp_size > 0 && comp_size < 2048) {  /* 重复模式应该压缩到50%以下 */
        ZLOG("[ZRAM] test4 pattern compress: PASS (size=");
        serial_puthex(0x3F8, comp_size);
        ZLOG(", ratio=");
        serial_puthex(0x3F8, comp_size * 100 / 4096);
        ZLOG("%)\n");
        pass++;
    } else {
        ZLOG("[ZRAM] test4 pattern compress: FAIL (size=");
        serial_puthex(0x3F8, comp_size);
        ZLOG(")\n");
        fail++;
    }

    /* 测试5：zram存储/加载验证 */
    int ret = zram_store_page(0, pattern_page);
    if (ret == 0) {
        memset(decomp_buf, 0, 4096);
        ret = zram_load_page(0, decomp_buf);
        if (ret == 0 && memcmp(pattern_page, decomp_buf, 4096) == 0) {
            ZLOG("[ZRAM] test5 store/load page: PASS\n");
            pass++;
        } else {
            ZLOG("[ZRAM] test5 store/load page: FAIL (load ret=");
            serial_puthex(0x3F8, ret);
            ZLOG(")\n");
            fail++;
        }
    } else {
        ZLOG("[ZRAM] test5 store page: FAIL\n");
        fail++;
    }

    /* 测试6：统计信息验证 */
    zram_stats_t stats;
    zram_get_stats(&stats);
    if (stats.compressed_pages >= 1 && stats.original_bytes >= 4096) {
        ZLOG("[ZRAM] test6 stats: PASS (compressed_pages=");
        serial_puthex(0x3F8, stats.compressed_pages);
        ZLOG(", original_bytes=");
        serial_puthex(0x3F8, stats.original_bytes);
        ZLOG(", compressed_bytes=");
        serial_puthex(0x3F8, stats.compressed_bytes);
        ZLOG(")\n");
        pass++;
    } else {
        ZLOG("[ZRAM] test6 stats: FAIL\n");
        fail++;
    }

    /* 测试7：释放页验证 */
    zram_free_page(0);
    zram_get_stats(&stats);
    if (stats.compressed_pages == 0 && stats.original_bytes == 0) {
        ZLOG("[ZRAM] test7 free page: PASS\n");
        pass++;
    } else {
        ZLOG("[ZRAM] test7 free page: FAIL\n");
        fail++;
    }

    ZLOG("[ZRAM] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    ZLOG(", fail=");
    serial_puthex(0x3F8, fail);
    ZLOG(")\n");

    #undef ZLOG
}
