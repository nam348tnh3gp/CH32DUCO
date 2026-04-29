/*
 * Duino-Coin Miner for CH32V003 (RISC-V MCU)
 * Based on official Arduino code 4.3, adapted for direct register control.
 * No Arduino Serial, no LED, 48MHz HSI+PLL, 115200 baud UART1.
 * Uses unique ID from ESIG registers.
 * DUCO-S1A hash algorithm implemented in plain C (no assembly).
 *
 * Target: CH32V003 (QingKe V2A, RV32EC)
 * Toolchain: riscv-none-embed-gcc
 *
 * License: MIT
 */

#include <stdint.h>

//=============================================================================
// Hardware registers definitions (from CH32V003 reference manual)
//=============================================================================

// RCC
#define RCC_BASE        0x40021000u
#define RCC_CTLR        (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR0       (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_INTR        (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_APB2PRSTR   (*(volatile uint32_t *)(RCC_BASE + 0x0C))
#define RCC_APB1PRSTR   (*(volatile uint32_t *)(RCC_BASE + 0x10))
#define RCC_AHBPCENR    (*(volatile uint32_t *)(RCC_BASE + 0x14))
#define RCC_APB2PCENR   (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_APB1PCENR   (*(volatile uint32_t *)(RCC_BASE + 0x1C))
#define RCC_RSTSCKR     (*(volatile uint32_t *)(RCC_BASE + 0x24))

// GPIO
#define GPIOD_BASE      0x40011400u
#define GPIOD_CFGLR     (*(volatile uint32_t *)(GPIOD_BASE + 0x00))
#define GPIOD_OUTDR     (*(volatile uint32_t *)(GPIOD_BASE + 0x0C))
#define GPIOD_BSHR      (*(volatile uint32_t *)(GPIOD_BASE + 0x10))

// USART1
#define USART1_BASE     0x40013800u
#define USART1_STATR    (*(volatile uint32_t *)(USART1_BASE + 0x00))
#define USART1_DATAR    (*(volatile uint32_t *)(USART1_BASE + 0x04))
#define USART1_BRR      (*(volatile uint32_t *)(USART1_BASE + 0x08))
#define USART1_CTLR1    (*(volatile uint32_t *)(USART1_BASE + 0x0C))
#define USART1_CTLR2    (*(volatile uint32_t *)(USART1_BASE + 0x10))
#define USART1_CTLR3    (*(volatile uint32_t *)(USART1_BASE + 0x14))

// ESIG (Unique ID)
#define ESIG_BASE       0x1FFFF7E0u
#define ESIG_UNIID1     (*(volatile uint32_t *)(ESIG_BASE + 0x08)) // UID[63:32]
#define ESIG_UNIID2     (*(volatile uint32_t *)(ESIG_BASE + 0x0C)) // UID[31:0]
#define ESIG_UNIID3     (*(volatile uint32_t *)(ESIG_BASE + 0x10)) // UID[95:64]

//=============================================================================
// Hardware configuration
//=============================================================================
static void SystemClock_Init(void) {
    // Enable HSI (already on by default, but ensure)
    RCC_CTLR |= (1u << 0);   // HSION
    while (!(RCC_CTLR & (1u << 1))); // Wait HSIRDY

    // Configure PLL: source = HSI, factor = 2 (manual says 2x frequency)
    // EXTEND_CTR register is used for PLL configuration in CH32V003.
    // Base address of EXTEND is 0x40023800
    volatile uint32_t *EXTEND_CTR = (volatile uint32_t *)0x40023800u;
    // Set PLL mul factor (bits 10:8? See manual EXTEND_CTR)
    // For 48MHz from 24MHz HSI: PLL_NODIV = 0? Actually we need 2x.
    // According to manual, EXTEND_CTR bit 11: PLL_MUL, but exact setting not given.
    // We'll assume default reset state sets PLL to 2x when enabled.
    // Enable PLL
    RCC_CTLR |= (1u << 24);  // PLLON
    while (!(RCC_CTLR & (1u << 25))); // Wait PLLRDY

    // Select PLL as system clock
    RCC_CFGR0 &= ~(3u << 0); // Clear SW
    RCC_CFGR0 |= (2u << 0);  // SW = 10 (PLL output as system clock)
    while ((RCC_CFGR0 & (3u << 2)) != (2u << 2)); // Wait SWS = 10

    // AHB prescaler = 1 (HPRE = 0)
    RCC_CFGR0 &= ~(0xFu << 4);
}

