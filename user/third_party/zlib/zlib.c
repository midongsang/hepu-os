/* ============================================================================
 * 赫普OS - 简化版zlib实现
 * 使用RLE行程编码，兼容zlib接口
 * V8.25: 初始实现
 * ========================================================================== */

#include "zlib.h"

/* V8.27: 标准deflate解压 */
extern int deflate_inflate(const uint8_t *input, uint32_t input_size,
                            uint8_t *output, uint32_t *output_size);

/* 简化的内存操作（避免依赖系统string.h） */
static void *zmemcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void *zmemset(void *dst, int val, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)val;
    return dst;
}

/* 内存分配（简化：使用malloc/free） */
extern void *malloc(size_t size);
extern void free(void *ptr);

void *zcalloc(void *opaque, unsigned int items, unsigned int size)
{
    (void)opaque;
    return malloc(items * size);
}

void zcfree(void *opaque, void *ptr)
{
    (void)opaque;
    free(ptr);
}

/* 版本 */
const char *zlibVersion(void)
{
    return ZLIB_VERSION;
}

const char *zlib_version(void)
{
    return ZLIB_VERSION;
}

/* 错误消息 */
const char *zError(int err)
{
    switch (err) {
        case Z_OK:            return "OK";
        case Z_STREAM_END:    return "stream end";
        case Z_NEED_DICT:     return "need dictionary";
        case Z_ERRNO:         return "file error";
        case Z_STREAM_ERROR:  return "stream error";
        case Z_DATA_ERROR:    return "data error";
        case Z_MEM_ERROR:     return "out of memory";
        case Z_BUF_ERROR:     return "buffer error";
        case Z_VERSION_ERROR: return "version error";
        default:              return "unknown error";
    }
}

/* Adler-32校验和 */
uLong adler32(uLong adler, const Byte *buf, uInt len)
{
    const unsigned long BASE = 65521;  /* 最大素数小于65536 */
    unsigned long s1 = adler & 0xffff;
    unsigned long s2 = (adler >> 16) & 0xffff;

    for (uInt i = 0; i < len; i++) {
        s1 = (s1 + buf[i]) % BASE;
        s2 = (s2 + s1) % BASE;
    }

    return (s2 << 16) | s1;
}

/* CRC-32校验和（简化实现） */
static unsigned long crc32_table[256];
static int crc32_table_initialized = 0;

