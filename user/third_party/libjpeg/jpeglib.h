/* ============================================================================
 * 赫普OS - libjpeg兼容头文件
 * 简化版libjpeg接口，支持JPEG图片解码
 * V8.28: 初始实现
 * ========================================================================== */

#ifndef _JPEGLIB_H
#define _JPEGLIB_H

#include <stdint.h>
#include <stddef.h>

/* libjpeg版本 */
#define JPEG_LIB_VERSION 62
#define JPEG_LIB_VERSION_MAJOR 6
#define JPEG_LIB_VERSION_MINOR 2

/* 错误码 */
#define JPEG_OK              0
#define JPEG_ERROR          -1
#define JPEG_NO_MEMORY      -2
#define JPEG_INVALID_SOI    -3
#define JPEG_UNSUPPORTED    -4
#define JPEG_INVALID_DATA   -5
#define JPEG_DECODE_ERROR   -6

/* 颜色空间 */
#define JCS_UNKNOWN       0
#define JCS_GRAYSCALE     1
#define JCS_RGB           2
#define JCS_YCbCr         3
#define JCS_CMYK          4
#define JCS_YCCK          5

/* DCT方法 */
#define JDCT_ISLOW        0
#define JDCT_IFAST        1
#define JDCT_FLOAT        2

/* 抖动方法 */
#define JDITHER_NONE      0
#define JDITHER_ORDERED   1
#define JDITHER_FS        2

/* Marker代码 */
#define M_SOF0  0xC0  /* 基线DCT */
#define M_SOF1  0xC1
#define M_SOF2  0xC2
#define M_SOF3  0xC3
#define M_DHT   0xC4  /* 定义霍夫曼表 */
#define M_SOI   0xD8  /* 图像开始 */
#define M_EOI   0xD9  /* 图像结束 */
#define M_SOS   0xDA  /* 扫描开始 */
#define M_DQT   0xDB  /* 定义量化表 */
#define M_APP0  0xE0  /* APP0 */
#define M_APP1  0xE1
#define M_COM   0xFE  /* 注释 */

/* 最大组件数 */
#define MAX_COMPONENTS  4
#define MAX_QUANT_TABLES 4
#define MAX_HUFF_TABLES  4

/* 采样因子 */
typedef struct {
    int component_id;
    int h_sampling;  /* 水平采样因子 */
    int v_sampling;  /* 垂直采样因子 */
    int quant_table; /* 量化表索引 */
    int dc_table;    /* DC霍夫曼表索引 */
    int ac_table;    /* AC霍夫曼表索引 */
} jpeg_component_t;

/* 量化表 */
typedef struct {
    uint16_t table[64];
    int precision;  /* 8或16位 */
} jpeg_quant_table_t;

/* 霍夫曼表 */
typedef struct {
    uint8_t counts[17];   /* 每个码长的码数量 */
    uint8_t values[256];  /* 符号值 */
    int num_values;
} jpeg_huff_table_t;

/* 解码上下文 */
typedef struct {
    /* 图像信息 */
    uint32_t width;
    uint32_t height;
    int num_components;
    int color_space;
    int precision;  /* 数据精度（位） */

    /* 组件信息 */
    jpeg_component_t components[MAX_COMPONENTS];
    int max_h_sampling;
    int max_v_sampling;

    /* 量化表 */
    jpeg_quant_table_t quant_tables[MAX_QUANT_TABLES];
    int quant_table_count;

    /* 霍夫曼表 */
    jpeg_huff_table_t dc_tables[MAX_HUFF_TABLES];
    jpeg_huff_table_t ac_tables[MAX_HUFF_TABLES];
    int dc_table_count;
    int ac_table_count;

    /* 扫描数据 */
    const uint8_t *scan_data;
    uint32_t scan_data_size;
    uint32_t scan_data_pos;
    int bit_buffer;
    int bits_left;

    /* 输出 */
    uint8_t *output_data;  /* RGB格式 */
    uint32_t output_size;

    /* 错误 */
    char error_msg[256];
    int error_code;
} jpeg_decompress_t;

/* 简化接口 */
typedef struct {
    uint32_t width;
    uint32_t height;
    int color_space;
    int channels;
    uint8_t *pixels;  /* RGB格式 */
    uint32_t pixels_size;
} jpeg_image_t;

/* 函数声明 */

/* 基本解码 */
int jpeg_decode(const uint8_t *data, uint32_t data_size,
                 jpeg_image_t *image);
void jpeg_image_free(jpeg_image_t *image);

/* 高级接口（libjpeg兼容） */
jpeg_decompress_t *jpeg_decompress_create(void);
void jpeg_decompress_destroy(jpeg_decompress_t *cinfo);
int jpeg_read_header(jpeg_decompress_t *cinfo,
                      const uint8_t *data, uint32_t data_size);
int jpeg_start_decompress(jpeg_decompress_t *cinfo);
int jpeg_read_scanlines(jpeg_decompress_t *cinfo,
                         uint8_t **scanlines, int max_lines);

#endif /* _JPEGLIB_H */