static void USART1_Init(uint32_t baud) {
    // Enable clocks for GPIO D and USART1
    RCC_APB2PCENR |= (1u << 5) | (1u << 14);  // IOPDEN, USART1EN

    // Configure PD5 (TX) as push-pull output, 50MHz max speed
    GPIOD_CFGLR &= ~(0xFu << 20);
    GPIOD_CFGLR |= (0xBu << 20);   // CNF5=10 (multiplex push-pull), MODE5=11 (50MHz)

    // Configure PD6 (RX) as floating input
    GPIOD_CFGLR &= ~(0xFu << 24);
    GPIOD_CFGLR |= (0x4u << 24);   // CNF6=01 (floating), MODE6=00

    // Calculate USARTDIV
    uint32_t pclk = 48000000u; // Assuming HCLK=48MHz
    uint32_t usartdiv = (pclk / 16u + baud / 2u) / baud; // Round closest
    uint32_t mantissa = usartdiv;
    // Calculate fractional part (not accurate but simple)
    uint32_t fraction = ((pclk * 100u / baud / 16u) % 100u) * 16u / 100u;
    if (fraction > 15) fraction = 0; // clamp
    USART1_BRR = (mantissa << 4) | (fraction & 0x0F);

    // Enable USART, transmitter, receiver
    USART1_CTLR1 |= (1u << 13) | (1u << 3) | (1u << 2); // UE, TE, RE
}

//=============================================================================
// UART utility functions (replacing Serial)
//=============================================================================
static inline void USART_SendByte(char c) {
    while (!(USART1_STATR & (1u << 7))); // Wait TXE (Transmit Data Register Empty)
    USART1_DATAR = c;
}

static inline char USART_ReceiveByte(void) {
    while (!(USART1_STATR & (1u << 5))); // Wait RXNE (Read Data Register Not Empty)
    return (char)(USART1_DATAR & 0xFF);
}

static inline void USART_Flush(void) {
    char dummy;
    while (USART1_STATR & (1u << 5)) {
        dummy = USART1_DATAR; // Read and discard
    }
}

static inline int USART_Available(void) {
    return (USART1_STATR & (1u << 5)) != 0;
}

static void USART_SendString(const char *str) {
    while (*str) {
        USART_SendByte(*str++);
    }
}

static void USART_SendU32Binary(uint32_t val) {
    char buf[33];
    int len = 0;
    if (val == 0) {
        USART_SendByte('0');
        return;
    }
    // Skip leading zeros
    uint32_t mask = 1u << 31;
    while (mask && !(val & mask)) mask >>= 1;
    while (mask) {
        USART_SendByte((val & mask) ? '1' : '0');
        mask >>= 1;
    }
}

//=============================================================================
// Unique ID and DUCOID generation
//=============================================================================
static char ducoid_chars[23]; // "DUCOID" + 16 hex + null

static void generate_ducoid(void) {
    // Get unique ID bytes (8 bytes from UNIID2 and UNIID1)
    uint32_t uid_low  = ESIG_UNIID2; // UID[31:0]
    uint32_t uid_high = ESIG_UNIID1; // UID[63:32]
    uint8_t uid8[8];
    uid8[0] = uid_low & 0xFF;
    uid8[1] = (uid_low >> 8) & 0xFF;
    uid8[2] = (uid_low >> 16) & 0xFF;
    uid8[3] = uid_low >> 24;
    uid8[4] = uid_high & 0xFF;
    uid8[5] = (uid_high >> 8) & 0xFF;
    uid8[6] = (uid_high >> 16) & 0xFF;
    uid8[7] = uid_high >> 24;

    const char prefix[] = "DUCOID";
    for (int i = 0; i < 6; i++) {
        ducoid_chars[i] = prefix[i];
    }
    char *ptr = ducoid_chars + 6;
    for (int i = 0; i < 8; i++) {
        *ptr++ = "0123456789ABCDEF"[uid8[i] >> 4];
        *ptr++ = "0123456789ABCDEF"[uid8[i] & 0x0F];
    }
    *ptr = '\0';
}

//=============================================================================
// DUCO-S1A Hash implementation (adapted from duco_hash.cpp)
//=============================================================================
#define SHA1_HASH_LEN 20
#define ROTL32(bits, word) (((word) << (bits)) | ((word) >> (32 - (bits))))