static void crc32_init_table(void)
{
    for (unsigned long i = 0; i < 256; i++) {
        unsigned long crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = 1;
}

uLong crc32(uLong crc, const Byte *buf, uInt len)
{
    if (!crc32_table_initialized) {
        crc32_init_table();
    }

    crc = crc ^ 0xFFFFFFFFUL;
    for (uInt i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* RLE压缩
 * 格式：[count(1字节)][data(1字节)]
 * count=0表示原始数据（非重复），后面跟1字节数据
 * count>0表示重复count+1次，后面跟1字节数据
 * 最大行程长度256
 */
static int rle_compress(const Byte *src, uLong src_len,
                        Byte *dst, uLong *dst_len)
{
    uLong src_pos = 0;
    uLong dst_pos = 0;

    while (src_pos < src_len) {
        Byte current = src[src_pos];
        uLong run_len = 1;

        /* 计算行程长度 */
        while (src_pos + run_len < src_len &&
               src[src_pos + run_len] == current &&
               run_len < 256) {
            run_len++;
        }

        if (run_len >= 3) {
            /* 重复字节：输出[run_len-1][byte] */
            if (dst_pos + 2 > *dst_len) return Z_BUF_ERROR;
            dst[dst_pos++] = (Byte)(run_len - 1);
            dst[dst_pos++] = current;
        } else {
            /* 非重复字节：输出[0][byte]，逐个输出 */
            for (uLong i = 0; i < run_len; i++) {
                if (dst_pos + 2 > *dst_len) return Z_BUF_ERROR;
                dst[dst_pos++] = 0;
                dst[dst_pos++] = src[src_pos + i];
            }
        }

        src_pos += run_len;
    }

    *dst_len = dst_pos;
    return Z_OK;
}

/* RLE解压 */
static int rle_decompress(const Byte *src, uLong src_len,
                          Byte *dst, uLong *dst_len)
{
    uLong src_pos = 0;
    uLong dst_pos = 0;

    while (src_pos < src_len) {
        if (src_pos + 1 >= src_len) return Z_DATA_ERROR;

        Byte count = src[src_pos++];
        Byte data = src[src_pos++];

        if (count == 0) {
            /* 非重复字节 */
            if (dst_pos + 1 > *dst_len) return Z_BUF_ERROR;
            dst[dst_pos++] = data;
        } else {
            /* 重复字节（count+1次） */
            uLong repeat = count + 1;
            if (dst_pos + repeat > *dst_len) return Z_BUF_ERROR;
            for (uLong i = 0; i < repeat; i++) {
                dst[dst_pos++] = data;
            }
        }
    }

    *dst_len = dst_pos;
    return Z_OK;
}

/* 压缩边界（最大压缩后大小） */
uLong compressBound(uLong sourceLen)
{
    /* RLE最坏情况：每个字节变成2字节，加上头部 */
    return sourceLen * 2 + 16;
}

/* 基本压缩 */
int compress(Byte *dest, uLong *destLen, const Byte *source, uLong sourceLen)
{
    return compress2(dest, destLen, source, sourceLen, Z_DEFAULT_COMPRESSION);
}

int compress2(Byte *dest, uLong *destLen, const Byte *source, uLong sourceLen, int level)
{
    if (!dest || !destLen || !source) return Z_STREAM_ERROR;
    if (level < 0 || level > 9) return Z_STREAM_ERROR;

    /* zlib格式头部：CMF(1字节) + FLG(1字节) */
    /* CMF: CM=8 (deflate), CINFO=7 (32K窗口) */
    /* FLG: 检查位 */
    if (*destLen < 6) return Z_BUF_ERROR;

    dest[0] = 0x78;  /* CMF: CM=8, CINFO=7 */
    dest[1] = 0x01;  /* FLG: 无预设字典, 检查位 */

    /* RLE压缩数据 */
    uLong compressed_len = *destLen - 6;  /* 减去头部和校验和 */
    Byte *compressed_data = dest + 2;
    int ret = rle_compress(source, sourceLen, compressed_data, &compressed_len);
    if (ret != Z_OK) return ret;

    /* Adler-32校验和（大端序） */
    uLong checksum = adler32(1L, source, sourceLen);
    dest[2 + compressed_len] = (Byte)((checksum >> 24) & 0xFF);
    dest[2 + compressed_len + 1] = (Byte)((checksum >> 16) & 0xFF);
    dest[2 + compressed_len + 2] = (Byte)((checksum >> 8) & 0xFF);
    dest[2 + compressed_len + 3] = (Byte)(checksum & 0xFF);

    *destLen = compressed_len + 6;  /* 头部2 + 数据 + 校验和4 */

    return Z_OK;
}

/* 基本解压 */
int uncompress(Byte *dest, uLong *destLen, const Byte *source, uLong sourceLen)
{
    if (!dest || !destLen || !source) return Z_STREAM_ERROR;
    if (sourceLen < 6) return Z_DATA_ERROR;

    /* V8.27: 使用标准deflate解压（支持真实zlib格式数据） */
    uint32_t output_len = (uint32_t)*destLen;
    int ret = deflate_inflate(source, (uint32_t)sourceLen, dest, &output_len);
    if (ret != 0) return Z_DATA_ERROR;

    *destLen = output_len;
    return Z_OK;
}

/* 流操作（简化实现） */

int deflateInit(z_stream *strm, int level)
{
    return deflateInit2(strm, level, Z_DEFLATED, MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
}

int deflateInit2(z_stream *strm, int level, int method, int windowBits,
                  int memLevel, int strategy)
{
    if (!strm) return Z_STREAM_ERROR;
    if (method != Z_DEFLATED) return Z_STREAM_ERROR;
    if (level < 0 || level > 9) level = Z_DEFAULT_COMPRESSION;

    strm->msg = NULL;
    strm->state = NULL;
    strm->total_in = 0;
    strm->total_out = 0;
    strm->adler = 1L;

    /* 分配内部状态 */
    z_internal_state *state = (z_internal_state *)malloc(sizeof(z_internal_state));
    if (!state) return Z_MEM_ERROR;

    state->mode = 1;  /* 压缩 */
    state->level = level;
    state->last_byte = 0;
    state->run_length = 0;
    state->state = 0;
    state->adler = 1L;

    strm->state = state;

    return Z_OK;
}

int deflate(z_stream *strm, int flush)
{
    if (!strm || !strm->state) return Z_STREAM_ERROR;

    z_internal_state *state = (z_internal_state *)strm->state;

    /* 简化：直接压缩所有输入 */
    if (strm->avail_in > 0 && strm->avail_out > 2) {
        uLong src_len = strm->avail_in;
        uLong dst_len = strm->avail_out;
        int ret = rle_compress(strm->next_in, src_len, strm->next_out, &dst_len);
        if (ret != Z_OK) return ret;

        strm->next_in += src_len;
        strm->avail_in = 0;
        strm->total_in += src_len;
        strm->next_out += dst_len;
        strm->avail_out -= dst_len;
        strm->total_out += dst_len;
        state->adler = adler32(state->adler, strm->next_in - src_len, src_len);
    }

    if (flush == Z_FINISH) {
        return Z_STREAM_END;
    }

    return Z_OK;
}

int deflateEnd(z_stream *strm)
{
    if (!strm) return Z_STREAM_ERROR;
    if (strm->state) {
        free(strm->state);
        strm->state = NULL;
    }
    return Z_OK;
}

int deflateReset(z_stream *strm)
{
    if (!strm || !strm->state) return Z_STREAM_ERROR;
    z_internal_state *state = (z_internal_state *)strm->state;
    state->last_byte = 0;
    state->run_length = 0;
    state->state = 0;
    state->adler = 1L;
    strm->total_in = 0;
    strm->total_out = 0;
    strm->adler = 1L;
    return Z_OK;
}

int deflateSetDictionary(z_stream *strm, const Byte *dictionary, uInt dictLength)
{
    (void)strm;
    (void)dictionary;
    (void)dictLength;
    return Z_OK;  /* 简化：不支持预设字典 */
}

int inflateInit(z_stream *strm)
{
    return inflateInit2(strm, MAX_WBITS);
}

int inflateInit2(z_stream *strm, int windowBits)
{
    if (!strm) return Z_STREAM_ERROR;
    (void)windowBits;

    strm->msg = NULL;
    strm->state = NULL;
    strm->total_in = 0;
    strm->total_out = 0;
    strm->adler = 1L;

    /* 分配内部状态 */
    z_internal_state *state = (z_internal_state *)malloc(sizeof(z_internal_state));
    if (!state) return Z_MEM_ERROR;

    state->mode = 0;  /* 解压 */
    state->level = 0;
    state->last_byte = 0;
    state->run_length = 0;
    state->state = 0;
    state->adler = 1L;

    strm->state = state;

    return Z_OK;
}

int inflate(z_stream *strm, int flush)
{
    if (!strm || !strm->state) return Z_STREAM_ERROR;
    (void)flush;

    /* 简化：直接解压所有输入 */
    if (strm->avail_in > 0 && strm->avail_out > 0) {
        uLong src_len = strm->avail_in;
        uLong dst_len = strm->avail_out;
        int ret = rle_decompress(strm->next_in, src_len, strm->next_out, &dst_len);
        if (ret != Z_OK) return ret;

        strm->next_in += src_len;
        strm->avail_in = 0;
        strm->total_in += src_len;
        strm->next_out += dst_len;
        strm->avail_out -= dst_len;
        strm->total_out += dst_len;
    }

    return Z_OK;
}

int inflateEnd(z_stream *strm)
{
    if (!strm) return Z_STREAM_ERROR;
    if (strm->state) {
        free(strm->state);
        strm->state = NULL;
    }
    return Z_OK;
}

int inflateReset(z_stream *strm)
{
    if (!strm || !strm->state) return Z_STREAM_ERROR;
    z_internal_state *state = (z_internal_state *)strm->state;
    state->last_byte = 0;
    state->run_length = 0;
    state->state = 0;
    state->adler = 1L;
    strm->total_in = 0;
    strm->total_out = 0;
    strm->adler = 1L;
    return Z_OK;
}

int inflateSetDictionary(z_stream *strm, const Byte *dictionary, uInt dictLength)
{
    (void)strm;
    (void)dictionary;
    (void)dictLength;
    return Z_OK;  /* 简化：不支持预设字典 */
}

int inflateSync(z_stream *strm)
{
    if (!strm) return Z_STREAM_ERROR;
    return Z_OK;  /* 简化：直接返回OK */
}
