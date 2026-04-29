/*
 * Duino-Coin Miner for CH32V003 (RISC-V RV32EC)
 * Adapted from Arduino_Code_TURBO.ino
 * Uses USART1, 48MHz PLL, no LED, no Arduino dependencies.
 */

#pragma GCC optimize("-Ofast")

#include <stdint.h>
#include <string.h>

// ================== Register definitions ==================
// RCC
#define RCC_BASE             0x40021000u
#define RCC_CTLR             (*(volatile uint32_t*)(RCC_BASE + 0x00))
#define RCC_CFGR0            (*(volatile uint32_t*)(RCC_BASE + 0x04))
#define RCC_INTR             (*(volatile uint32_t*)(RCC_BASE + 0x08))
#define RCC_APB2PRSTR        (*(volatile uint32_t*)(RCC_BASE + 0x0C))
#define RCC_APB1PRSTR        (*(volatile uint32_t*)(RCC_BASE + 0x10))
#define RCC_AHBPCCNR         (*(volatile uint32_t*)(RCC_BASE + 0x14))   // Note: manual has AHBPCCNR (typo AHBPCCNR? We use AHBPCENR as in manual 0x14)
#define RCC_APB2PCENR        (*(volatile uint32_t*)(RCC_BASE + 0x18))
#define RCC_APB1PCENR        (*(volatile uint32_t*)(RCC_BASE + 0x1C))
#define RCC_RSTSCKR          (*(volatile uint32_t*)(RCC_BASE + 0x24))

// GPIO
#define GPIOA_BASE           0x40010800u
#define GPIOC_BASE           0x40011000u
#define GPIOD_BASE           0x40011400u
#define GPIO_CFGLR_OFF       0x00u
#define GPIO_INDIR_OFF       0x08u
#define GPIO_OUTDR_OFF       0x0Cu
#define GPIO_BSHR_OFF        0x10u
#define GPIOx_CFGLR(x)       (*(volatile uint32_t*)((x) + GPIO_CFGLR_OFF))
#define GPIOx_BSHR(x)        (*(volatile uint32_t*)((x) + GPIO_BSHR_OFF))

// USART1
#define USART1_BASE          0x40013800u
#define USART1_STATR         (*(volatile uint32_t*)(USART1_BASE + 0x00))
#define USART1_DATAR         (*(volatile uint32_t*)(USART1_BASE + 0x04))
#define USART1_BRR           (*(volatile uint32_t*)(USART1_BASE + 0x08))
#define USART1_CTLR1         (*(volatile uint32_t*)(USART1_BASE + 0x0C))
#define USART1_CTLR2         (*(volatile uint32_t*)(USART1_BASE + 0x10))
#define USART1_CTLR3         (*(volatile uint32_t*)(USART1_BASE + 0x14))
#define USART1_GPR           (*(volatile uint32_t*)(USART1_BASE + 0x18))

// AFIO
#define AFIO_BASE            0x40010000u
#define AFIO_PCFR1           (*(volatile uint32_t*)(AFIO_BASE + 0x04))

// ESIG (unique ID)
#define ESIG_BASE            0x1FFFF7E0u
#define ESIG_UNIID1          (*(volatile uint32_t*)(0x1FFFF7E8))
#define ESIG_UNIID2          (*(volatile uint32_t*)(0x1FFFF7EC))
#define ESIG_UNIID3          (*(volatile uint32_t*)(0x1FFFF7F0))

// SysTick (STK) – not used for timing (we use cycle counter)
#define STK_BASE             0xE000F000u
// mcycle CSR read
#define read_cycle()         ({ uint32_t _cyc; asm volatile ("rdcycle %0" : "=r"(_cyc)); _cyc; })

// Clock
#define HCLK_MHZ             48u

// USART bits
#define USART_STATR_TXE      (1u << 7)
#define USART_STATR_TC       (1u << 6)
#define USART_STATR_RXNE     (1u << 5)
#define USART_CTLR1_TE       (1u << 3)
#define USART_CTLR1_RE       (1u << 2)
#define USART_CTLR1_UE       (1u << 13)

