/* ============================================================================
 * 赫普OS - 简化版libjpeg实现
 * 支持基线JPEG解码（SOF0），灰度和YUV444/YUV420颜色空间
 * V8.28: 初始实现
 * ========================================================================== */

#include "jpeglib.h"

/* 内存分配 */
extern void *malloc(size_t size);
extern void free(void *ptr);

/* 简化的内存操作 */
static void *jpeg_memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void *jpeg_memset(void *dst, int val, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)val;
    return dst;
}

/* 读取大端序16位整数 */
static uint16_t jpeg_read_uint16(const uint8_t *data)
{
    return (data[0] << 8) | data[1];
}

/* 位读取器 */
static int jpeg_get_bit(jpeg_decompress_t *cinfo)
{
    if (cinfo->bits_left == 0) {
        if (cinfo->scan_data_pos >= cinfo->scan_data_size) return -1;
        cinfo->bit_buffer = cinfo->scan_data[cinfo->scan_data_pos++];
        cinfo->bits_left = 8;

        /* 处理字节填充（0xFF 0x00） */
        if (cinfo->bit_buffer == 0xFF) {
            if (cinfo->scan_data_pos < cinfo->scan_data_size) {
                uint8_t next = cinfo->scan_data[cinfo->scan_data_pos];
                if (next == 0x00) {
                    cinfo->scan_data_pos++;  /* 跳过填充字节 */
                } else if (next >= 0xD0 && next <= 0xD7) {
                    /* 重启标记，跳过 */
                    cinfo->scan_data_pos++;
                    cinfo->bit_buffer = cinfo->scan_data[cinfo->scan_data_pos++];
                }
            }
        }
    }

    cinfo->bits_left--;
    return (cinfo->bit_buffer >> cinfo->bits_left) & 1;
}

static int jpeg_get_bits(jpeg_decompress_t *cinfo, int num_bits)
{
    int value = 0;
    for (int i = 0; i < num_bits; i++) {
        int bit = jpeg_get_bit(cinfo);
        if (bit < 0) return -1;
        value = (value << 1) | bit;
    }
    return value;
}

/* 霍夫曼解码 */
static int jpeg_huff_decode(jpeg_decompress_t *cinfo,
                             const jpeg_huff_table_t *table)
{
    int code = 0;
    int first = 0;
    int index = 0;

    for (int bits = 1; bits <= 16; bits++) {
        int bit = jpeg_get_bit(cinfo);
        if (bit < 0) return -1;

        code = (code << 1) | bit;
        int count = table->counts[bits];

        if (count > 0 && code - first < count) {
            return table->values[index + (code - first)];
        }

        index += count;
        first += count;
        first <<= 1;
    }

    return -1;
}

/* 接收DC差值 */
static int jpeg_receive_extend(jpeg_decompress_t *cinfo, int ssss)
{
    if (ssss == 0) return 0;

    int value = jpeg_get_bits(cinfo, ssss);
    if (value < 0) return 0;

    /* 扩展符号 */
    if (value < (1 << (ssss - 1))) {
        value += (-1 << ssss) + 1;
    }

    return value;
}

/* IDCT（反离散余弦变换）- 简化的AAN算法 */
static void jpeg_idct(int *block)
{
    /* 使用浮点IDCT（简化实现） */
    float tmp[64];
    float result[64];

    /* 常量 */
    static const float c[8] = {
        0.3535533906f, 0.4903926402f, 0.4619397663f, 0.4157348062f,
        0.3535533906f, 0.2777851165f, 0.1913417162f, 0.0975451610f
    };

    /* 行方向IDCT */
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            float sum = 0;
            for (int u = 0; u < 8; u++) {
                sum += block[y * 8 + u] * c[u] *
                       cosf((2 * x + 1) * u * 3.14159265358979323846f / 16.0f);
            }
            tmp[y * 8 + x] = sum;
        }
    }

    /* 列方向IDCT */
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            float sum = 0;
            for (int v = 0; v < 8; v++) {
                sum += tmp[v * 8 + x] * c[v] *
                       cosf((2 * y + 1) * v * 3.14159265358979323846f / 16.0f);
            }
            result[y * 8 + x] = sum;
        }
    }

    /* 转换为整数并限幅 */
    for (int i = 0; i < 64; i++) {
        int val = (int)(result[i] + 128.0f + 0.5f);
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        block[i] = val;
    }
}

