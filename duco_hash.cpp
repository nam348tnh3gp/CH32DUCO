// duco_hash.cpp – Full tối ưu cho Uno (unroll tất cả, phiên bản nonce 5 byte riêng)
#include "duco_hash.h"

#pragma GCC optimize("-Ofast")

// ================== ROTATE MACROS (giữ nguyên) ==================
#define sha1_rotl(bits, word) \
    (((word) << (bits)) | ((word) >> (32 - (bits))))

#if defined(__AVR__)
extern "C" uint32_t sha1_rotl5(uint32_t value);
extern "C" uint32_t sha1_rotl30(uint32_t value);
#define SHA1_ROTL5(word) sha1_rotl5(word)
#define SHA1_ROTL30(word) sha1_rotl30(word)
#else
#define SHA1_ROTL5(word) sha1_rotl(5, word)
#define SHA1_ROTL30(word) sha1_rotl(30, word)
#endif

static uint32_t const kLengthWordByNonceLen[6] = {
    0x00000000UL,
    0x00000148UL,
    0x00000150UL,
    0x00000158UL,
    0x00000160UL,
    0x00000168UL
};

// ================== HÀM LOAD BLOCK WORDS (GIỮ NGUYÊN) ==================
static inline __attribute__((always_inline)) void duco_hash_load_block_words(
    uint32_t *W,
    uint32_t const *baseWords,
    char const *nonce,
    uint8_t nonceLen)
{
    W[0] = baseWords[0];
    W[1] = baseWords[1];
    W[2] = baseWords[2];
    W[3] = baseWords[3];
    W[4] = baseWords[4];
    W[5] = baseWords[5];
    W[6] = baseWords[6];
    W[7] = baseWords[7];
    W[8] = baseWords[8];
    W[9] = baseWords[9];

    uint32_t d0 = (uint8_t)nonce[0];
    uint32_t d1 = (uint8_t)nonce[1];
    uint32_t d2 = (uint8_t)nonce[2];
    uint32_t d3 = (uint8_t)nonce[3];
    uint32_t d4 = (uint8_t)nonce[4];

    if (nonceLen <= 5) {
        switch (nonceLen) {
            case 1:
                W[10] = (d0 << 24) | 0x00800000UL;
                W[11] = 0;
                W[12] = 0;
                break;
            case 2:
                W[10] = (d0 << 24) | (d1 << 16) | 0x00008000UL;
                W[11] = 0;
                W[12] = 0;
                break;
            case 3:
                W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | 0x00000080UL;
                W[11] = 0;
                W[12] = 0;
                break;
            case 4:
                W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
                W[11] = 0x80000000UL;
                W[12] = 0;
                break;
            default: // 5
                W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
                W[11] = (d4 << 24) | 0x00800000UL;
                W[12] = 0;
                break;
        }
        W[13] = 0;
        W[14] = 0;
        W[15] = kLengthWordByNonceLen[nonceLen];
        return;
    }

    W[10] = 0; W[11] = 0; W[12] = 0; W[13] = 0; W[14] = 0;
    for (uint8_t i = 0; i < nonceLen; i++) {
        uint8_t wordIndex = 10 + (i >> 2);
        uint8_t shift = 24 - ((i & 3) << 3);
        W[wordIndex] |= (uint32_t)(uint8_t)nonce[i] << shift;
    }
    {
        uint8_t wordIndex = 10 + (nonceLen >> 2);
        uint8_t shift = 24 - ((nonceLen & 3) << 3);
        W[wordIndex] |= 0x80UL << shift;
    }
    W[15] = (uint32_t)(40 + nonceLen) << 3;
}

// ================== INIT UNROLL HOÀN TOÀN 10 VÒNG ==================
void duco_hash_init(duco_hash_state_t *hasher, char const *prevHash)
{
    uint32_t a = 0x67452301UL;
    uint32_t b = 0xEFCDAB89UL;
    uint32_t c = 0x98BADCFEUL;
    uint32_t d = 0x10325476UL;
    uint32_t e = 0xC3D2E1F0UL;
    uint32_t t;

    for (uint8_t i = 0, i4 = 0; i < 10; i++, i4 += 4) {
        hasher->initialWords[i] =
            ((uint32_t)(uint8_t)prevHash[i4    ] << 24) |
            ((uint32_t)(uint8_t)prevHash[i4 + 1] << 16) |
            ((uint32_t)(uint8_t)prevHash[i4 + 2] <<  8) |
            ((uint32_t)(uint8_t)prevHash[i4 + 3]);
    }

    // 10 vòng đầu, unroll cứng
    #define SHA1_INIT_ROUND(idx) do { \
        t = SHA1_ROTL5(a) + e + ((b & (c ^ d)) ^ d) + hasher->initialWords[idx] + 0x5A827999UL; \
        e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t; \
    } while(0)

    SHA1_INIT_ROUND(0);
    SHA1_INIT_ROUND(1);
    SHA1_INIT_ROUND(2);
    SHA1_INIT_ROUND(3);
    SHA1_INIT_ROUND(4);
    SHA1_INIT_ROUND(5);
    SHA1_INIT_ROUND(6);
    SHA1_INIT_ROUND(7);
    SHA1_INIT_ROUND(8);
    SHA1_INIT_ROUND(9);

    #undef SHA1_INIT_ROUND

    hasher->tempState[0] = a;
    hasher->tempState[1] = b;
    hasher->tempState[2] = c;
    hasher->tempState[3] = d;
    hasher->tempState[4] = e;
}