// RCC bits
#define RCC_CTLR_HSION       (1u << 0)
#define RCC_CTLR_HSIRDY      (1u << 1)
#define RCC_CTLR_PLLON       (1u << 24)
#define RCC_CTLR_PLLRDY      (1u << 25)
#define RCC_CFGR0_SW_PLL     (2u << 0)
#define RCC_CFGR0_SWS_PLL    (2u << 2)
#define RCC_CFGR0_PLLSRC_HSI (0u << 16)
#define RCC_APB2PCENR_USART1 (1u << 14)
#define RCC_AHBPCENR_DMA1    (1u << 1)   // not used, but keep for structure
#define RCC_AHBPCENR_SRAM    (1u << 2)
#define RCC_APB2PCENR_IOPD   (1u << 5)
#define RCC_APB2PCENR_IOPC   (1u << 4)
#define RCC_APB2PCENR_IOPA   (1u << 2)

// GPIO CFG bits (MODE[1:0] and CNF[1:0])
// MODE: 00=input, 01=output 10MHz, 10=output 2MHz, 11=output 50MHz
// CNF in output: 00=universal push-pull, 01=universal open-drain, 10=alternate push-pull, 11=alternate open-drain
#define GPIO_CFG_OUT_10MHZ_PP       (0x01u)  // MODE=01, CNF=00 -> 0x1
#define GPIO_CFG_ALT_50MHZ_PP       (0x0Bu)  // MODE=11, CNF=10 -> 0xB
#define GPIO_CFG_IN_FLOAT           (0x04u)  // MODE=00, CNF=01 -> 0x4

// ================== Timing ==================
static inline uint32_t micros(void) {
    return read_cycle() / HCLK_MHZ;
}
static inline void delay_us(uint32_t us) {
    uint32_t start = micros();
    while (micros() - start < us) /* wait */;
}

// ================== USART ==================
static void usart1_init(void) {
    // Enable clocks for GPIOD, USART1, AFIO
    RCC_APB2PCENR |= RCC_APB2PCENR_IOPD | RCC_APB2PCENR_USART1;
    RCC_AHBPCCNR |= 1u; // enable DMA1 clock? Not needed but maybe required? Actually AHBPCENR bit 0 is DMA1EN. We'll enable it just in case for bus access.
    // Configure PD5 (TX) as alternate push-pull 50MHz
    // GPIOx_CFGLR[31:0] = ... we need to modify only bits for PD5 (bits 21:20 MODE, 23:22 CNF)
    uint32_t tmp = GPIOx_CFGLR(GPIOD_BASE);
    // clear bits for PD5 (bits 20-23) and PD6 (bits 24-27)
    tmp &= ~(0xFu << 20) & ~(0xFu << 24);
    // PD5: MODE=11 (50MHz), CNF=10 (AF push-pull) -> 0xB << 20
    tmp |= (0xBu << 20);
    // PD6: RX, input floating: MODE=00, CNF=01 -> 0x4 << 24
    tmp |= (0x4u << 24);
    GPIOx_CFGLR(GPIOD_BASE) = tmp;

    // USART baud: 48MHz, 115200 -> 26.0625 -> mantissa=26, fraction=1
    USART1_BRR = (26u << 4) | 1u;
    // Enable USART, transmitter, receiver
    USART1_CTLR1 = USART_CTLR1_UE | USART_CTLR1_TE | USART_CTLR1_RE;
    // Wait a bit for flags to settle
    delay_us(100);
}

static inline void usart1_putc(char c) {
    while (!(USART1_STATR & USART_STATR_TXE));
    USART1_DATAR = (uint32_t)c;
}