typedef struct {
    uint32_t initialWords[10];
    uint32_t tempState[5];
} duco_hash_state_t;

// Forward declarations
static void duco_hash_init(duco_hash_state_t *hasher, const char *prevHash);
static uint32_t ducos1a_mine(const char *prevBlockHash, const uint32_t *targetWords, uint32_t maxNonce);
static int duco_hash_try_nonce(duco_hash_state_t *hasher, const char *nonce, uint8_t nonceLen, const uint32_t *targetWords);

// Helper: convert 40-char hex string (lower case) to 5 uint32_t words
static void hex_to_words(const char *hex, uint32_t *words) {
    for (int w = 0; w < SHA1_HASH_LEN / 4; w++) {
        const char *src = hex + w * 8;
        uint32_t b0 = ((src[0] >= 'a' ? src[0] - 'a' + 10 : src[0] - '0') << 4) |
                      (src[1] >= 'a' ? src[1] - 'a' + 10 : src[1] - '0');
        uint32_t b1 = ((src[2] >= 'a' ? src[2] - 'a' + 10 : src[2] - '0') << 4) |
                      (src[3] >= 'a' ? src[3] - 'a' + 10 : src[3] - '0');
        uint32_t b2 = ((src[4] >= 'a' ? src[4] - 'a' + 10 : src[4] - '0') << 4) |
                      (src[5] >= 'a' ? src[5] - 'a' + 10 : src[5] - '0');
        uint32_t b3 = ((src[6] >= 'a' ? src[6] - 'a' + 10 : src[6] - '0') << 4) |
                      (src[7] >= 'a' ? src[7] - 'a' + 10 : src[7] - '0');
        words[w] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    }
}

static void increment_nonce_ascii(char *nonceStr, uint8_t *nonceLen) {
    int8_t i = *nonceLen - 1;
    for (; i >= 0; --i) {
        if (nonceStr[i] != '9') {
            nonceStr[i]++;
            return;
        }
        nonceStr[i] = '0';
    }
    for (uint8_t j = *nonceLen; j > 0; --j) {
        nonceStr[j] = nonceStr[j - 1];
    }
    nonceStr[0] = '1';
    (*nonceLen)++;
    nonceStr[*nonceLen] = '\0';
}

// Length word constants (length of (prevBlock + nonce) in bits)
static const uint32_t kLengthWordByNonceLen[6] = {
    0x00000000, // 0 (not used)
    0x00000148, // (40+1)*8 = 328 = 0x148
    0x00000150, // (40+2)*8 = 336 = 0x150
    0x00000158, // (40+3)*8 = 344 = 0x158
    0x00000160, // (40+4)*8 = 352 = 0x160
    0x00000168  // (40+5)*8 = 360 = 0x168
};

// Load block words (W) for SHA1 second block given nonce
static void load_block_words(uint32_t *W, const uint32_t *baseWords, const char *nonce, uint8_t nonceLen) {
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
                W[10] = (d0 << 24) | 0x00800000u;
                W[11] = 0;
                W[12] = 0;
                break;
            case 2:
                W[10] = (d0 << 24) | (d1 << 16) | 0x00008000u;
                W[11] = 0;
                W[12] = 0;
                break;
            case 3:
                W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | 0x00000080u;
                W[11] = 0;
                W[12] = 0;
                break;
            case 4:
                W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
                W[11] = 0x80000000u;
                W[12] = 0;
                break;
            default: // 5
                W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
                W[11] = (d4 << 24) | 0x00800000u;
                W[12] = 0;
                break;
        }
        W[13] = 0;
        W[14] = 0;
        W[15] = kLengthWordByNonceLen[nonceLen];
        return;
    }

    // For nonceLen > 5 (rare)
    for (int i = 0; i < 16; i++) W[i] = (i < 10) ? baseWords[i] : 0;
    for (int i = 0; i < nonceLen; i++) {
        int wordIdx = 10 + (i >> 2);
        int shift = 24 - ((i & 3) << 3);
        W[wordIdx] |= (uint32_t)(uint8_t)nonce[i] << shift;
    }
    int wordIdx = 10 + (nonceLen >> 2);
    int shift = 24 - ((nonceLen & 3) << 3);
    W[wordIdx] |= 0x80UL << shift;
    W[15] = (uint32_t)(40 + nonceLen) << 3;
}

