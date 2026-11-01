/* ============================================================================
 * 赫普OS - SHA512哈希算法（从sha.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "sha.h"
#include "string.h"

static inline uint64_t sha_rotl64(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}

static inline uint64_t sha_get_be64(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

static inline void sha_put_be64(uint8_t *p, uint64_t v) {
    p[0] = (v >> 56) & 0xFF; p[1] = (v >> 48) & 0xFF;
    p[2] = (v >> 40) & 0xFF; p[3] = (v >> 32) & 0xFF;
    p[4] = (v >> 24) & 0xFF; p[5] = (v >> 16) & 0xFF;
    p[6] = (v >> 8) & 0xFF; p[7] = v & 0xFF;
}

static const uint64_t sha512_k[80] = {
    0x428A2F98D728AE22ULL, 0x7137449123EF65CDULL,
    0xB5C0FBCFEC4D3B2FULL, 0xE9B5DBA58189DBBCULL,
    0x3956C25BF348B538ULL, 0x59F111F1B605D019ULL,
    0x923F82A4AF194F9BULL, 0xAB1C5ED5DA6D8118ULL,
    0xD807AA98A3030242ULL, 0x12835B0145706FBEULL,
    0x243185BE4EE4B28CULL, 0x550C7DC3D5FFB4E2ULL,
    0x72BE5D74F27B896FULL, 0x80DEB1FE3B1696B1ULL,
    0x9BDC06A725C71235ULL, 0xC19BF174CF692694ULL,
    0xE49B69C19EF14AD2ULL, 0xEFBE4786384F25E3ULL,
    0x0FC19DC68B8CD5B5ULL, 0x240CA1CC77AC9C65ULL,
    0x2DE92C6F592B0275ULL, 0x4A7484AA6EA6E483ULL,
    0x5CB0A9DCBD41FBD4ULL, 0x76F988DA831153B5ULL,
    0x983E5152EE66DFABULL, 0xA831C66D2DB43210ULL,
    0xB00327C898FB213FULL, 0xBF597FC7BEEF0EE4ULL,
    0xC6E00BF33DA88FC2ULL, 0xD5A79147930AA725ULL,
    0x06CA6351E003826FULL, 0x142929670A0E6E70ULL,
    0x27B70A8546D22FFCULL, 0x2E1B21385C26C926ULL,
    0x4D2C6DFC5AC42AEDULL, 0x53380D139D95B3DFULL,
    0x650A73548BAF63DEULL, 0x766A0ABB3C77B2A8ULL,
    0x81C2C92E47EDAEE6ULL, 0x92722C851482353BULL,
    0xA2BFE8A14CF10364ULL, 0xA81A664BBC423001ULL,
    0xC24B8B70D0F89791ULL, 0xC76C51A30654BE30ULL,
    0xD192E819D6EF5218ULL, 0xD69906245565A910ULL,
    0xF40E35855771202AULL, 0x106AA07032BBD1B8ULL,
    0x19A4C116B8D2D0C8ULL, 0x1E376C085141AB53ULL,
    0x2748774CDF8EEB99ULL, 0x34B0BCB5E19B48A8ULL,
    0x391C0CB3C5C95A63ULL, 0x4ED8AA4AE3418ACBULL,
    0x5B9CCA4F7763E373ULL, 0x682E6FF3D6B2B8A3ULL,
    0x748F82EE5DEFB2FCULL, 0x78A5636F43172F60ULL,
    0x84C87814A1F0AB72ULL, 0x8CC702081A6439ECULL,
    0x90BEFFFA23631E28ULL, 0xA4506CEBDE82BDE9ULL,
    0xBEF9A3F7B2C67915ULL, 0xC67178F2E372532BULL,
    0xCA273ECEEA26619CULL, 0xD186B8C721C0C207ULL,
    0xEADA7DD6CDE0EB1EULL, 0xF57D4F7FEE6ED178ULL,
    0x06F067AA72176FBAULL, 0x0A637DC5A2C898A6ULL,
    0x113F9804BEF90DAEULL, 0x1B710B35131C471BULL,
    0x28DB77F523047D84ULL, 0x32CAAB7B40C72493ULL,
    0x3C9EBE0A15C9BEBCULL, 0x431D67C49C100D4CULL,
    0x4CC5D4BECB3E42B6ULL, 0x597F299CFC657E2AULL,
    0x5FCB6FAB3AD6FAECULL, 0x6C44198C4A475817ULL
};

static inline uint64_t sha512_ch(uint64_t x, uint64_t y, uint64_t z) { return (x & y) ^ (~x & z); }
static inline uint64_t sha512_maj(uint64_t x, uint64_t y, uint64_t z) { return (x & y) ^ (x & z) ^ (y & z); }
static inline uint64_t sha512_sigma0(uint64_t x) { return sha_rotl64(x,28)^sha_rotl64(x,34)^sha_rotl64(x,39); }
static inline uint64_t sha512_sigma1(uint64_t x) { return sha_rotl64(x,14)^sha_rotl64(x,18)^sha_rotl64(x,41); }
static inline uint64_t sha512_gamma0(uint64_t x) { return sha_rotl64(x,1)^sha_rotl64(x,8)^(x>>7); }
static inline uint64_t sha512_gamma1(uint64_t x) { return sha_rotl64(x,19)^sha_rotl64(x,61)^(x>>6); }

static void sha512_process_block(sha512_ctx_t *ctx, const uint8_t *block) {
    uint64_t w[80], a,b,c,d,e,f,g,h,t1,t2;
    for (int i=0;i<16;i++) w[i]=sha_get_be64(block+i*8);
    for (int i=16;i<80;i++) w[i]=sha512_gamma1(w[i-2])+w[i-7]+sha512_gamma0(w[i-15])+w[i-16];
    a=ctx->state[0];b=ctx->state[1];c=ctx->state[2];d=ctx->state[3];
    e=ctx->state[4];f=ctx->state[5];g=ctx->state[6];h=ctx->state[7];
    for (int i=0;i<80;i++) {
        t1=h+sha512_sigma1(e)+sha512_ch(e,f,g)+sha512_k[i]+w[i];
        t2=sha512_sigma0(a)+sha512_maj(a,b,c);
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;
    ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}

void sha512_init(sha512_ctx_t *ctx) {
    ctx->state[0]=0x6A09E667F3BCC908ULL; ctx->state[1]=0xBB67AE8584CAA73BULL;
    ctx->state[2]=0x3C6EF372FE94F82BULL; ctx->state[3]=0xA54FF53A5F1D36F1ULL;
    ctx->state[4]=0x510E527FADE682D1ULL; ctx->state[5]=0x9B05688C2B3E6C1FULL;
    ctx->state[6]=0x1F83D9ABFB41BD6BULL; ctx->state[7]=0x5BE0CD19137E2179ULL;
    ctx->count=0;
}

void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    uint32_t buffer_idx=(uint32_t)(ctx->count%SHA512_BLOCK_SIZE);
    ctx->count+=len;
    if (buffer_idx+len>=SHA512_BLOCK_SIZE) {
        uint32_t copy=SHA512_BLOCK_SIZE-buffer_idx;
        memcpy(ctx->buffer+buffer_idx,data,copy);
        sha512_process_block(ctx,ctx->buffer);
        data+=copy;len-=copy;
        while (len>=SHA512_BLOCK_SIZE) { sha512_process_block(ctx,data); data+=SHA512_BLOCK_SIZE; len-=SHA512_BLOCK_SIZE; }
        buffer_idx=0;
    }
    if (len>0) memcpy(ctx->buffer+buffer_idx,data,len);
}

void sha512_final(sha512_ctx_t *ctx, uint8_t *digest) {
    uint32_t buffer_idx=(uint32_t)(ctx->count%SHA512_BLOCK_SIZE);
    uint64_t bit_count=ctx->count*8;
    ctx->buffer[buffer_idx++]=0x80;
    if (buffer_idx>112) {
        while (buffer_idx<SHA512_BLOCK_SIZE) ctx->buffer[buffer_idx++]=0;
        sha512_process_block(ctx,ctx->buffer); buffer_idx=0;
    }
    while (buffer_idx<112) ctx->buffer[buffer_idx++]=0;
    sha_put_be64(ctx->buffer+112,0);
    sha_put_be64(ctx->buffer+120,bit_count);
    sha512_process_block(ctx,ctx->buffer);
    for (int i=0;i<8;i++) sha_put_be64(digest+i*8,ctx->state[i]);
}

int sha512(const uint8_t *data, uint32_t len, uint8_t *digest) {
    sha512_ctx_t ctx;
    sha512_init(&ctx); sha512_update(&ctx,data,len); sha512_final(&ctx,digest);
    return 0;
}