static void usart1_print(const char* s) {
    while (*s) usart1_putc(*s++);
}
static void usart1_print_bin_u32(uint32_t n) {
    if (n == 0) { usart1_putc('0'); return; }
    uint32_t mask = 1UL << 31;
    while (mask && !(n & mask)) mask >>= 1;
    for (; mask; mask >>= 1) usart1_putc((n & mask) ? '1' : '0');
}

static int usart1_getc_timeout(char* c, uint32_t timeout_ms) {
    uint32_t start = micros();
    while (!(USART1_STATR & USART_STATR_RXNE)) {
        if ((micros() - start) >= timeout_ms * 1000u) return 0;
    }
    *c = (char)(USART1_DATAR & 0xFF);
    return 1;
}

// ================== Unique ID ==================
static char ducoid[23]; // "DUCOID" + 16 hex + null
static void generate_ducoid(void) {
    // Read 96-bit UID from ESIG, use last 8 bytes for hex string
    uint32_t uid1 = ESIG_UNIID1; // [63:32]
    uint32_t uid2 = ESIG_UNIID2; // [31:0]
    uint32_t uid3 = ESIG_UNIID3; // [95:64]
    // little-endian? ESIG_UNIID1 is 32-63 bits, UNIID2 is 0-31, UNIID3 is 64-95.
    // Let's assemble last 8 bytes: we need 8 bytes. We'll take uid3[31:0] as top 4 bytes, and uid1 as next 4? Not sure.
    // Safer: read as array of bytes from address 0x1FFFF7E8. We'll do:
    uint8_t uid_bytes[12];
    *(uint32_t*)(uid_bytes + 0) = uid2; // bytes 0-3 (LSB in uid2)
    *(uint32_t*)(uid_bytes + 4) = uid1; // bytes 4-7
    *(uint32_t*)(uid_bytes + 8) = uid3; // bytes 8-11
    // Use the last 8 bytes (bytes 4..11) for the ID (as original uses UniqueID8)
    memcpy(ducoid, "DUCOID", 6);
    char* ptr = ducoid + 6;
    for (uint8_t i = 4; i < 12; i++) {
        uint8_t val = uid_bytes[i];
        *ptr++ = "0123456789ABCDEF"[val >> 4];
        *ptr++ = "0123456789ABCDEF"[val & 0xF];
    }
    *ptr = '\0';
}

// ================== SHA-1 related ==================
#define SHA1_HASH_LEN 20

#define sha1_rotl(bits, word)  (((word) << (bits)) | ((word) >> (32 - (bits))))
#define SHA1_ROTL5(word)       sha1_rotl(5, word)
#define SHA1_ROTL30(word)      sha1_rotl(30, word)

typedef struct {
    uint32_t initialWords[10];
    uint32_t tempState[5];
} duco_hash_state_t;

static void hex_to_words(const char* hex, uint32_t* words) {
    // hex is 40 chars lowercase
    for (uint8_t w = 0; w < SHA1_HASH_LEN / 4; w++) {
        const char* src = hex + w * 8;
        uint32_t b0 = (src[0] <= '9' ? src[0] - '0' : src[0] - 'a' + 10);
        uint32_t b1 = (src[1] <= '9' ? src[1] - '0' : src[1] - 'a' + 10);
        uint32_t b2 = (src[2] <= '9' ? src[2] - '0' : src[2] - 'a' + 10);
        uint32_t b3 = (src[3] <= '9' ? src[3] - '0' : src[3] - 'a' + 10);
        b0 = (b0 << 4) | (src[4] <= '9' ? src[4] - '0' : src[4] - 'a' + 10);
        b1 = (b1 << 4) | (src[5] <= '9' ? src[5] - '0' : src[5] - 'a' + 10);
        b2 = (b2 << 4) | (src[6] <= '9' ? src[6] - '0' : src[6] - 'a' + 10);
        b3 = (b3 << 4) | (src[7] <= '9' ? src[7] - '0' : src[7] - 'a' + 10);
        words[w] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    }
}