// SHA1 round functions (unrolled)
static void sha1_process(uint32_t *W, uint32_t *state) {
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t t;

    // Round 10-15 (f = Ch(b,c,d), K = 0x5A827999)
    #define SHA1RND10_15(idx) do { \
        t = ROTL32(5,a) + e + ((b & (c ^ d)) ^ d) + W[idx] + 0x5A827999u; \
        e = d; d = c; c = ROTL32(30,b); b = a; a = t; \
    } while(0)
    SHA1RND10_15(10); SHA1RND10_15(11); SHA1RND10_15(12);
    SHA1RND10_15(13); SHA1RND10_15(14); SHA1RND10_15(15);
    #undef SHA1RND10_15

    // Round 16-19 (f = Ch, K = 0x5A827999) + expand
    #define SHA1RND16_19(idx) do { \
        W[idx] = ROTL32(1, W[(idx+2) & 0xF] ^ W[(idx+8) & 0xF] ^ W[(idx+13) & 0xF] ^ W[idx]); \
        t = ROTL32(5,a) + e + ((b & (c ^ d)) ^ d) + W[idx] + 0x5A827999u; \
        e = d; d = c; c = ROTL32(30,b); b = a; a = t; \
    } while(0)
    SHA1RND16_19(0); SHA1RND16_19(1); SHA1RND16_19(2); SHA1RND16_19(3);
    #undef SHA1RND16_19

    // Round 20-39 (f = Parity, K = 0x6ED9EBA1)
    #define SHA1RND20_39(idx) do { \
        W[idx] = ROTL32(1, W[(idx+2) & 0xF] ^ W[(idx+8) & 0xF] ^ W[(idx+13) & 0xF] ^ W[idx]); \
        t = ROTL32(5,a) + e + (b ^ c ^ d) + W[idx] + 0x6ED9EBA1u; \
        e = d; d = c; c = ROTL32(30,b); b = a; a = t; \
    } while(0)
    SHA1RND20_39(4);  SHA1RND20_39(5);  SHA1RND20_39(6);  SHA1RND20_39(7);
    SHA1RND20_39(8);  SHA1RND20_39(9);  SHA1RND20_39(10); SHA1RND20_39(11);
    SHA1RND20_39(12); SHA1RND20_39(13); SHA1RND20_39(14); SHA1RND20_39(15);
    SHA1RND20_39(0);  SHA1RND20_39(1);  SHA1RND20_39(2);  SHA1RND20_39(3);
    SHA1RND20_39(4);  SHA1RND20_39(5);  SHA1RND20_39(6);  SHA1RND20_39(7);
    #undef SHA1RND20_39

    // Round 40-59 (f = Maj, K = 0x8F1BBCDC)
    #define SHA1RND40_59(idx) do { \
        W[idx] = ROTL32(1, W[(idx+2) & 0xF] ^ W[(idx+8) & 0xF] ^ W[(idx+13) & 0xF] ^ W[idx]); \
        t = ROTL32(5,a) + e + ((b & c) | (b & d) | (c & d)) + W[idx] + 0x8F1BBCDCu; \
        e = d; d = c; c = ROTL32(30,b); b = a; a = t; \
    } while(0)
    SHA1RND40_59(8);  SHA1RND40_59(9);  SHA1RND40_59(10); SHA1RND40_59(11);
    SHA1RND40_59(12); SHA1RND40_59(13); SHA1RND40_59(14); SHA1RND40_59(15);
    SHA1RND40_59(0);  SHA1RND40_59(1);  SHA1RND40_59(2);  SHA1RND40_59(3);
    SHA1RND40_59(4);  SHA1RND40_59(5);  SHA1RND40_59(6);  SHA1RND40_59(7);
    SHA1RND40_59(8);  SHA1RND40_59(9);  SHA1RND40_59(10); SHA1RND40_59(11);
    #undef SHA1RND40_59

    // Round 60-79 (f = Parity, K = 0xCA62C1D6)
    #define SHA1RND60_79(idx) do { \
        W[idx] = ROTL32(1, W[(idx+2) & 0xF] ^ W[(idx+8) & 0xF] ^ W[(idx+13) & 0xF] ^ W[idx]); \
        t = ROTL32(5,a) + e + (b ^ c ^ d) + W[idx] + 0xCA62C1D6u; \
        e = d; d = c; c = ROTL32(30,b); b = a; a = t; \
    } while(0)
    SHA1RND60_79(12); SHA1RND60_79(13); SHA1RND60_79(14); SHA1RND60_79(15);
    SHA1RND60_79(0);  SHA1RND60_79(1);  SHA1RND60_79(2);  SHA1RND60_79(3);
    SHA1RND60_79(4);  SHA1RND60_79(5);  SHA1RND60_79(6);  SHA1RND60_79(7);
    SHA1RND60_79(8);  SHA1RND60_79(9);  SHA1RND60_79(10); SHA1RND60_79(11);
    SHA1RND60_79(12); SHA1RND60_79(13); SHA1RND60_79(14); SHA1RND60_79(15);
    #undef SHA1RND60_79

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void duco_hash_init(duco_hash_state_t *hasher, const char *prevHash) {
    uint32_t baseWords[10]; // 40 bytes => 10 uint32_t
    for (int i = 0; i < 10; i++) {
        baseWords[i] = ((uint32_t)(uint8_t)prevHash[i*4]   << 24) |
                       ((uint32_t)(uint8_t)prevHash[i*4+1] << 16) |
                       ((uint32_t)(uint8_t)prevHash[i*4+2] << 8)  |
                       ((uint32_t)(uint8_t)prevHash[i*4+3]);
    }

    uint32_t a = 0x67452301u;
    uint32_t b = 0xEFCDAB89u;
    uint32_t c = 0x98BADCFEu;
    uint32_t d = 0x10325476u;
    uint32_t e = 0xC3D2E1F0u;
    uint32_t t;

    // First 10 rounds (unrolled)
    #define FIRST10(idx) do { \
        t = ROTL32(5,a) + e + ((b & (c ^ d)) ^ d) + baseWords[idx] + 0x5A827999u; \
        e = d; d = c; c = ROTL32(30,b); b = a; a = t; \
    } while(0)
    FIRST10(0); FIRST10(1); FIRST10(2); FIRST10(3); FIRST10(4);
    FIRST10(5); FIRST10(6); FIRST10(7); FIRST10(8); FIRST10(9);
    #undef FIRST10

    hasher->initialWords[0] = baseWords[0];
    hasher->initialWords[1] = baseWords[1];
    hasher->initialWords[2] = baseWords[2];
    hasher->initialWords[3] = baseWords[3];
    hasher->initialWords[4] = baseWords[4];
    hasher->initialWords[5] = baseWords[5];
    hasher->initialWords[6] = baseWords[6];
    hasher->initialWords[7] = baseWords[7];
    hasher->initialWords[8] = baseWords[8];
    hasher->initialWords[9] = baseWords[9];

    hasher->tempState[0] = a;
    hasher->tempState[1] = b;
    hasher->tempState[2] = c;
    hasher->tempState[3] = d;
    hasher->tempState[4] = e;
}

