/* ============================================================================
 * 赫普OS - zlib兼容头文件
 * 简化版zlib接口，使用RLE行程编码
 * V8.25: 初始实现
 * ========================================================================== */

#ifndef _ZLIB_H
#define _ZLIB_H

#include <stdint.h>
#include <stddef.h>

/* zlib版本 */
#define ZLIB_VERSION "1.2.11-hepu"
#define ZLIB_VERNUM 0x12B0

/* 错误码 */
#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)

/* 压缩级别 */
#define Z_NO_COMPRESSION      0
#define Z_BEST_SPEED          1
#define Z_BEST_COMPRESSION    9
#define Z_DEFAULT_COMPRESSION (-1)

/* 压缩策略 */
#define Z_FILTERED            1
#define Z_HUFFMAN_ONLY        2
#define Z_RLE                 3
#define Z_FIXED               4
#define Z_DEFAULT_STRATEGY    0

/* 数据类型 */
#define Z_BINARY   0
#define Z_TEXT     1
#define Z_ASCII    Z_TEXT
#define Z_UNKNOWN  2

/* 压缩方法 */
#define Z_DEFLATED 8

/* 窗口位 */
#define MAX_WBITS   15

/* 内存级别 */
#define MAX_MEM_LEVEL 9

/* 默认缓冲区大小 */
#define Z_DEFAULT_BUFSIZE 16384

/* 字节类型 */
typedef unsigned char Byte;
typedef unsigned int uInt;
typedef unsigned long uLong;

/* 偏移类型 */
typedef long z_off_t;

/* 流结构 */
typedef struct z_stream_s {
    const Byte *next_in;     /* 下一个输入字节 */
    uInt     avail_in;       /* 可用输入字节数 */
    uLong    total_in;       /* 总输入字节数 */

    Byte    *next_out;       /* 下一个输出字节位置 */
    uInt     avail_out;      /* 可用输出空间 */
    uLong    total_out;      /* 总输出字节数 */

    char    *msg;            /* 错误消息 */
    void    *state;          /* 内部状态 */

    void    *zalloc;         /* 内存分配函数 */
    void    *zfree;          /* 内存释放函数 */
    void    *opaque;         /* 传递给alloc/free的私有数据 */

    int      data_type;      /* 数据类型（Z_BINARY/Z_TEXT/Z_UNKNOWN） */
    uLong    adler;          /* 未压缩数据的adler32校验和 */
    uLong    reserved;       /* 保留字段 */
} z_stream;

typedef z_stream z_streamp;

/* 内部状态结构（RLE编码器） */
typedef struct {
    int     mode;           /* 模式：压缩/解压 */
    int     level;          /* 压缩级别 */
    Byte    last_byte;      /* 上一个字节 */
    int     run_length;     /* 当前行程长度 */
    int     state;          /* 内部状态 */
    uLong   adler;          /* adler32校验和 */
} z_internal_state;

/* 函数声明 */

/* 版本相关 */
const char *zlibVersion(void);
const char *zlib_version(void);

/* 基本压缩/解压函数 */
int compress(Byte *dest, uLong *destLen, const Byte *source, uLong sourceLen);
int compress2(Byte *dest, uLong *destLen, const Byte *source, uLong sourceLen, int level);
int uncompress(Byte *dest, uLong *destLen, const Byte *source, uLong sourceLen);

/* 压缩边界 */
uLong compressBound(uLong sourceLen);

/* 流操作 */
int deflateInit(z_stream *strm, int level);
int deflateInit2(z_stream *strm, int level, int method, int windowBits,
                  int memLevel, int strategy);
int deflate(z_stream *strm, int flush);
int deflateEnd(z_stream *strm);
int deflateReset(z_stream *strm);
int deflateSetDictionary(z_stream *strm, const Byte *dictionary, uInt dictLength);

int inflateInit(z_stream *strm);
int inflateInit2(z_stream *strm, int windowBits);
int inflate(z_stream *strm, int flush);
int inflateEnd(z_stream *strm);
int inflateReset(z_stream *strm);
int inflateSetDictionary(z_stream *strm, const Byte *dictionary, uInt dictLength);
int inflateSync(z_stream *strm);

/* 校验和 */
uLong adler32(uLong adler, const Byte *buf, uInt len);
uLong crc32(uLong crc, const Byte *buf, uInt len);

/* 内存分配 */
void *zcalloc(void *opaque, unsigned int items, unsigned int size);
void zcfree(void *opaque, void *ptr);

/* 错误消息 */
const char *zError(int err);

/* flush值 */
#define Z_NO_FLUSH      0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH    2
#define Z_FULL_FLUSH    3
#define Z_FINISH        4
#define Z_BLOCK         5
#define Z_TREES         6

#endif /* _ZLIB_H */