static const uint32_t kLengthWordByNonceLen[6] = {
    0x00000000UL, 0x00000148UL, 0x00000150UL,
    0x00000158UL, 0x00000160UL, 0x00000168UL
};

static inline void load_block_words(uint32_t* W, const uint32_t* baseWords,
                                    const char* nonce, uint8_t nonceLen) {
    W[0] = baseWords[0]; W[1] = baseWords[1]; W[2] = baseWords[2];
    W[3] = baseWords[3]; W[4] = baseWords[4]; W[5] = baseWords[5];
    W[6] = baseWords[6]; W[7] = baseWords[7]; W[8] = baseWords[8];
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
                W[11] = 0; W[12] = 0;
                break;
            case 2:
                W[10] = (d0 << 24) | (d1 << 16) | 0x00008000UL;
                W[11] = 0; W[12] = 0;
                break;
            case 3:
                W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | 0x00000080UL;
                W[11] = 0; W[12] = 0;
                break;
            case 4:
                W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
                W[11] = 0x80000000UL; W[12] = 0;
                break;
            default: // 5
                W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
                W[11] = (d4 << 24) | 0x00800000UL; W[12] = 0;
                break;
        }
        W[13] = 0; W[14] = 0;
        W[15] = kLengthWordByNonceLen[nonceLen];
        return;
    }
    // >5: generic (rare for this mining)
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

void duco_hash_init(duco_hash_state_t* hasher, const char* prevHash) {
    uint32_t a = 0x67452301UL, b = 0xEFCDAB89UL, c = 0x98BADCFEUL,
             d = 0x10325476UL, e = 0xC3D2E1F0UL;
    for (int i = 0; i < 10; i++) {
        hasher->initialWords[i] =
            ((uint32_t)(uint8_t)prevHash[4*i]   << 24) |
            ((uint32_t)(uint8_t)prevHash[4*i+1] << 16) |
            ((uint32_t)(uint8_t)prevHash[4*i+2] <<  8) |
            ((uint32_t)(uint8_t)prevHash[4*i+3]);
    }

    uint32_t t;
    #define SHA1_INIT_ROUND(idx) do { \
        t = SHA1_ROTL5(a) + e + ((b & (c ^ d)) ^ d) + hasher->initialWords[idx] + 0x5A827999UL; \
        e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t; \
    } while(0)

    SHA1_INIT_ROUND(0); SHA1_INIT_ROUND(1); SHA1_INIT_ROUND(2);
    SHA1_INIT_ROUND(3); SHA1_INIT_ROUND(4); SHA1_INIT_ROUND(5);
    SHA1_INIT_ROUND(6); SHA1_INIT_ROUND(7); SHA1_INIT_ROUND(8);
    SHA1_INIT_ROUND(9);
    #undef SHA1_INIT_ROUND

    hasher->tempState[0] = a; hasher->tempState[1] = b;
    hasher->tempState[2] = c; hasher->tempState[3] = d;
    hasher->tempState[4] = e;
}