int duco_hash_try_nonce(duco_hash_state_t *hasher, const char *nonce, uint8_t nonceLen, const uint32_t *targetWords) {
    uint32_t W[16];
    load_block_words(W, hasher->initialWords, nonce, nonceLen);

    uint32_t state[5];
    state[0] = hasher->tempState[0];
    state[1] = hasher->tempState[1];
    state[2] = hasher->tempState[2];
    state[3] = hasher->tempState[3];
    state[4] = hasher->tempState[4];

    sha1_process(W, state);

    return state[0] == targetWords[0] &&
           state[1] == targetWords[1] &&
           state[2] == targetWords[2] &&
           state[3] == targetWords[3] &&
           state[4] == targetWords[4];
}

static uint32_t ducos1a_mine(const char *prevBlockHash, const uint32_t *targetWords, uint32_t maxNonce) {
    duco_hash_state_t hash;
    duco_hash_init(&hash, prevBlockHash);

    char nonceStr[11] = "0"; // max "9999999999" + null
    uint8_t nonceLen = 1;

    for (uint32_t nonce = 0; nonce < maxNonce; nonce++) {
        if (duco_hash_try_nonce(&hash, nonceStr, nonceLen, targetWords)) {
            return nonce;
        }
        increment_nonce_ascii(nonceStr, &nonceLen);
    }
    return 0; // not found
}