/* 简化的整数IDCT（不使用浮点） */
static void jpeg_idct_fast(int *block)
{
    /* 使用固定的整数IDCT（简化版，精度稍低但速度快） */
    /* 这里使用一个简化的近似算法 */
    int tmp[64];

    /* 行方向 */
    for (int y = 0; y < 8; y++) {
        int *row = &block[y * 8];
        int *out = &tmp[y * 8];

        int x0 = row[0];
        int x1 = row[1];
        int x2 = row[2];
        int x3 = row[3];
        int x4 = row[4];
        int x5 = row[5];
        int x6 = row[6];
        int x7 = row[7];

        /* 简化的8点IDCT（使用固定点近似） */
        int a0 = x0 + x4;
        int a1 = x0 - x4;
        int a2 = x2 + x6;
        int a3 = x2 - x6;

        int b0 = a0 + a2;
        int b1 = a0 - a2;
        int b2 = a1 + (a3 >> 1);
        int b3 = a1 - (a3 >> 1);

        out[0] = (b0 + x1 + x3 + x5 + x7) >> 1;
        out[1] = (b2 + x1 - x3 - x5 + x7) >> 1;
        out[2] = (b1 + x1 - x3 + x5 - x7) >> 1;
        out[3] = (b3 + x1 + x3 - x5 - x7) >> 1;
        out[4] = (b0 - x1 - x3 - x5 - x7) >> 1;
        out[5] = (b3 - x1 - x3 + x5 + x7) >> 1;
        out[6] = (b1 - x1 + x3 - x5 + x7) >> 1;
        out[7] = (b2 - x1 + x3 + x5 - x7) >> 1;
    }

    /* 列方向 */
    for (int x = 0; x < 8; x++) {
        int y0 = tmp[0 * 8 + x];
        int y1 = tmp[1 * 8 + x];
        int y2 = tmp[2 * 8 + x];
        int y3 = tmp[3 * 8 + x];
        int y4 = tmp[4 * 8 + x];
        int y5 = tmp[5 * 8 + x];
        int y6 = tmp[6 * 8 + x];
        int y7 = tmp[7 * 8 + x];

        int a0 = y0 + y4;
        int a1 = y0 - y4;
        int a2 = y2 + y6;
        int a3 = y2 - y6;

        int b0 = a0 + a2;
        int b1 = a0 - a2;
        int b2 = a1 + (a3 >> 1);
        int b3 = a1 - (a3 >> 1);

        block[0 * 8 + x] = ((b0 + y1 + y3 + y5 + y7) >> 1) + 128;
        block[1 * 8 + x] = ((b2 + y1 - y3 - y5 + y7) >> 1) + 128;
        block[2 * 8 + x] = ((b1 + y1 - y3 + y5 - y7) >> 1) + 128;
        block[3 * 8 + x] = ((b3 + y1 + y3 - y5 - y7) >> 1) + 128;
        block[4 * 8 + x] = ((b0 - y1 - y3 - y5 - y7) >> 1) + 128;
        block[5 * 8 + x] = ((b3 - y1 - y3 + y5 + y7) >> 1) + 128;
        block[6 * 8 + x] = ((b1 - y1 + y3 - y5 + y7) >> 1) + 128;
        block[7 * 8 + x] = ((b2 - y1 + y3 + y5 - y7) >> 1) + 128;
    }

    /* 限幅 */
    for (int i = 0; i < 64; i++) {
        if (block[i] < 0) block[i] = 0;
        if (block[i] > 255) block[i] = 255;
    }
}

/* YCbCr转RGB */
static void ycbcr_to_rgb(int y, int cb, int cr,
                          uint8_t *r, uint8_t *g, uint8_t *b)
{
    int rr = y + (int)(1.402f * (cr - 128));
    int gg = y - (int)(0.344136f * (cb - 128)) - (int)(0.714136f * (cr - 128));
    int bb = y + (int)(1.772f * (cb - 128));

    if (rr < 0) rr = 0; if (rr > 255) rr = 255;
    if (gg < 0) gg = 0; if (gg > 255) gg = 255;
    if (bb < 0) bb = 0; if (bb > 255) bb = 255;

    *r = (uint8_t)rr;
    *g = (uint8_t)gg;
    *b = (uint8_t)bb;
}

