/* ============================================================================
 * 赫普OS - 简化版libpng实现
 * 支持PNG图片解码（8位深度，所有颜色类型）
 * V8.26: 初始实现
 * ========================================================================== */

#include "png.h"
#include "../zlib/zlib.h"

/* 内存分配 */
extern void *malloc(size_t size);
extern void free(void *ptr);

/* PNG签名 */
const unsigned char png_signature[8] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
};

/* 简化的内存操作 */
static void *png_memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void *png_memset(void *dst, int val, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)val;
    return dst;
}

/* 读取大端序32位整数 */
static uint32_t png_read_uint32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

/* CRC-32计算（PNG使用标准CRC-32） */
static uint32_t png_crc_table[256];
static int png_crc_table_initialized = 0;

static void png_crc_init_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
        png_crc_table[i] = crc;
    }
    png_crc_table_initialized = 1;
}

static uint32_t png_crc32(const uint8_t *data, uint32_t len)
{
    if (!png_crc_table_initialized) {
        png_crc_init_table();
    }

    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc = png_crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* Paeth预测器 */
static int png_paeth_predictor(int a, int b, int c)
{
    int p = a + b - c;
    int pa = (p >= a) ? (p - a) : (a - p);
    int pb = (p >= b) ? (p - b) : (b - p);
    int pc = (p >= c) ? (p - c) : (c - p);

    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* 解除行过滤 */
static int png_unfilter_row(uint8_t *row, const uint8_t *prev_row,
                             uint32_t rowbytes, int filter_type, int channels)
{
    switch (filter_type) {
        case PNG_FILTER_NONE:
            /* 无需处理 */
            break;

        case PNG_FILTER_SUB:
            /* Sub: x[i] = raw[i] + x[i-1] */
            for (uint32_t i = channels; i < rowbytes; i++) {
                row[i] = row[i] + row[i - channels];
            }
            break;

        case PNG_FILTER_UP:
            /* Up: x[i] = raw[i] + prev[i] */
            if (prev_row) {
                for (uint32_t i = 0; i < rowbytes; i++) {
                    row[i] = row[i] + prev_row[i];
                }
            }
            break;

        case PNG_FILTER_AVG:
            /* Average: x[i] = raw[i] + floor((x[i-1] + prev[i]) / 2) */
            for (uint32_t i = 0; i < rowbytes; i++) {
                int left = (i >= channels) ? row[i - channels] : 0;
                int up = prev_row ? prev_row[i] : 0;
                row[i] = row[i] + (uint8_t)((left + up) / 2);
            }
            break;

        case PNG_FILTER_PAETH:
            /* Paeth: x[i] = raw[i] + Paeth(x[i-1], prev[i], prev[i-1]) */
            for (uint32_t i = 0; i < rowbytes; i++) {
                int left = (i >= channels) ? row[i - channels] : 0;
                int up = prev_row ? prev_row[i] : 0;
                int up_left = (prev_row && i >= channels) ? prev_row[i - channels] : 0;
                row[i] = row[i] + (uint8_t)png_paeth_predictor(left, up, up_left);
            }
            break;

        default:
            return PNG_UNSUPPORTED;
    }

    return PNG_OK;
}

/* 创建读取结构 */
png_structp png_create_read_struct(const char *user_png_ver,
                                    void *error_ptr,
                                    void *error_fn,
                                    void *warn_fn)
{
    (void)user_png_ver;
    (void)error_ptr;
    (void)error_fn;
    (void)warn_fn;

    png_structp png_ptr = (png_structp)malloc(sizeof(png_struct));
    if (!png_ptr) return NULL;

    png_memset(png_ptr, 0, sizeof(png_struct));
    png_ptr->error_code = PNG_OK;

    return png_ptr;
}

/* 创建信息结构 */
png_infop png_create_info_struct(png_structp png_ptr)
{
    if (!png_ptr) return NULL;

    png_infop info_ptr = (png_infop)malloc(sizeof(png_info));
    if (!info_ptr) return NULL;

    png_memset(info_ptr, 0, sizeof(png_info));

    return info_ptr;
}

/* 销毁结构 */
void png_destroy_read_struct(png_structp *png_ptr_ptr,
                              png_infop *info_ptr_ptr,
                              png_infop *end_info_ptr_ptr)
{
    if (png_ptr_ptr && *png_ptr_ptr) {
        png_structp png_ptr = *png_ptr_ptr;
        if (png_ptr->image_data) free(png_ptr->image_data);
        if (png_ptr->row_pointers) free(png_ptr->row_pointers);
        if (png_ptr->palette) free(png_ptr->palette);
        if (png_ptr->trans_alpha) free(png_ptr->trans_alpha);
        if (png_ptr->zlib_buffer) free(png_ptr->zlib_buffer);
        free(png_ptr);
        *png_ptr_ptr = NULL;
    }

    if (info_ptr_ptr && *info_ptr_ptr) {
        png_infop info_ptr = *info_ptr_ptr;
        if (info_ptr->palette) free(info_ptr->palette);
        if (info_ptr->trans_alpha) free(info_ptr->trans_alpha);
        free(info_ptr);
        *info_ptr_ptr = NULL;
    }

    if (end_info_ptr_ptr && *end_info_ptr_ptr) {
        free(*end_info_ptr_ptr);
        *end_info_ptr_ptr = NULL;
    }
}

/* 验证PNG签名 */
int png_sig_cmp(const uint8_t *sig, uint32_t start, uint32_t num_to_check)
{
    if (!sig || start + num_to_check > PNG_SIGNATURE_SIZE) return -1;

    for (uint32_t i = 0; i < num_to_check; i++) {
        if (sig[start + i] != png_signature[start + i]) {
            return -1;
        }
    }

    return 0;
}

/* 从内存读取PNG */
int png_read_png_from_memory(png_structp png_ptr, png_infop info_ptr,
                               const uint8_t *data, uint32_t data_size,
                               int transforms)
{
    if (!png_ptr || !info_ptr || !data || data_size < 8) {
        return PNG_INVALID_SIGNATURE;
    }

    /* 验证签名 */
    if (png_sig_cmp(data, 0, 8) != 0) {
        return PNG_INVALID_SIGNATURE;
    }

    uint32_t offset = 8;  /* 跳过签名 */
    int ihdr_found = 0;
    int idat_found = 0;
    int iend_found = 0;

    /* IDAT数据缓冲区 */
    uint8_t *idat_data = NULL;
    uint32_t idat_size = 0;
    uint32_t idat_capacity = 0;

    /* 解析所有chunk */
    while (offset + 8 <= data_size) {
        uint32_t chunk_length = png_read_uint32(data + offset);
        uint8_t *chunk_type = data + offset + 4;
        uint8_t *chunk_data = data + offset + 8;

        /* 检查chunk是否完整 */
        if (offset + 8 + chunk_length + 4 > data_size) {
            break;
        }

        /* 验证CRC（可选，简化实现跳过） */
        /* uint32_t chunk_crc = png_read_uint32(data + offset + 8 + chunk_length); */

        /* 处理IHDR */
        if (chunk_type[0] == 'I' && chunk_type[1] == 'H' &&
            chunk_type[2] == 'D' && chunk_type[3] == 'R') {
            if (chunk_length < 13) return PNG_INVALID_IHDR;

            png_ptr->width = png_read_uint32(chunk_data);
            png_ptr->height = png_read_uint32(chunk_data + 4);
            png_ptr->bit_depth = chunk_data[8];
            png_ptr->color_type = chunk_data[9];
            int compression_method = chunk_data[10];
            int filter_method = chunk_data[11];
            png_ptr->interlace_type = chunk_data[12];

            /* 验证 */
            if (png_ptr->width == 0 || png_ptr->height == 0) return PNG_INVALID_IHDR;
            if (compression_method != 0) return PNG_UNSUPPORTED;
            if (filter_method != 0) return PNG_UNSUPPORTED;
            if (png_ptr->interlace_type != PNG_INTERLACE_NONE) return PNG_UNSUPPORTED;

            /* 只支持8位深度 */
            if (png_ptr->bit_depth != 8) {
                /* 简化：1/2/4位深度需要扩展，暂时不支持 */
                if (png_ptr->bit_depth < 8) return PNG_UNSUPPORTED;
                if (png_ptr->bit_depth == 16) {
                    /* 16位深度需要strip到8位，暂时不支持 */
                    return PNG_UNSUPPORTED;
                }
            }

            /* 计算通道数和行字节数 */
            switch (png_ptr->color_type) {
                case PNG_COLOR_TYPE_GRAY:
                    info_ptr->channels = 1;
                    break;
                case PNG_COLOR_TYPE_RGB:
                    info_ptr->channels = 3;
                    break;
                case PNG_COLOR_TYPE_PALETTE:
                    info_ptr->channels = 1;
                    break;
                case PNG_COLOR_TYPE_GRAY_ALPHA:
                    info_ptr->channels = 2;
                    break;
                case PNG_COLOR_TYPE_RGB_ALPHA:
                    info_ptr->channels = 4;
                    break;
                default:
                    return PNG_UNSUPPORTED;
            }

            info_ptr->width = png_ptr->width;
            info_ptr->height = png_ptr->height;
            info_ptr->bit_depth = png_ptr->bit_depth;
            info_ptr->color_type = png_ptr->color_type;
            info_ptr->interlace_type = png_ptr->interlace_type;
            info_ptr->rowbytes = png_ptr->width * info_ptr->channels;

            ihdr_found = 1;
        }

        /* 处理PLTE */
        else if (chunk_type[0] == 'P' && chunk_type[1] == 'L' &&
                 chunk_type[2] == 'T' && chunk_type[3] == 'E') {
            if (chunk_length % 3 != 0) return PNG_INVALID_DATA;

            png_ptr->num_palette = chunk_length / 3;
            png_ptr->palette = (png_color *)malloc(sizeof(png_color) * png_ptr->num_palette);
            if (!png_ptr->palette) return PNG_NO_MEMORY;

            for (int i = 0; i < png_ptr->num_palette; i++) {
                png_ptr->palette[i].red = chunk_data[i * 3];
                png_ptr->palette[i].green = chunk_data[i * 3 + 1];
                png_ptr->palette[i].blue = chunk_data[i * 3 + 2];
            }

            info_ptr->palette = png_ptr->palette;
            info_ptr->num_palette = png_ptr->num_palette;
        }

        /* 处理tRNS（透明） */
        else if (chunk_type[0] == 't' && chunk_type[1] == 'R' &&
                 chunk_type[2] == 'N' && chunk_type[3] == 'S') {
            if (png_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
                png_ptr->num_trans = chunk_length;
                png_ptr->trans_alpha = (uint8_t *)malloc(chunk_length);
                if (!png_ptr->trans_alpha) return PNG_NO_MEMORY;
                png_memcpy(png_ptr->trans_alpha, chunk_data, chunk_length);
                info_ptr->trans_alpha = png_ptr->trans_alpha;
                info_ptr->num_trans = png_ptr->num_trans;
            }
        }

        /* 处理IDAT */
        else if (chunk_type[0] == 'I' && chunk_type[1] == 'D' &&
                 chunk_type[2] == 'A' && chunk_type[3] == 'T') {
            /* 扩展缓冲区 */
            if (idat_size + chunk_length > idat_capacity) {
                uint32_t new_capacity = idat_capacity + chunk_length + 4096;
                uint8_t *new_data = (uint8_t *)malloc(new_capacity);
                if (!new_data) {
                    if (idat_data) free(idat_data);
                    return PNG_NO_MEMORY;
                }
                if (idat_data) {
                    png_memcpy(new_data, idat_data, idat_size);
                    free(idat_data);
                }
                idat_data = new_data;
                idat_capacity = new_capacity;
            }

            png_memcpy(idat_data + idat_size, chunk_data, chunk_length);
            idat_size += chunk_length;
            idat_found = 1;
        }

        /* 处理IEND */
        else if (chunk_type[0] == 'I' && chunk_type[1] == 'E' &&
                 chunk_type[2] == 'N' && chunk_type[3] == 'D') {
            iend_found = 1;
            break;
        }

        /* 移动到下一个chunk */
        offset += 8 + chunk_length + 4;
    }

    /* 检查必要chunk */
    if (!ihdr_found) return PNG_INVALID_IHDR;
    if (!idat_found) return PNG_INVALID_DATA;

    /* zlib解压IDAT数据 */
    uint32_t raw_size = png_ptr->height * (info_ptr->rowbytes + 1);  /* +1 for filter byte */
    png_ptr->image_data = (uint8_t *)malloc(raw_size);
    if (!png_ptr->image_data) {
        if (idat_data) free(idat_data);
        return PNG_NO_MEMORY;
    }

    uLong dest_len = raw_size;
    int zret = uncompress(png_ptr->image_data, &dest_len, idat_data, idat_size);
    free(idat_data);

    if (zret != Z_OK) {
        return PNG_INVALID_DATA;
    }

    png_ptr->image_data_size = dest_len;

    /* 解除行过滤 */
    uint8_t *filtered_data = png_ptr->image_data;
    uint32_t rowbytes = info_ptr->rowbytes;
    uint32_t channels = info_ptr->channels;

    for (uint32_t y = 0; y < png_ptr->height; y++) {
        uint8_t *row = filtered_data + y * (rowbytes + 1);
        uint8_t *prev_row = (y > 0) ? filtered_data + (y - 1) * (rowbytes + 1) + 1 : NULL;
        int filter_type = row[0];

        /* 移动数据（去掉filter byte） */
        png_memcpy(row, row + 1, rowbytes);

        /* 解除过滤 */
        int ret = png_unfilter_row(row, prev_row, rowbytes, filter_type, channels);
        if (ret != PNG_OK) return ret;
    }

    /* 创建行指针 */
    png_ptr->row_pointers = (uint8_t **)malloc(sizeof(uint8_t *) * png_ptr->height);
    if (!png_ptr->row_pointers) return PNG_NO_MEMORY;

    for (uint32_t y = 0; y < png_ptr->height; y++) {
        png_ptr->row_pointers[y] = png_ptr->image_data + y * (rowbytes + 1);
    }

    info_ptr->row_pointers = png_ptr->row_pointers;
    png_ptr->transformations = transforms;

    return PNG_OK;
}

/* 获取信息函数 */
uint32_t png_get_image_width(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->width : 0;
}

uint32_t png_get_image_height(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->height : 0;
}

int png_get_bit_depth(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->bit_depth : 0;
}

int png_get_color_type(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->color_type : 0;
}

int png_get_interlace_type(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->interlace_type : 0;
}

uint32_t png_get_rowbytes(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->rowbytes : 0;
}

int png_get_channels(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->channels : 0;
}

png_bytepp png_get_rows(png_structp png_ptr, png_infop info_ptr)
{
    (void)info_ptr;
    return png_ptr ? png_ptr->row_pointers : NULL;
}

png_colorp png_get_palette(png_structp png_ptr, png_infop info_ptr, int *num_palette)
{
    (void)png_ptr;
    if (num_palette) *num_palette = info_ptr->num_palette;
    return info_ptr ? info_ptr->palette : NULL;
}

/* 简化接口：解码PNG为RGBA格式 */
int png_decode_simple(const uint8_t *data, uint32_t data_size,
                       png_image_t *image)
{
    if (!data || !image) return PNG_ERROR;

    png_memset(image, 0, sizeof(png_image_t));

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) return PNG_NO_MEMORY;

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return PNG_NO_MEMORY;
    }

    int ret = png_read_png_from_memory(png_ptr, info_ptr, data, data_size,
                                         PNG_TRANSFORM_EXPAND | PNG_TRANSFORM_STRIP_16);
    if (ret != PNG_OK) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return ret;
    }

    image->width = info_ptr->width;
    image->height = info_ptr->height;
    image->bit_depth = info_ptr->bit_depth;
    image->color_type = info_ptr->color_type;
    image->channels = 4;  /* 统一为RGBA */
    image->rowbytes = image->width * 4;
    image->pixels_size = image->height * image->rowbytes;

    image->pixels = (uint8_t *)malloc(image->pixels_size);
    if (!image->pixels) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return PNG_NO_MEMORY;
    }

    /* 转换为RGBA格式 */
    uint8_t **rows = png_get_rows(png_ptr, info_ptr);
    int src_channels = info_ptr->channels;

    for (uint32_t y = 0; y < image->height; y++) {
        uint8_t *src_row = rows[y];
        uint8_t *dst_row = image->pixels + y * image->rowbytes;

        for (uint32_t x = 0; x < image->width; x++) {
            uint8_t r = 0, g = 0, b = 0, a = 255;

            switch (info_ptr->color_type) {
                case PNG_COLOR_TYPE_GRAY:
                    r = g = b = src_row[x * src_channels];
                    break;

                case PNG_COLOR_TYPE_GRAY_ALPHA:
                    r = g = b = src_row[x * src_channels];
                    a = src_row[x * src_channels + 1];
                    break;

                case PNG_COLOR_TYPE_RGB:
                    r = src_row[x * src_channels];
                    g = src_row[x * src_channels + 1];
                    b = src_row[x * src_channels + 2];
                    break;

                case PNG_COLOR_TYPE_RGB_ALPHA:
                    r = src_row[x * src_channels];
                    g = src_row[x * src_channels + 1];
                    b = src_row[x * src_channels + 2];
                    a = src_row[x * src_channels + 3];
                    break;

                case PNG_COLOR_TYPE_PALETTE: {
                    int index = src_row[x * src_channels];
                    if (index < png_ptr->num_palette && png_ptr->palette) {
                        r = png_ptr->palette[index].red;
                        g = png_ptr->palette[index].green;
                        b = png_ptr->palette[index].blue;
                    }
                    /* 检查透明调色板 */
                    if (png_ptr->trans_alpha && index < png_ptr->num_trans) {
                        a = png_ptr->trans_alpha[index];
                    }
                    break;
                }
            }

            dst_row[x * 4] = r;
            dst_row[x * 4 + 1] = g;
            dst_row[x * 4 + 2] = b;
            dst_row[x * 4 + 3] = a;
        }
    }

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return PNG_OK;
}

/* 释放图像 */
void png_image_free(png_image_t *image)
{
    if (image && image->pixels) {
        free(image->pixels);
        image->pixels = NULL;
    }
}