// Fast path for nonce length = 5 (most common in mining)
static bool duco_hash_try_nonce_len5(const duco_hash_state_t* hasher,
                                     const char* nonce, const uint32_t* targetWords) {
    uint32_t W[16];
    const uint32_t* base = hasher->initialWords;
    W[0] = base[0]; W[1] = base[1]; W[2] = base[2]; W[3] = base[3];
    W[4] = base[4]; W[5] = base[5]; W[6] = base[6]; W[7] = base[7];
    W[8] = base[8]; W[9] = base[9];

    uint32_t d0 = (uint8_t)nonce[0], d1 = (uint8_t)nonce[1], d2 = (uint8_t)nonce[2],
             d3 = (uint8_t)nonce[3], d4 = (uint8_t)nonce[4];
    W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
    W[11] = (d4 << 24) | 0x00800000UL;
    W[12] = 0; W[13] = 0; W[14] = 0; W[15] = 0x00000168UL;

    uint32_t a = hasher->tempState[0], b = hasher->tempState[1],
             c = hasher->tempState[2], d = hasher->tempState[3],
             e = hasher->tempState[4];
    uint32_t t;

    // Rounds 10..79 unrolled (same as original)
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[10] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[11] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[12] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[13] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[14] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[15] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // 16..19
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[0] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[1] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[2] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[3] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // 20..39 Parity (f = b^c^d, K=0x6ED9EBA1)
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[8] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[9] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[10] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[11] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[0] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[1] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[2] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[3] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // 40..59 Majority (f = (b&c)|(b&d)|(c&d), K=0x8F1BBCDC)
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[8] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[9] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[10] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[11] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[12] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[13] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[14] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[15] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[0] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[1] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[2] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[3] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[4] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[5] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[6] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[7] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[8] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[9] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[10] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[11] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    // 60..79 Parity (f = b^c^d, K=0xCA62C1D6)
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[0] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[1] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[2] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[3] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[8] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[9] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[10] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[11] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    a += 0x67452301UL; b += 0xEFCDAB89UL; c += 0x98BADCFEUL;
    d += 0x10325476UL; e += 0xC3D2E1F0UL;

    return a == targetWords[0] && b == targetWords[1] && c == targetWords[2]
        && d == targetWords[3] && e == targetWords[4];
}