/* 解析DQT（定义量化表） */
static int jpeg_parse_dqt(jpeg_decompress_t *cinfo,
                           const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0;

    while (offset < length) {
        uint8_t precision_and_id = data[offset++];
        int precision = (precision_and_id >> 4) & 0x0F;
        int table_id = precision_and_id & 0x0F;

        if (table_id >= MAX_QUANT_TABLES) return JPEG_UNSUPPORTED;

        jpeg_quant_table_t *table = &cinfo->quant_tables[table_id];
        table->precision = precision;

        /* Zigzag顺序的量化表 */
        static const int zigzag[64] = {
             0,  1,  8, 16,  9,  2,  3, 10,
            17, 24, 32, 25, 18, 11,  4,  5,
            12, 19, 26, 33, 40, 48, 41, 34,
            27, 20, 13,  6,  7, 14, 21, 28,
            35, 42, 49, 56, 57, 50, 43, 36,
            29, 22, 15, 23, 30, 37, 44, 51,
            58, 59, 52, 45, 38, 31, 39, 46,
            53, 60, 61, 54, 47, 55, 62, 63
        };

        for (int i = 0; i < 64; i++) {
            if (precision == 0) {
                table->table[zigzag[i]] = data[offset++];
            } else {
                table->table[zigzag[i]] = (data[offset] << 8) | data[offset + 1];
                offset += 2;
            }
        }

        if (table_id >= cinfo->quant_table_count) {
            cinfo->quant_table_count = table_id + 1;
        }
    }

    return JPEG_OK;
}

/* 解析SOF0（帧信息） */
static int jpeg_parse_sof0(jpeg_decompress_t *cinfo,
                            const uint8_t *data, uint32_t length)
{
    if (length < 8) return JPEG_INVALID_DATA;

    cinfo->precision = data[0];
    cinfo->height = jpeg_read_uint16(data + 1);
    cinfo->width = jpeg_read_uint16(data + 3);
    cinfo->num_components = data[5];

    if (cinfo->num_components > MAX_COMPONENTS) return JPEG_UNSUPPORTED;
    if (cinfo->num_components == 1) {
        cinfo->color_space = JCS_GRAYSCALE;
    } else if (cinfo->num_components == 3) {
        cinfo->color_space = JCS_YCbCr;
    } else {
        cinfo->color_space = JCS_UNKNOWN;
    }

    cinfo->max_h_sampling = 1;
    cinfo->max_v_sampling = 1;

    for (int i = 0; i < cinfo->num_components; i++) {
        int offset = 6 + i * 3;
        if (offset + 3 > (int)length) return JPEG_INVALID_DATA;

        cinfo->components[i].component_id = data[offset];
        cinfo->components[i].h_sampling = (data[offset + 1] >> 4) & 0x0F;
        cinfo->components[i].v_sampling = data[offset + 1] & 0x0F;
        cinfo->components[i].quant_table = data[offset + 2];

        if (cinfo->components[i].h_sampling > cinfo->max_h_sampling)
            cinfo->max_h_sampling = cinfo->components[i].h_sampling;
        if (cinfo->components[i].v_sampling > cinfo->max_v_sampling)
            cinfo->max_v_sampling = cinfo->components[i].v_sampling;
    }

    return JPEG_OK;
}

/* 解析DHT（定义霍夫曼表） */
static int jpeg_parse_dht(jpeg_decompress_t *cinfo,
                           const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0;

    while (offset < length) {
        uint8_t table_class_and_id = data[offset++];
        int table_class = (table_class_and_id >> 4) & 0x0F;
        int table_id = table_class_and_id & 0x0F;

        jpeg_huff_table_t *table;
        if (table_class == 0) {
            if (table_id >= MAX_HUFF_TABLES) return JPEG_UNSUPPORTED;
            table = &cinfo->dc_tables[table_id];
            if (table_id >= cinfo->dc_table_count)
                cinfo->dc_table_count = table_id + 1;
        } else {
            if (table_id >= MAX_HUFF_TABLES) return JPEG_UNSUPPORTED;
            table = &cinfo->ac_tables[table_id];
            if (table_id >= cinfo->ac_table_count)
                cinfo->ac_table_count = table_id + 1;
        }

        /* 读取每个码长的数量 */
        int total = 0;
        for (int i = 1; i <= 16; i++) {
            table->counts[i] = data[offset++];
            total += table->counts[i];
        }

        /* 读取符号值 */
        table->num_values = total;
        for (int i = 0; i < total && i < 256; i++) {
            table->values[i] = data[offset++];
        }
    }

    return JPEG_OK;
}