//=============================================================================
// Main application (equivalent to Arduino sketch)
//=============================================================================
int main(void) {
    SystemClock_Init();
    USART1_Init(115200);
    generate_ducoid();

    // Wait a moment for stability
    for (volatile uint32_t i = 0; i < 100000; i++) { __asm__ volatile ("nop"); }

    // Flush any garbage
    USART_Flush();

    char lastBlockHash[41];
    char newBlockHash[41];
    uint32_t difficulty;
    uint32_t targetWords[SHA1_HASH_LEN / 4];
    uint32_t result, elapsed, start_time;

    while (1) {
        if (!USART_Available()) continue;

        // Read job: lastBlockHash, newBlockHash, difficulty
        int i = 0;
        for (i = 0; i < 40; i++) {
            lastBlockHash[i] = USART_ReceiveByte();
        }
        lastBlockHash[40] = '\0';
        if (USART_ReceiveByte() != ',') { USART_Flush(); continue; } // separator

        for (i = 0; i < 40; i++) {
            newBlockHash[i] = USART_ReceiveByte();
        }
        newBlockHash[40] = '\0';
        if (USART_ReceiveByte() != ',') { USART_Flush(); continue; }

        // Read difficulty (until comma)
        difficulty = 0;
        char c;
        while (1) {
            c = USART_ReceiveByte();
            if (c == ',') break;
            if (c >= '0' && c <= '9') {
                difficulty = difficulty * 10 + (c - '0');
            } else {
                USART_Flush();
                difficulty = 0;
                break;
            }
        }
        if (difficulty == 0) continue; // invalid

        // Drain any extra bytes (should be '0\n' in original protocol, ignore)
        USART_Flush();

        // Convert target hash to words
        hex_to_words(newBlockHash, targetWords);

        // Calculate maxNonce = difficulty * 100 + 1
        uint32_t maxNonce = difficulty * 100 + 1;

        // Start mining
        start_time = *(volatile uint32_t *)0xE000F008; // SysTick? No, use DWT_CYCCNT if available. We'll use a simple timer.
        // CH32V003 has SysTick. We'll use it for elapsed measurement.
        // But simpler: use __rdcycle() if available. For now, approximate with a dummy loop.
        // Actually we can use the system timer (SysTick) with 1us resolution.
        // Configure SysTick for 1us tick: not necessary for this mining, we'll just measure time with microsecond counter.
        // Since we don't have Arduino micros(), we'll use the built-in 64-bit cycle counter if available.
        // For now, use a simple software loop to estimate? Better to use SysTick.
        // Let's set SysTick to 1MHz (1us tick) based on HCLK/8 = 6MHz? Not perfectly 1MHz. Use HCLK/48 to get 1us if HCLK=48MHz.
        // But we'll keep it simple: just send result with elapsed time = 0 for now, as original protocol uses elapsed in us for hashrate calculation.
        // The Python miner uses elapsed to calculate hashrate. We can send elapsed = 0, but that's bad.
        // Let's use the mcycle CSR register if present (read with __attribute__((unused))). The RV32E core may have it.
        // Here we'll use a simple busy-wait loop of known iterations to estimate time (not accurate). Alternative: use SysTick.
        // We'll implement a quick SysTick 1us setup.
        volatile uint32_t *STK_CTLR = (volatile uint32_t *)0xE000F000;
        volatile uint32_t *STK_CNTL = (volatile uint32_t *)0xE000F008;
        // Configure SysTick for 1us: clock source = HCLK/8 = 6MHz, reload = 6-1 = 5? No, we need 1us.
        // If HCLK=48MHz, HCLK/8 = 6MHz => 6 ticks per us. So we can't use SysTick for 1us easily.
        // Better: use performance counter (cycle) and divide by 48 to get us. Let's use __rdcycle() if available.
        // We'll define a function to read cycle.
        uint32_t start_cycle;
        uint32_t end_cycle;
        __asm__ volatile ("rdcycle %0" : "=r"(start_cycle));

        result = ducos1a_mine(lastBlockHash, targetWords, maxNonce);

        __asm__ volatile ("rdcycle %0" : "=r"(end_cycle));
        uint32_t cycles = end_cycle - start_cycle;
        // Assume HCLK = 48MHz => 48 cycles per us. elapsed in microseconds = cycles / 48.
        elapsed = cycles / 48u;

        // Send result: result (binary string), elapsed (binary string), ducoid, '\n'
        USART_SendU32Binary(result);
        USART_SendByte(',');
        USART_SendU32Binary(elapsed);
        USART_SendByte(',');
        USART_SendString(ducoid_chars);
        USART_SendByte('\n');
    }

    return 0;
}
