/* ============================================================================
 * 赫普OS - libpng兼容头文件
 * 简化版libpng接口，支持PNG图片解码
 * V8.26: 初始实现
 * ========================================================================== */

#ifndef _PNG_H
#define _PNG_H

#include <stdint.h>
#include <stddef.h>

/* libpng版本 */
#define PNG_LIBPNG_VER_STRING "1.6.37-hepu"
#define PNG_LIBPNG_VER 10637

/* 错误码 */
#define PNG_OK              0
#define PNG_ERROR          -1
#define PNG_NO_MEMORY      -2
#define PNG_INVALID_SIGNATURE -3
#define PNG_INVALID_IHDR   -4
#define PNG_INVALID_DATA   -5
#define PNG_UNSUPPORTED    -6
#define PNG_CRC_ERROR      -7

/* 颜色类型 */
#define PNG_COLOR_TYPE_GRAY        0
#define PNG_COLOR_TYPE_PALETTE     3
#define PNG_COLOR_TYPE_RGB         2
#define PNG_COLOR_TYPE_RGB_ALPHA   6
#define PNG_COLOR_TYPE_GRAY_ALPHA  4

/* 过滤方法 */
#define PNG_FILTER_NONE     0
#define PNG_FILTER_SUB      1
#define PNG_FILTER_UP       2
#define PNG_FILTER_AVG      3
#define PNG_FILTER_PAETH    4

/* 隔行方法 */
#define PNG_INTERLACE_NONE  0
#define PNG_INTERLACE_ADAM7 1

/* 位深 */
#define PNG_BIT_DEPTH_1   1
#define PNG_BIT_DEPTH_2   2
#define PNG_BIT_DEPTH_4   4
#define PNG_BIT_DEPTH_8   8
#define PNG_BIT_DEPTH_16  16

/* 变换标志 */
#define PNG_TRANSFORM_IDENTITY      0x0000
#define PNG_TRANSFORM_STRIP_16      0x0001
#define PNG_TRANSFORM_STRIP_ALPHA   0x0002
#define PNG_TRANSFORM_PACKING       0x0004
#define PNG_TRANSFORM_PACKSWAP      0x0008
#define PNG_TRANSFORM_EXPAND        0x0010
#define PNG_TRANSFORM_INVERT_MONO   0x0020
#define PNG_TRANSFORM_SHIFT         0x0040
#define PNG_TRANSFORM_BGR           0x0080
#define PNG_TRANSFORM_SWAP_ALPHA    0x0100
#define PNG_TRANSFORM_SWAP_ENDIAN   0x0200
#define PNG_TRANSFORM_INVERT_ALPHA  0x0400
#define PNG_TRANSFORM_RGB_TO_GRAY   0x0800

/* PNG签名 */
#define PNG_SIGNATURE_SIZE 8
extern const unsigned char png_signature[8];

/* 颜色结构 */
typedef struct png_color_struct {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
} png_color;

/* 调色板指针 */
typedef png_color *png_colorp;

/* 图像信息结构 */
typedef struct png_info_struct {
    uint32_t width;           /* 图像宽度 */
    uint32_t height;          /* 图像高度 */
    int bit_depth;            /* 位深 */
    int color_type;           /* 颜色类型 */
    int compression_method;   /* 压缩方法 */
    int filter_method;        /* 过滤方法 */
    int interlace_type;       /* 隔行方法 */
    uint32_t rowbytes;        /* 每行字节数 */
    int channels;             /* 通道数 */
    png_color *palette;       /* 调色板 */
    int num_palette;          /* 调色板颜色数 */
    uint8_t *trans_alpha;     /* 透明alpha值 */
    int num_trans;            /* 透明颜色数 */
    png_color *trans_color;   /* 透明颜色 */
    uint32_t valid;           /* 有效字段标志 */
    uint8_t **row_pointers;   /* 行指针数组 */
} png_info;

typedef png_info *png_infop;

/* 图像结构 */
typedef struct png_struct_struct {
    /* 基本信息 */
    uint32_t width;
    uint32_t height;
    int bit_depth;
    int color_type;
    int interlace_type;

    /* 数据 */
    uint8_t *image_data;      /* 解压后的图像数据（过滤前） */
    uint32_t image_data_size; /* 图像数据大小 */
    uint8_t **row_pointers;   /* 行指针数组 */

    /* 调色板 */
    png_color *palette;
    int num_palette;

    /* 透明 */
    uint8_t *trans_alpha;
    int num_trans;
    png_color *trans_color;

    /* 变换 */
    int transformations;

    /* 错误处理 */
    char error_msg[256];
    int error_code;

    /* 内部状态 */
    int mode;
    uint8_t *zlib_buffer;
    uint32_t zlib_buffer_size;
} png_struct;

typedef png_struct *png_structp;

/* 行指针 */
typedef uint8_t *png_bytep;
typedef png_bytep *png_bytepp;

/* 函数声明 */

/* 基本操作 */
png_structp png_create_read_struct(const char *user_png_ver,
                                    void *error_ptr,
                                    void *error_fn,
                                    void *warn_fn);
png_infop png_create_info_struct(png_structp png_ptr);
void png_destroy_read_struct(png_structp *png_ptr_ptr,
                              png_infop *info_ptr_ptr,
                              png_infop *end_info_ptr_ptr);

/* 读取PNG */
int png_read_png_from_memory(png_structp png_ptr, png_infop info_ptr,
                               const uint8_t *data, uint32_t data_size,
                               int transforms);
int png_sig_cmp(const uint8_t *sig, uint32_t start, uint32_t num_to_check);

/* 获取信息 */
uint32_t png_get_image_width(png_structp png_ptr, png_infop info_ptr);
uint32_t png_get_image_height(png_structp png_ptr, png_infop info_ptr);
int png_get_bit_depth(png_structp png_ptr, png_infop info_ptr);
int png_get_color_type(png_structp png_ptr, png_infop info_ptr);
int png_get_interlace_type(png_structp png_ptr, png_infop info_ptr);
uint32_t png_get_rowbytes(png_structp png_ptr, png_infop info_ptr);
int png_get_channels(png_structp png_ptr, png_infop info_ptr);
png_bytepp png_get_rows(png_structp png_ptr, png_infop info_ptr);
png_colorp png_get_palette(png_structp png_ptr, png_infop info_ptr, int *num_palette);

/* 简化接口 */
typedef struct {
    uint32_t width;
    uint32_t height;
    int bit_depth;
    int color_type;
    int channels;
    uint32_t rowbytes;
    uint8_t *pixels;        /* RGBA格式像素数据 */
    uint32_t pixels_size;
} png_image_t;

int png_decode_simple(const uint8_t *data, uint32_t data_size,
                       png_image_t *image);
void png_image_free(png_image_t *image);

#endif /* _PNG_H */