/* 解析SOS（扫描开始） */
static int jpeg_parse_sos(jpeg_decompress_t *cinfo,
                           const uint8_t *data, uint32_t length,
                           const uint8_t *jpeg_data, uint32_t jpeg_size,
                           uint32_t *scan_offset)
{
    if (length < 4) return JPEG_INVALID_DATA;

    int num_components = data[0];

    for (int i = 0; i < num_components; i++) {
        int offset = 1 + i * 2;
        if (offset + 2 > (int)length) return JPEG_INVALID_DATA;

        int component_id = data[offset];
        int table_select = data[offset + 1];

        /* 找到对应的组件 */
        for (int j = 0; j < cinfo->num_components; j++) {
            if (cinfo->components[j].component_id == component_id) {
                cinfo->components[j].dc_table = (table_select >> 4) & 0x0F;
                cinfo->components[j].ac_table = table_select & 0x0F;
                break;
            }
        }
    }

    /* 扫描数据从SOS段之后开始 */
    *scan_offset = (uint32_t)(data - jpeg_data) + length;

    /* 找到EOI标记，确定扫描数据结束位置 */
    uint32_t end_pos = *scan_offset;
    while (end_pos + 1 < jpeg_size) {
        if (jpeg_data[end_pos] == 0xFF && jpeg_data[end_pos + 1] == M_EOI) {
            break;
        }
        end_pos++;
    }

    cinfo->scan_data = jpeg_data + *scan_offset;
    cinfo->scan_data_size = end_pos - *scan_offset;
    cinfo->scan_data_pos = 0;
    cinfo->bit_buffer = 0;
    cinfo->bits_left = 0;

    return JPEG_OK;
}

