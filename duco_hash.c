#include "duco_hash.h"

#define SHA1_ROTL(bits, word) (((word) << (bits)) | ((word) >> (32 - (bits))))
#define SHA1_ROTL5(word)      SHA1_ROTL(5, word)
#define SHA1_ROTL30(word)     SHA1_ROTL(30, word)

static const uint32_t kLengthWordByNonceLen[6] = {
    0x00000000UL,
    0x00000148UL,
    0x00000150UL,
    0x00000158UL,
    0x00000160UL,
    0x00000168UL
};

static void load_block_words(uint32_t *W, const uint32_t *baseWords,
                             const char *nonce, uint8_t nonceLen) {
    for (int i = 0; i < 10; i++) W[i] = baseWords[i];
    W[10] = W[11] = W[12] = W[13] = W[14] = 0;

    if (nonceLen <= 5) {
        uint32_t d[5] = {0};
        for (int i = 0; i < nonceLen; i++) d[i] = (uint8_t)nonce[i];
        switch (nonceLen) {
            case 1:
                W[10] = (d[0] << 24) | 0x00800000UL;
                break;
            case 2:
                W[10] = (d[0] << 24) | (d[1] << 16) | 0x00008000UL;
                break;
            case 3:
                W[10] = (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | 0x00000080UL;
                break;
            case 4:
                W[10] = (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3];
                W[11] = 0x80000000UL;
                break;
            default: // 5
                W[10] = (d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3];
                W[11] = (d[4] << 24) | 0x00800000UL;
                break;
        }
        W[15] = kLengthWordByNonceLen[nonceLen];
        return;
    }

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

int duco_hash_try_nonce(duco_hash_state_t *hasher, const char *nonce,
                        uint8_t nonceLen, const uint32_t *targetWords) {
    uint32_t W[16];
    load_block_words(W, hasher->initialWords, nonce, nonceLen);

    uint32_t a = hasher->tempState[0];
    uint32_t b = hasher->tempState[1];
    uint32_t c = hasher->tempState[2];
    uint32_t d = hasher->tempState[3];
    uint32_t e = hasher->tempState[4];

    #define SHA1_EXPAND(i) \
        W[(i) & 15] = SHA1_ROTL(1, W[((i)-3) & 15] ^ W[((i)-8) & 15] ^ \
                                   W[((i)-14) & 15] ^ W[(i) & 15])

    #define SHA1_ROUND(f_expr, K) do { \
        uint32_t _t = SHA1_ROTL5(a) + (f_expr) + e + W[i & 15] + (K); \
        e = d; \
        d = c; \
        c = SHA1_ROTL30(b); \
        b = a; \
        a = _t; \
    } while(0)

    for (uint8_t i = 10; i < 16; i++)
        SHA1_ROUND((b & (c ^ d)) ^ d, 0x5A827999UL);
    for (uint8_t i = 16; i < 20; i++) {
        SHA1_EXPAND(i);
        SHA1_ROUND((b & (c ^ d)) ^ d, 0x5A827999UL);
    }
    for (uint8_t i = 20; i < 40; i++) {
        SHA1_EXPAND(i);
        SHA1_ROUND(b ^ c ^ d, 0x6ED9EBA1UL);
    }
    for (uint8_t i = 40; i < 60; i++) {
        SHA1_EXPAND(i);
        SHA1_ROUND((b & c) | (b & d) | (c & d), 0x8F1BBCDCUL);
    }
    for (uint8_t i = 60; i < 80; i++) {
        SHA1_EXPAND(i);
        SHA1_ROUND(b ^ c ^ d, 0xCA62C1D6UL);
    }

    a += 0x67452301UL;
    b += 0xEFCDAB89UL;
    c += 0x98BADCFEUL;
    d += 0x10325476UL;
    e += 0xC3D2E1F0UL;

    return (a == targetWords[0] && b == targetWords[1] &&
            c == targetWords[2] && d == targetWords[3] && e == targetWords[4]);
}

void duco_hash_init(duco_hash_state_t *hasher, const char *prevHash) {
    uint32_t a = 0x67452301UL, b = 0xEFCDAB89UL, c = 0x98BADCFEUL;
    uint32_t d = 0x10325476UL, e = 0xC3D2E1F0UL;

    for (uint8_t i = 0; i < 10; i++) {
        hasher->initialWords[i] =
            ((uint32_t)(uint8_t)prevHash[i*4]   << 24) |
            ((uint32_t)(uint8_t)prevHash[i*4+1] << 16) |
            ((uint32_t)(uint8_t)prevHash[i*4+2] << 8)  |
            ((uint32_t)(uint8_t)prevHash[i*4+3]);
    }

    for (uint8_t i = 0; i < 10; i++) {
        uint32_t temp = SHA1_ROTL5(a) + e +
                        ((b & c) | ((~b) & d)) +
                        hasher->initialWords[i] + 0x5A827999UL;
        e = d; d = c; c = SHA1_ROTL30(b); b = a; a = temp;
    }

    hasher->tempState[0] = a; hasher->tempState[1] = b;
    hasher->tempState[2] = c; hasher->tempState[3] = d;
    hasher->tempState[4] = e;
}