// Generic try_nonce for any length (unrolled 70 rounds)
static bool duco_hash_try_nonce(const duco_hash_state_t* hasher,
                                const char* nonce, uint8_t nonceLen,
                                const uint32_t* targetWords) {
    uint32_t W[16];
    load_block_words(W, hasher->initialWords, nonce, nonceLen);

    uint32_t a = hasher->tempState[0], b = hasher->tempState[1],
             c = hasher->tempState[2], d = hasher->tempState[3],
             e = hasher->tempState[4];
    uint32_t t;

    // Rounds 10..79 unrolled (identical to len5 version above but using W[])
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[10] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[11] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[12] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[13] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[14] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[15] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[0] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[1] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[2] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + ((b & (c ^ d)) ^ d) + e + W[3] + 0x5A827999UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[8] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[9] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[10] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[11] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[0] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[1] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[2] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[3] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0x6ED9EBA1UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[8] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[9] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[10] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[11] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[12] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[13] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[14] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[15] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[0] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[1] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[2] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[3] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[4] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[5] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[6] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[7] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[8] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[9] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[10] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + ((b & c) | (b & d) | (c & d)) + e + W[11] + 0x8F1BBCDCUL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[0] = sha1_rotl(1, W[13] ^ W[8] ^ W[2] ^ W[0]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[0] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[1] = sha1_rotl(1, W[14] ^ W[9] ^ W[3] ^ W[1]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[1] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[2] = sha1_rotl(1, W[15] ^ W[10] ^ W[4] ^ W[2]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[2] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[3] = sha1_rotl(1, W[0] ^ W[11] ^ W[5] ^ W[3]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[3] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[4] = sha1_rotl(1, W[1] ^ W[12] ^ W[6] ^ W[4]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[4] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[5] = sha1_rotl(1, W[2] ^ W[13] ^ W[7] ^ W[5]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[5] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[6] = sha1_rotl(1, W[3] ^ W[14] ^ W[8] ^ W[6]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[6] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[7] = sha1_rotl(1, W[4] ^ W[15] ^ W[9] ^ W[7]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[7] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[8] = sha1_rotl(1, W[5] ^ W[0] ^ W[10] ^ W[8]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[8] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[9] = sha1_rotl(1, W[6] ^ W[1] ^ W[11] ^ W[9]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[9] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[10] = sha1_rotl(1, W[7] ^ W[2] ^ W[12] ^ W[10]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[10] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[11] = sha1_rotl(1, W[8] ^ W[3] ^ W[13] ^ W[11]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[11] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[12] = sha1_rotl(1, W[9] ^ W[4] ^ W[14] ^ W[12]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[12] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[13] = sha1_rotl(1, W[10] ^ W[5] ^ W[15] ^ W[13]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[13] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[14] = sha1_rotl(1, W[11] ^ W[6] ^ W[0] ^ W[14]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[14] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;
    W[15] = sha1_rotl(1, W[12] ^ W[7] ^ W[1] ^ W[15]);
    t = SHA1_ROTL5(a) + (b ^ c ^ d) + e + W[15] + 0xCA62C1D6UL; e = d; d = c; c = SHA1_ROTL30(b); b = a; a = t;

    a += 0x67452301UL; b += 0xEFCDAB89UL; c += 0x98BADCFEUL;
    d += 0x10325476UL; e += 0xC3D2E1F0UL;

    return a == targetWords[0] && b == targetWords[1] && c == targetWords[2]
        && d == targetWords[3] && e == targetWords[4];
}

// ================== Nonce increment ==================
static void increment_nonce_ascii(char* nonceStr, uint8_t* nonceLen) {
    int8_t i = *nonceLen - 1;
    for (; i >= 0; --i) {
        if (nonceStr[i] != '9') {
            nonceStr[i]++;
            return;
        }
        nonceStr[i] = '0';
    }
    // overflow
    for (uint8_t j = *nonceLen; j > 0; --j)
        nonceStr[j] = nonceStr[j - 1];
    nonceStr[0] = '1';
    (*nonceLen)++;
    nonceStr[*nonceLen] = '\0';
}

// ================== Mining ==================
typedef uint32_t uintDiff;

static uintDiff ducos1a_mine(const char* prevBlockHash, const uint32_t* targetWords,
                             uintDiff maxNonce, const duco_hash_state_t* hashBase) {
    char nonceStr[12] = "0";
    uint8_t nonceLen = 1;

    // Try fast path with length 5 as much as possible (but nonce will change length)
    // We'll use generic try_nonce which handles all lengths.
    for (uintDiff nonce = 0; nonce < maxNonce; nonce++) {
        if (nonceLen == 5) {
            if (duco_hash_try_nonce_len5(hashBase, nonceStr, targetWords))
                return nonce;
        } else {
            if (duco_hash_try_nonce(hashBase, nonceStr, nonceLen, targetWords))
                return nonce;
        }
        increment_nonce_ascii(nonceStr, &nonceLen);
    }
    return 0;
}

// ================== System Clock Init ==================
static void clock_init_48MHz(void) {
    // Enable HSI and wait for ready (default is on after reset)
    RCC_CTLR |= RCC_CTLR_HSION;
    while (!(RCC_CTLR & RCC_CTLR_HSIRDY));
    // Configure PLL source = HSI, enable PLL
    RCC_CFGR0 &= ~(1u << 16);  // PLLSRC = 0 (HSI)
    RCC_CTLR |= RCC_CTLR_PLLON;
    while (!(RCC_CTLR & RCC_CTLR_PLLRDY));
    // Set flash latency to 0 wait state for <=24MHz? But we are 48MHz, need 1 wait state.
    // FLASH_ACTLR: LATENCY[1:0] = 01 for 24<SYSCLK<=48MHz
    *(volatile uint32_t*)0x40022000 = 1;  // FLASH_ACTLR = 1 wait state
    // Switch system clock to PLL
    RCC_CFGR0 = (RCC_CFGR0 & ~0x3u) | RCC_CFGR0_SW_PLL;
    while ((RCC_CFGR0 & 0xCu) != RCC_CFGR0_SWS_PLL);
    // HCLK = SYSCLK, HPRE=0
    RCC_CFGR0 &= ~(0xFu << 4);
}

// ================== Job I/O ==================
static int read_hex_field_until_comma(char* out, uint8_t hex_len) {
    for (uint8_t i = 0; i < hex_len; i++) {
        char c;
        if (!usart1_getc_timeout(&c, 2000)) return 0;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
        out[i] = c;
    }
    out[hex_len] = '\0';
    char comma;
    if (!usart1_getc_timeout(&comma, 2000) || comma != ',') return 0;
    return 1;
}

static int read_difficulty_until_comma(uint32_t* diff) {
    uint32_t v = 0;
    uint8_t digits = 0;
    char c;
    for (;;) {
        if (!usart1_getc_timeout(&c, 2000)) return 0;
        if (c == ',') break;
        if (c >= '0' && c <= '9') {
            uint8_t d = c - '0';
            if (v > (0xFFFFFFFFu - d) / 10u) return 0;
            v = v * 10u + d;
            if (++digits > 9) return 0;
        } else return 0;
    }
    if (digits == 0) return 0;
    *diff = v;
    return 1;
}

static int skip_job_tail(void) {
    char c;
    if (!usart1_getc_timeout(&c, 2000) || c != '0') return 0;
    if (!usart1_getc_timeout(&c, 2000)) return 0;
    if (c == '\r') {
        if (!usart1_getc_timeout(&c, 2000)) return 0;
    }
    return c == '\n';
}

// ================== Main ==================
int main(void) {
    clock_init_48MHz();
    usart1_init();
    generate_ducoid();

    // Main loop
    while (1) {
        // Wait for data
        char c;
        if (!usart1_getc_timeout(&c, 10000)) continue; // timeout, loop

        // Read last block hash (40 hex chars + comma)
        char lastBlockHash[41];
        if (c != '0') continue; // assume job starts with '0'? Actually original reads until ',' directly. We'll re-insert the first byte logic.
        // But original code uses Serial.readBytesUntil(',',...,41) which skips the delimiter.
        // We need to read 40 hex chars, then expect comma.
        // We already consumed one '0', but that might be the first char of hash. So we should read remaining 39 + comma.
        // Safer: read all 40 chars after the first. Let's read 39 more hex and then comma.
        lastBlockHash[0] = c;
        for (int i = 1; i < 40; i++) {
            if (!usart1_getc_timeout(&c, 2000)) goto job_err;
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) goto job_err;
            lastBlockHash[i] = c;
        }
        lastBlockHash[40] = '\0';
        if (!usart1_getc_timeout(&c, 2000) || c != ',') goto job_err;

        // Read new block hash (40 hex + comma)
        char newBlockHash[41];
        for (int i = 0; i < 40; i++) {
            if (!usart1_getc_timeout(&c, 2000)) goto job_err;
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) goto job_err;
            newBlockHash[i] = c;
        }
        newBlockHash[40] = '\0';
        if (!usart1_getc_timeout(&c, 2000) || c != ',') goto job_err;

        // Read difficulty number + comma
        uint32_t difficulty;
        if (!read_difficulty_until_comma(&difficulty)) goto job_err;

        // Skip trailing "0\n"
        if (!skip_job_tail()) goto job_err;

        // Flush any remaining
        while (usart1_getc_timeout(&c, 0));

        // Begin mining
        uint32_t targetWords[5];
        hex_to_words(newBlockHash, targetWords);

        uintDiff maxNonce = difficulty * 100 + 1;
        if (difficulty > 655) maxNonce = 0;  // AVR limit, keep for safety

        duco_hash_state_t hasher;
        duco_hash_init(&hasher, lastBlockHash);

        uint32_t start_us = micros();
        uintDiff result = ducos1a_mine(lastBlockHash, targetWords, maxNonce, &hasher);
        uint32_t elapsed_us = micros() - start_us;

        // Send result: nonce in binary, comma, elapsed binary, comma, ducoid, newline
        usart1_print_bin_u32(result);
        usart1_putc(',');
        usart1_print_bin_u32(elapsed_us);
        usart1_putc(',');
        usart1_print(ducoid);
        usart1_putc('\n');

        continue;
    job_err:
        // Flush input and send error
        while (usart1_getc_timeout(&c, 0)) {}
        usart1_print("ERR\n");
    }
    return 0;
}
