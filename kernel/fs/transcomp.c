/* ============================================================================
 * 赫普OS - 透明压缩文件系统
 * 基于《赫普OS内存、显存与存储资源管理详解》3.2节实现
 *
 * 实现：
 * - 块级透明压缩：写入时自动压缩，读取时自动解压
 * - 复用LZ4压缩算法（与zram相同）
 * - 智能压缩：压缩率低于阈值才存储压缩版，否则存储原始数据
 * - 压缩块映射：记录每个块的压缩状态和位置
 * - 统计信息：压缩率、节省空间、压缩/解压次数
 * - 内存版实现（压缩数据存储在内存中，后续可持久化到磁盘）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "transcomp.h"
#include "kheap.h"
#include "vga.h"

/* LZ4常量 */
#define LZ4_MIN_MATCH     4
#define LZ4_TOKEN_LITERAL_SHIFT 4
#define LZ4_TOKEN_MATCH_MASK   0x0F

/* 全局透明压缩管理器 */
static transcomp_mgr_t tc_mgr;

/* ============================================================================
 * LZ4压缩/解压（简化版，与zram相同）
 * ========================================================================== */

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

static uint32_t lz4_read_length(const uint8_t *src, uint32_t *src_pos, uint32_t src_len)
{
    uint32_t len = src[(*src_pos)++];
    if (len == 255) {
        len = 0;
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

static void xtea_encrypt_block_dummy(void) {}  /* 占位，避免未使用警告 */

static uint32_t tc_lz4_compress(const uint8_t *src, uint32_t src_len,
                                  uint8_t *dst, uint32_t dst_size)
{
    if (!src || !dst || src_len == 0 || dst_size < 4) return 0;

    uint32_t src_pos = 0, dst_pos = 0, anchor = 0;

    while (src_pos + LZ4_MIN_MATCH <= src_len) {
        uint32_t best_len = 0, best_off = 0;
        uint32_t search_start = (src_pos > 65535) ? src_pos - 65535 : 0;

        for (uint32_t i = search_start; i < src_pos; i++) {
            uint32_t len = 0;
            while (src_pos + len < src_len && src[i + len] == src[src_pos + len]) len++;
            if (len > best_len) {
                best_len = len;
                best_off = src_pos - i;
                if (best_len >= 65535) break;
            }
        }

        if (best_len >= LZ4_MIN_MATCH) {
            uint32_t literal_len = src_pos - anchor;
            if (dst_pos + 1 + literal_len + 2 + 16 > dst_size) return 0;

            uint8_t token = 0;
            token |= (literal_len < 15) ? (literal_len << 4) : (15 << 4);
            uint32_t match_enc = best_len - 4;
            token |= (match_enc < 15) ? match_enc : 15;
            dst[dst_pos++] = token;

            if (literal_len >= 15) dst_pos = lz4_write_length(dst, dst_pos, literal_len);
            if (literal_len > 0) {
                memcpy(dst + dst_pos, src + anchor, literal_len);
                dst_pos += literal_len;
            }

            dst[dst_pos++] = best_off & 0xFF;
            dst[dst_pos++] = (best_off >> 8) & 0xFF;
            if (match_enc >= 15) dst_pos = lz4_write_length(dst, dst_pos, match_enc);

            src_pos += best_len;
            anchor = src_pos;
        } else {
            src_pos++;
        }
    }

    uint32_t literal_len = src_len - anchor;
    if (dst_pos + 1 + literal_len + 1 > dst_size) return 0;

    uint8_t token = (literal_len < 15) ? (literal_len << 4) : (15 << 4);
    dst[dst_pos++] = token;
    if (literal_len >= 15) dst_pos = lz4_write_length(dst, dst_pos, literal_len);
    if (literal_len > 0) {
        memcpy(dst + dst_pos, src + anchor, literal_len);
        dst_pos += literal_len;
    }

    return dst_pos;
}

static uint32_t tc_lz4_decompress(const uint8_t *src, uint32_t src_len,
                                    uint8_t *dst, uint32_t dst_size)
{
    if (!src || !dst || src_len == 0 || dst_size == 0) return 0;

    uint32_t src_pos = 0, dst_pos = 0;

    while (src_pos < src_len) {
        uint8_t token = src[src_pos++];
        uint32_t literal_len = token >> 4;
        if (literal_len == 15) literal_len += lz4_read_length(src, &src_pos, src_len);

        if (literal_len > 0) {
            if (src_pos + literal_len > src_len || dst_pos + literal_len > dst_size) return 0;
            memcpy(dst + dst_pos, src + src_pos, literal_len);
            src_pos += literal_len;
            dst_pos += literal_len;
        }

        if (src_pos >= src_len) break;

        if (src_pos + 1 >= src_len) return 0;
        uint32_t match_off = src[src_pos] | (src[src_pos + 1] << 8);
        src_pos += 2;
        if (match_off == 0 || match_off > dst_pos) return 0;

        uint32_t match_len = token & LZ4_TOKEN_MATCH_MASK;
        if (match_len == 15) match_len += lz4_read_length(src, &src_pos, src_len);
        match_len += 4;

        if (dst_pos + match_len > dst_size) return 0;
        for (uint32_t i = 0; i < match_len; i++) {
            dst[dst_pos + i] = dst[dst_pos - match_off + i];
        }
        dst_pos += match_len;
    }

    return dst_pos;
}

/* ============================================================================
 * 公共接口
 * ========================================================================== */

int transcomp_init(void)
{
    if (tc_mgr.initialized) return 0;

    memset(&tc_mgr, 0, sizeof(tc_mgr));
    tc_mgr.total_blocks = TRANSCOMP_MAX_BLOCKS;
    tc_mgr.initialized = 1;

    for (int i = 0; i < TRANSCOMP_MAX_BLOCKS; i++) {
        tc_mgr.block_map[i].state = TRANSCOMP_STATE_EMPTY;
    }

    return 0;
}

int transcomp_write_block(uint32_t block, const void *data)
{
    if (!tc_mgr.initialized || !data || block >= TRANSCOMP_MAX_BLOCKS) return -1;

    transcomp_block_map_t *map = &tc_mgr.block_map[block];

    /* 释放旧数据 */
    if (map->state != TRANSCOMP_STATE_EMPTY && map->comp_data) {
        kfree(map->comp_data);
        map->comp_data = NULL;
        if (map->state == TRANSCOMP_STATE_COMPRESSED) {
            tc_mgr.compressed_blocks--;
            tc_mgr.original_bytes -= TRANSCOMP_BLOCK_SIZE;
            tc_mgr.compressed_bytes -= map->comp_size;
        }
    }

    /* 尝试压缩 */
    static uint8_t comp_buf[TRANSCOMP_BLOCK_SIZE];
    uint32_t comp_size = tc_lz4_compress(data, TRANSCOMP_BLOCK_SIZE, comp_buf, TRANSCOMP_BLOCK_SIZE);

    /* 检查压缩率：只有压缩后明显更小才存储压缩版 */
    if (comp_size > 0 && comp_size < TRANSCOMP_BLOCK_SIZE * TRANSCOMP_COMPRESSED_THRESHOLD) {
        /* 存储压缩版 */
        map->comp_data = (uint8_t *)kmalloc(comp_size);
        if (!map->comp_data) return -2;
        memcpy(map->comp_data, comp_buf, comp_size);
        map->comp_size = comp_size;
        map->orig_block = block;
        map->state = TRANSCOMP_STATE_COMPRESSED;

        tc_mgr.compressed_blocks++;
        tc_mgr.original_bytes += TRANSCOMP_BLOCK_SIZE;
        tc_mgr.compressed_bytes += comp_size;
        tc_mgr.compress_count++;
    } else {
        /* 压缩率差，存储原始数据（标记为未压缩） */
        map->comp_data = (uint8_t *)kmalloc(TRANSCOMP_BLOCK_SIZE);
        if (!map->comp_data) return -2;
        memcpy(map->comp_data, data, TRANSCOMP_BLOCK_SIZE);
        map->comp_size = TRANSCOMP_BLOCK_SIZE;
        map->orig_block = block;
        map->state = TRANSCOMP_STATE_UNCOMPRESSED;

        tc_mgr.skip_count++;
    }

    return 0;
}

int transcomp_read_block(uint32_t block, void *data)
{
    if (!tc_mgr.initialized || !data || block >= TRANSCOMP_MAX_BLOCKS) return -1;

    transcomp_block_map_t *map = &tc_mgr.block_map[block];
    if (map->state == TRANSCOMP_STATE_EMPTY || !map->comp_data) return -2;

    if (map->state == TRANSCOMP_STATE_COMPRESSED) {
        /* 解压 */
        uint32_t decomp_size = tc_lz4_decompress(map->comp_data, map->comp_size,
                                                   data, TRANSCOMP_BLOCK_SIZE);
        if (decomp_size != TRANSCOMP_BLOCK_SIZE) return -3;
        tc_mgr.decompress_count++;
    } else {
        /* 未压缩，直接复制 */
        memcpy(data, map->comp_data, TRANSCOMP_BLOCK_SIZE);
    }

    return 0;
}

int transcomp_discard_block(uint32_t block)
{
    if (!tc_mgr.initialized || block >= TRANSCOMP_MAX_BLOCKS) return -1;

    transcomp_block_map_t *map = &tc_mgr.block_map[block];
    if (map->state == TRANSCOMP_STATE_EMPTY) return 0;

    if (map->comp_data) {
        kfree(map->comp_data);
        map->comp_data = NULL;
    }

    if (map->state == TRANSCOMP_STATE_COMPRESSED) {
        tc_mgr.compressed_blocks--;
        tc_mgr.original_bytes -= TRANSCOMP_BLOCK_SIZE;
        tc_mgr.compressed_bytes -= map->comp_size;
    }

    map->state = TRANSCOMP_STATE_EMPTY;
    map->comp_size = 0;
    return 0;
}

void transcomp_get_stats(transcomp_mgr_t *stats)
{
    if (!stats) return;
    memcpy(stats, &tc_mgr, sizeof(tc_mgr));
}

uint32_t transcomp_get_ratio(void)
{
    if (tc_mgr.original_bytes == 0) return 100;
    return (tc_mgr.compressed_bytes * 100) / tc_mgr.original_bytes;
}

/* ============================================================================
 * 自测
 * ========================================================================== */
void transcomp_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define TCLOG(s) serial_puts(0x3F8, s)

    TCLOG("[TRANSCOMP] selftest: start\n");
    int pass = 0, fail = 0;

    /* 测试1：初始化 */
    int ret = transcomp_init();
    if (ret == 0) {
        TCLOG("[TRANSCOMP] test1 init: PASS\n");
        pass++;
    } else {
        TCLOG("[TRANSCOMP] test1 init: FAIL\n");
        fail++;
    }

    /* 测试2：写入/读取全零块（应压缩） */
    static uint8_t zero_block[4096];
    static uint8_t read_buf[4096];
    memset(zero_block, 0, 4096);

    ret = transcomp_write_block(0, zero_block);
    if (ret == 0) {
        memset(read_buf, 0xFF, 4096);
        ret = transcomp_read_block(0, read_buf);
        if (ret == 0 && memcmp(zero_block, read_buf, 4096) == 0) {
            TCLOG("[TRANSCOMP] test2 zero block write/read: PASS\n");
            pass++;
        } else {
            TCLOG("[TRANSCOMP] test2 zero block read: FAIL\n");
            fail++;
        }
    } else {
        TCLOG("[TRANSCOMP] test2 zero block write: FAIL\n");
        fail++;
    }

    /* 测试3：写入/读取重复模式块（应压缩） */
    static uint8_t pattern_block[4096];
    for (int i = 0; i < 4096; i++) pattern_block[i] = (i % 64 < 32) ? 0xAA : 0x55;

    ret = transcomp_write_block(1, pattern_block);
    if (ret == 0) {
        memset(read_buf, 0, 4096);
        ret = transcomp_read_block(1, read_buf);
        if (ret == 0 && memcmp(pattern_block, read_buf, 4096) == 0) {
            TCLOG("[TRANSCOMP] test3 pattern block write/read: PASS\n");
            pass++;
        } else {
            TCLOG("[TRANSCOMP] test3 pattern block read: FAIL\n");
            fail++;
        }
    } else {
        TCLOG("[TRANSCOMP] test3 pattern block write: FAIL\n");
        fail++;
    }

    /* 测试4：写入/读取随机数据块（可能不压缩） */
    static uint8_t random_block[4096];
    uint32_t seed = 99999;
    for (int i = 0; i < 4096; i++) {
        seed = seed * 1103515245 + 12345;
        random_block[i] = (seed >> 16) & 0xFF;
    }

    ret = transcomp_write_block(2, random_block);
    if (ret == 0) {
        memset(read_buf, 0, 4096);
        ret = transcomp_read_block(2, read_buf);
        if (ret == 0 && memcmp(random_block, read_buf, 4096) == 0) {
            TCLOG("[TRANSCOMP] test4 random block write/read: PASS\n");
            pass++;
        } else {
            TCLOG("[TRANSCOMP] test4 random block read: FAIL\n");
            fail++;
        }
    } else {
        TCLOG("[TRANSCOMP] test4 random block write: FAIL\n");
        fail++;
    }

    /* 测试5：覆盖写入（释放旧数据） */
    static uint8_t new_data[4096];
    memset(new_data, 0xBB, 4096);
    ret = transcomp_write_block(0, new_data);
    if (ret == 0) {
        memset(read_buf, 0, 4096);
        ret = transcomp_read_block(0, read_buf);
        if (ret == 0 && memcmp(new_data, read_buf, 4096) == 0) {
            TCLOG("[TRANSCOMP] test5 overwrite: PASS\n");
            pass++;
        } else {
            TCLOG("[TRANSCOMP] test5 overwrite: FAIL\n");
            fail++;
        }
    } else {
        TCLOG("[TRANSCOMP] test5 overwrite: FAIL\n");
        fail++;
    }

    /* 测试6：丢弃块 */
    ret = transcomp_discard_block(0);
    if (ret == 0) {
        ret = transcomp_read_block(0, read_buf);
        if (ret != 0) {  /* 丢弃后读取应失败 */
            TCLOG("[TRANSCOMP] test6 discard: PASS\n");
            pass++;
        } else {
            TCLOG("[TRANSCOMP] test6 discard: FAIL (read after discard succeeded)\n");
            fail++;
        }
    } else {
        TCLOG("[TRANSCOMP] test6 discard: FAIL\n");
        fail++;
    }

    /* 测试7：统计信息和压缩率 */
    transcomp_mgr_t stats;
    transcomp_get_stats(&stats);
    uint32_t ratio = transcomp_get_ratio();
    if (stats.compress_count >= 2 && ratio <= 100) {
        TCLOG("[TRANSCOMP] test7 stats: PASS (compress=");
        serial_puthex(0x3F8, stats.compress_count);
        TCLOG(", ratio=");
        serial_puthex(0x3F8, ratio);
        TCLOG("%)\n");
        pass++;
    } else {
        TCLOG("[TRANSCOMP] test7 stats: FAIL\n");
        fail++;
    }

    TCLOG("[TRANSCOMP] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    TCLOG(", fail=");
    serial_puthex(0x3F8, fail);
    TCLOG(")\n");

    #undef TCLOG
}