/* 解码一个MCU（最小编码单元） */
static int jpeg_decode_mcu(jpeg_decompress_t *cinfo,
                            int *dc_predictions,
                            int mcu_x, int mcu_y)
{
    #define MAX_BLOCKS_PER_MCU 10
    int blocks[MAX_BLOCKS_PER_MCU][64];
    int block_count = 0;

    /* 解码每个组件的块 */
    for (int comp = 0; comp < cinfo->num_components; comp++) {
        jpeg_component_t *component = &cinfo->components[comp];
        jpeg_quant_table_t *quant = &cinfo->quant_tables[component->quant_table];
        jpeg_huff_table_t *dc_table = &cinfo->dc_tables[component->dc_table];
        jpeg_huff_table_t *ac_table = &cinfo->ac_tables[component->ac_table];

        int num_blocks = component->h_sampling * component->v_sampling;

        for (int b = 0; b < num_blocks; b++) {
            int *block = blocks[block_count++];
            jpeg_memset(block, 0, sizeof(int) * 64);

            /* 解码DC系数 */
            int dc_value = jpeg_huff_decode(cinfo, dc_table);
            if (dc_value < 0) return JPEG_DECODE_ERROR;

            int dc_diff = jpeg_receive_extend(cinfo, dc_value);
            dc_predictions[comp] += dc_diff;
            block[0] = dc_predictions[comp] * quant->table[0];

            /* 解码AC系数 */
            int index = 1;
            while (index < 64) {
                int ac_value = jpeg_huff_decode(cinfo, ac_table);
                if (ac_value < 0) return JPEG_DECODE_ERROR;

                if (ac_value == 0x00) {
                    /* EOB（块结束） */
                    break;
                }

                int run_length = (ac_value >> 4) & 0x0F;
                int ssss = ac_value & 0x0F;

                if (ssss == 0x0F && run_length == 0x0F) {
                    /* ZRL（16个零） */
                    index += 16;
                    continue;
                }

                index += run_length;
                if (index >= 64) break;

                int ac_coeff = jpeg_receive_extend(cinfo, ssss);

                /* Zigzag顺序 */
                static const int zigzag[64] = {
                     0,  1,  8, 16,  9,  2,  3, 10,
                    17, 24, 32, 25, 18, 11,  4,  5,
                    12, 19, 26, 33, 40, 48, 41, 34,
                    27, 20, 13,  6,  7, 14, 21, 28,
                    35, 42, 49, 56, 57, 50, 43, 36,
                    29, 22, 15, 23, 30, 37, 44, 51,
                    58, 59, 52, 45, 38, 31, 39, 46,
                    53, 60, 61, 54, 47, 55, 62, 63
                };

                block[zigzag[index]] = ac_coeff * quant->table[zigzag[index]];
                index++;
            }

            /* IDCT */
            jpeg_idct_fast(block);
        }
    }

    /* 将解码后的块转换为RGB像素 */
    /* 简化：只支持YUV444（每个组件1个块）和灰度 */
    if (cinfo->num_components == 1) {
        /* 灰度 */
        int *y_block = blocks[0];
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int pixel_x = mcu_x * 8 + x;
                int pixel_y = mcu_y * 8 + y;
                if (pixel_x < (int)cinfo->width && pixel_y < (int)cinfo->height) {
                    uint32_t offset = (pixel_y * cinfo->width + pixel_x) * 3;
                    uint8_t gray = (uint8_t)y_block[y * 8 + x];
                    cinfo->output_data[offset] = gray;
                    cinfo->output_data[offset + 1] = gray;
                    cinfo->output_data[offset + 2] = gray;
                }
            }
        }
    } else if (cinfo->num_components == 3 &&
               cinfo->max_h_sampling == 1 && cinfo->max_v_sampling == 1) {
        /* YUV444 */
        int *y_block = blocks[0];
        int *cb_block = blocks[1];
        int *cr_block = blocks[2];

        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int pixel_x = mcu_x * 8 + x;
                int pixel_y = mcu_y * 8 + y;
                if (pixel_x < (int)cinfo->width && pixel_y < (int)cinfo->height) {
                    uint32_t offset = (pixel_y * cinfo->width + pixel_x) * 3;
                    uint8_t r, g, b;
                    ycbcr_to_rgb(y_block[y * 8 + x],
                                  cb_block[y * 8 + x],
                                  cr_block[y * 8 + x],
                                  &r, &g, &b);
                    cinfo->output_data[offset] = r;
                    cinfo->output_data[offset + 1] = g;
                    cinfo->output_data[offset + 2] = b;
                }
            }
        }
    } else {
        /* YUV420或其他子采样 */
        /* YUV420: 4个Y块(2x2) + 1个Cb块 + 1个Cr块 */
        int *y00 = blocks[0];  /* 左上 */
        int *y01 = blocks[1];  /* 右上 */
        int *y10 = blocks[2];  /* 左下 */
        int *y11 = blocks[3];  /* 右下 */
        int *cb_block = blocks[4];
        int *cr_block = blocks[5];

        int mcu_w = 8 * cinfo->max_h_sampling;
        int mcu_h = 8 * cinfo->max_v_sampling;

        for (int y = 0; y < mcu_h; y++) {
            for (int x = 0; x < mcu_w; x++) {
                int pixel_x = mcu_x * mcu_w + x;
                int pixel_y = mcu_y * mcu_h + y;
                if (pixel_x < (int)cinfo->width && pixel_y < (int)cinfo->height) {
                    uint32_t offset = (pixel_y * cinfo->width + pixel_x) * 3;

                    /* 选择对应的Y块 */
                    int *y_block;
                    int local_x = x % 8;
                    int local_y = y % 8;
                    if (x < 8 && y < 8) y_block = y00;
                    else if (x >= 8 && y < 8) y_block = y01;
                    else if (x < 8 && y >= 8) y_block = y10;
                    else y_block = y11;

                    int y_val = y_block[local_y * 8 + local_x];
                    int cb_val = cb_block[(y / 2) * 8 + (x / 2)];
                    int cr_val = cr_block[(y / 2) * 8 + (x / 2)];

                    uint8_t r, g, b;
                    ycbcr_to_rgb(y_val, cb_val, cr_val, &r, &g, &b);
                    cinfo->output_data[offset] = r;
                    cinfo->output_data[offset + 1] = g;
                    cinfo->output_data[offset + 2] = b;
                }
            }
        }
    }

    return JPEG_OK;
}