// ================== TRY_NONCE DÀNH RIÊNG NONCE 5 BYTE ==================
/*
   Phiên bản này bỏ qua hoàn toàn switch và if khi load block words.
   Chỉ dùng khi chắc chắn nonceLen = 5.
*/
__attribute__((noinline)) bool duco_hash_try_nonce_len5(
    duco_hash_state_t *hasher,
    char const *nonce,
    uint32_t const *targetWords)
{
    static uint32_t W[16];

    // Load block words cho nonce 5 byte (không rẽ nhánh)
    W[0] = hasher->initialWords[0];
    W[1] = hasher->initialWords[1];
    W[2] = hasher->initialWords[2];
    W[3] = hasher->initialWords[3];
    W[4] = hasher->initialWords[4];
    W[5] = hasher->initialWords[5];
    W[6] = hasher->initialWords[6];
    W[7] = hasher->initialWords[7];
    W[8] = hasher->initialWords[8];
    W[9] = hasher->initialWords[9];

    uint32_t d0 = (uint8_t)nonce[0];
    uint32_t d1 = (uint8_t)nonce[1];
    uint32_t d2 = (uint8_t)nonce[2];
    uint32_t d3 = (uint8_t)nonce[3];
    uint32_t d4 = (uint8_t)nonce[4];
    W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
    W[11] = (d4 << 24) | 0x00800000UL;
    W[12] = 0;
    W[13] = 0;
    W[14] = 0;
    W[15] = 0x00000168UL;  // (40 + 5) * 8

    uint32_t a = hasher->tempState[0];
    uint32_t b = hasher->tempState[1];
    uint32_t c = hasher->tempState[2];
    uint32_t d = hasher->tempState[3];
    uint32_t e = hasher->tempState[4];
    uint32_t t;

    // ======= 70 VÒNG UNROLL (10..79) =======
    // Vòng 10..15 (f=Ch, K=0x5A827999)
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[10] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[11] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[12] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[13] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[14] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[15] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // Vòng 16..19 (f=Ch, K=0x5A827999, expand)
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[0] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[1] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[2] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[3] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // Vòng 20..39 (Parity, K=0x6ED9EBA1)
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[8] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[9] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[10] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[11] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[0] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[1] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[2] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[3] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // Vòng 40..59 (Majority, K=0x8F1BBCDC)
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[8] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[9] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[10] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[11] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[12] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[13] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[14] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[15] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[0] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[1] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[2] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[3] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[4] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[5] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[6] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[7] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[8] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[9] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[10] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[11] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // Vòng 60..79 (Parity, K=0xCA62C1D6)
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[0] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[1] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[2] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[3] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[8] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[9] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[10] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[11] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // Kết thúc
    a += 0x67452301UL;
    b += 0xEFCDAB89UL;
    c += 0x98BADCFEUL;
    d += 0x10325476UL;
    e += 0xC3D2E1F0UL;

    return a == targetWords[0]
        && b == targetWords[1]
        && c == targetWords[2]
        && d == targetWords[3]
        && e == targetWords[4];
}

// ================== TRY_NONCE TỔNG QUÁT (GIỮ NGUYÊN UNROLL 70 VÒNG) ==================
__attribute__((noinline)) bool duco_hash_try_nonce(duco_hash_state_t *hasher,
                                                   char const *nonce,
                                                   uint8_t nonceLen,
                                                   uint32_t const *targetWords)
{
    static uint32_t W[16];
    duco_hash_load_block_words(W, hasher->initialWords, nonce, nonceLen);

    uint32_t a = hasher->tempState[0];
    uint32_t b = hasher->tempState[1];
    uint32_t c = hasher->tempState[2];
    uint32_t d = hasher->tempState[3];
    uint32_t e = hasher->tempState[4];
    uint32_t t;

    // ======= Vòng 10 -> 79 (copy nguyên từ trên) =======
    // Vòng 10..15
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[10] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[11] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[12] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[13] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[14] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[15] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // Vòng 16..19
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[0] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[1] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[2] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[3] + 0x5A827999UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // Vòng 20..39
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[8] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[9] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[10] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[11] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[0] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[1] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[2] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[3] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0x6ED9EBA1UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // Vòng 40..59
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[8] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[9] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[10] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[11] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[12] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[13] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[14] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[15] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[0] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[1] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[2] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[3] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[4] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[5] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[6] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[7] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[8] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[9] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[10] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[11] + 0x8F1BBCDCUL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // Vòng 60..79
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[0] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[1] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[2] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[3] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[8] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[9] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[10] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[11] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0xCA62C1D6UL;
    e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // Kết thúc
    a += 0x67452301UL;
    b += 0xEFCDAB89UL;
    c += 0x98BADCFEUL;
    d += 0x10325476UL;
    e += 0xC3D2E1F0UL;

    return a == targetWords[0]
        && b == targetWords[1]
        && c == targetWords[2]
        && d == targetWords[3]
        && e == targetWords[4];
}