/* 主解码函数 */
int jpeg_decode(const uint8_t *data, uint32_t data_size,
                 jpeg_image_t *image)
{
    if (!data || !image || data_size < 4) return JPEG_ERROR;

    jpeg_memset(image, 0, sizeof(jpeg_image_t));

    /* 验证SOI标记 */
    if (data[0] != 0xFF || data[1] != M_SOI) {
        return JPEG_INVALID_SOI;
    }

    jpeg_decompress_t cinfo;
    jpeg_memset(&cinfo, 0, sizeof(cinfo));

    uint32_t pos = 2;  /* 跳过SOI */
    int sof_found = 0;
    int sos_found = 0;
    uint32_t scan_offset = 0;

    /* 解析所有marker */
    while (pos + 1 < data_size) {
        if (data[pos] != 0xFF) {
            pos++;
            continue;
        }

        uint8_t marker = data[pos + 1];
        pos += 2;

        if (marker == M_EOI) break;

        /* 读取段长度 */
        if (pos + 2 > data_size) break;
        uint16_t length = jpeg_read_uint16(data + pos);
        if (length < 2) break;

        const uint8_t *segment_data = data + pos + 2;
        uint32_t segment_length = length - 2;

        switch (marker) {
            case M_DQT:
                jpeg_parse_dqt(&cinfo, segment_data, segment_length);
                break;

            case M_SOF0:
                jpeg_parse_sof0(&cinfo, segment_data, segment_length);
                sof_found = 1;
                break;

            case M_DHT:
                jpeg_parse_dht(&cinfo, segment_data, segment_length);
                break;

            case M_SOS:
                jpeg_parse_sos(&cinfo, segment_data, segment_length,
                                data, data_size, &scan_offset);
                sos_found = 1;
                break;

            default:
                /* 跳过其他段（APP0、APP1、COM等） */
                break;
        }

        pos += length;

        if (sos_found) break;
    }

    if (!sof_found || !sos_found) {
        return JPEG_INVALID_DATA;
    }

    /* 分配输出缓冲区 */
    image->width = cinfo.width;
    image->height = cinfo.height;
    image->color_space = JCS_RGB;
    image->channels = 3;
    image->pixels_size = cinfo.width * cinfo.height * 3;
    image->pixels = (uint8_t *)malloc(image->pixels_size);
    if (!image->pixels) return JPEG_NO_MEMORY;

    cinfo.output_data = image->pixels;
    cinfo.output_size = image->pixels_size;

    /* 解码MCU */
    int dc_predictions[MAX_COMPONENTS] = {0, 0, 0, 0};

    int mcu_width = 8 * cinfo.max_h_sampling;
    int mcu_height = 8 * cinfo.max_v_sampling;
    int num_mcu_x = (cinfo.width + mcu_width - 1) / mcu_width;
    int num_mcu_y = (cinfo.height + mcu_height - 1) / mcu_height;

    for (int mcu_y = 0; mcu_y < num_mcu_y; mcu_y++) {
        for (int mcu_x = 0; mcu_x < num_mcu_x; mcu_x++) {
            int ret = jpeg_decode_mcu(&cinfo, dc_predictions, mcu_x, mcu_y);
            if (ret != JPEG_OK) {
                /* 解码错误，返回已解码的部分 */
                break;
            }
        }
    }

    return JPEG_OK;
}

/* 释放图像 */
void jpeg_image_free(jpeg_image_t *image)
{
    if (image && image->pixels) {
        free(image->pixels);
        image->pixels = NULL;
    }
}

/* libjpeg兼容接口 */
jpeg_decompress_t *jpeg_decompress_create(void)
{
    jpeg_decompress_t *cinfo = (jpeg_decompress_t *)malloc(sizeof(jpeg_decompress_t));
    if (cinfo) jpeg_memset(cinfo, 0, sizeof(jpeg_decompress_t));
    return cinfo;
}

void jpeg_decompress_destroy(jpeg_decompress_t *cinfo)
{
    if (cinfo) {
        if (cinfo->output_data) free(cinfo->output_data);
        free(cinfo);
    }
}

int jpeg_read_header(jpeg_decompress_t *cinfo,
                      const uint8_t *data, uint32_t data_size)
{
    /* 简化：直接调用jpeg_decode的头部解析部分 */
    if (!cinfo || !data || data_size < 4) return JPEG_ERROR;
    if (data[0] != 0xFF || data[1] != M_SOI) return JPEG_INVALID_SOI;

    /* 这里简化处理，完整实现需要解析头部 */
    return JPEG_OK;
}

int jpeg_start_decompress(jpeg_decompress_t *cinfo)
{
    if (!cinfo) return JPEG_ERROR;
    return JPEG_OK;
}

int jpeg_read_scanlines(jpeg_decompress_t *cinfo,
                         uint8_t **scanlines, int max_lines)
{
    /* 简化接口，完整实现需要逐行输出 */
    (void)cinfo;
    (void)scanlines;
    (void)max_lines;
    return 0;
}
