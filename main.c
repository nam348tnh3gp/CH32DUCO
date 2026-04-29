/*
 * Duino-Coin Miner for CH32V003 (RISC-V RV32EC)
 * Bare-metal NoneOS – USART1, 48MHz PLL
 */

#pragma GCC optimize("-Ofast")

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ================== Register definitions ==================
// RCC
#define RCC_BASE             0x40021000u
#define RCC_CTLR             (*(volatile uint32_t*)(RCC_BASE + 0x00))
#define RCC_CFGR0            (*(volatile uint32_t*)(RCC_BASE + 0x04))
#define RCC_APB2PCENR        (*(volatile uint32_t*)(RCC_BASE + 0x18))
#define RCC_AHBPCCNR         (*(volatile uint32_t*)(RCC_BASE + 0x14))

// GPIO
#define GPIOD_BASE           0x40011400u
#define GPIO_CFGLR_OFF       0x00u
#define GPIOx_CFGLR(x)       (*(volatile uint32_t*)((x) + GPIO_CFGLR_OFF))

// USART1
#define USART1_BASE          0x40013800u
#define USART1_STATR         (*(volatile uint32_t*)(USART1_BASE + 0x00))
#define USART1_DATAR         (*(volatile uint32_t*)(USART1_BASE + 0x04))
#define USART1_BRR           (*(volatile uint32_t*)(USART1_BASE + 0x08))
#define USART1_CTLR1         (*(volatile uint32_t*)(USART1_BASE + 0x0C))

// ESIG (unique ID)
#define ESIG_UNIID1          (*(volatile uint32_t*)(0x1FFFF7E8))
#define ESIG_UNIID2          (*(volatile uint32_t*)(0x1FFFF7EC))
#define ESIG_UNIID3          (*(volatile uint32_t*)(0x1FFFF7F0))

// FLASH
#define FLASH_ACTLR          (*(volatile uint32_t*)0x40022000)

// ================== Clock ==================
#define HCLK_MHZ             48u

static inline uint32_t micros(void) {
    uint32_t cyc;
    asm volatile ("rdcycle %0" : "=r"(cyc));
    return cyc / HCLK_MHZ;
}

static void clock_init_48MHz(void) {
    // HSI enable & wait
    RCC_CTLR |= 1u;  // HSION
    while (!(RCC_CTLR & 2u));  // HSIRDY

    // PLL source = HSI / 2 (HSI=24MHz -> PLL input 12MHz? Actually HSI=24MHz -> PLL x? Need to configure)
    // CH32V003 reference: PLL output = HSI * (PLLMUL). Default PLLMUL might be 2, giving 48MHz.
    // We need to set EXTEND_CTR register for PLL multiplier. Let's do it properly.
    // EXTEND_CTR is at 0x40023800
    volatile uint32_t* EXTEND_CTR = (volatile uint32_t*)0x40023800;
    // PLLMUL[5:0] – we need 48MHz: HSI 24MHz * 2 = 48. Default may already be 2. Set explicitly.
    *EXTEND_CTR = (*EXTEND_CTR & ~0x3Fu) | (2u & 0x3F);  // PLLMUL = 2

    RCC_CTLR |= (1u << 24);  // PLLON
    while (!(RCC_CTLR & (1u << 25)));  // PLLRDY

    // FLASH wait state = 1 for 24 < SYSCLK <= 48
    FLASH_ACTLR = 1;

    // Switch to PLL
    RCC_CFGR0 = (RCC_CFGR0 & ~3u) | (2u << 0);  // SW = 10 (PLL)
    while ((RCC_CFGR0 & (3u << 2)) != (2u << 2));  // SWS = 10
}

// ================== USART ==================
static void usart1_init(void) {
    // Enable GPIOD & USART1 clocks
    RCC_APB2PCENR |= (1u << 5) | (1u << 14);  // IOPD & USART1

    // PD5 = TX (alternate push-pull 50MHz), PD6 = RX (input floating)
    uint32_t tmp = GPIOx_CFGLR(GPIOD_BASE);
    tmp &= ~(0xFFu << 20);  // clear PD5(23:20) & PD6(27:24)
    tmp |= (0xBu << 20);    // PD5: MODE=11, CNF=10 (AF PP)
    tmp |= (0x4u << 24);    // PD6: MODE=00, CNF=01 (float input)
    GPIOx_CFGLR(GPIOD_BASE) = tmp;

    // Baud: 48MHz -> 115200: 48e6 / 16 / 115200 = 26.0416 -> mantissa=26, frac=1
    USART1_BRR = (26u << 4) | 1u;

    // Enable USART, TX, RX
    USART1_CTLR1 = (1u << 13) | (1u << 3) | (1u << 2);

    // Tiny delay
    for (volatile int i = 0; i < 10000; i++);
}

static void usart1_putc(char c) {
    while (!(USART1_STATR & (1u << 7)));  // TXE
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

static bool usart1_getc_timeout(char* c, uint32_t timeout_ms) {
    uint32_t start = micros();
    while (!(USART1_STATR & (1u << 5))) {  // RXNE
        if ((micros() - start) >= timeout_ms * 1000u) return false;
    }
    *c = (char)(USART1_DATAR & 0xFF);
    return true;
}

// ================== Unique ID ==================
static char ducoid_chars[23];  // "DUCOID" + 16 hex + null

static void generate_ducoid(void) {
    uint8_t uid_bytes[12];
    *(uint32_t*)(uid_bytes + 0) = ESIG_UNIID2;
    *(uint32_t*)(uid_bytes + 4) = ESIG_UNIID1;
    *(uint32_t*)(uid_bytes + 8) = ESIG_UNIID3;

    memcpy(ducoid_chars, "DUCOID", 6);
    char* ptr = ducoid_chars + 6;
    for (int i = 4; i < 12; i++) {
        *ptr++ = "0123456789ABCDEF"[uid_bytes[i] >> 4];
        *ptr++ = "0123456789ABCDEF"[uid_bytes[i] & 0xF];
    }
    *ptr = '\0';
}

// ================== SHA-1 core ==================
#define SHA1_HASH_LEN 20
#define sha1_rotl(bits, word)  (((word) << (bits)) | ((word) >> (32 - (bits))))
#define SHA1_ROTL5(word)       sha1_rotl(5, word)
#define SHA1_ROTL30(word)      sha1_rotl(30, word)

typedef struct {
    uint32_t initialWords[10];
    uint32_t tempState[5];
} duco_hash_state_t;

static void hex_to_words(const char* hex, uint32_t* words) {
    for (int w = 0; w < 5; w++) {
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

static const uint32_t kLenWord[6] = {
    0x00000000UL, 0x00000148UL, 0x00000150UL,
    0x00000158UL, 0x00000160UL, 0x00000168UL
};

static void load_block_words(uint32_t* W, const uint32_t* base,
                             const char* nonce, uint8_t nLen) {
    for (int i = 0; i < 10; i++) W[i] = base[i];
    uint32_t d0 = (uint8_t)nonce[0], d1 = (uint8_t)nonce[1],
             d2 = (uint8_t)nonce[2], d3 = (uint8_t)nonce[3],
             d4 = (uint8_t)nonce[4];

    if (nLen <= 5) {
        switch (nLen) {
            case 1: W[10] = (d0<<24)|0x00800000UL; W[11]=0; W[12]=0; break;
            case 2: W[10] = (d0<<24)|(d1<<16)|0x00008000UL; W[11]=0; W[12]=0; break;
            case 3: W[10] = (d0<<24)|(d1<<16)|(d2<<8)|0x00000080UL; W[11]=0; W[12]=0; break;
            case 4: W[10] = (d0<<24)|(d1<<16)|(d2<<8)|d3; W[11]=0x80000000UL; W[12]=0; break;
            case 5: W[10] = (d0<<24)|(d1<<16)|(d2<<8)|d3; W[11]=(d4<<24)|0x00800000UL; W[12]=0; break;
        }
        W[13]=0; W[14]=0; W[15]=kLenWord[nLen];
        return;
    }
    // >5 (generic, rare)
    for (int i = 10; i < 15; i++) W[i] = 0;
    for (int i = 0; i < nLen; i++) {
        uint8_t wi = 10 + (i >> 2);
        uint8_t sh = 24 - ((i & 3) << 3);
        W[wi] |= (uint32_t)(uint8_t)nonce[i] << sh;
    }
    uint8_t wi = 10 + (nLen >> 2);
    uint8_t sh = 24 - ((nLen & 3) << 3);
    W[wi] |= 0x80UL << sh;
    W[15] = (uint32_t)(40 + nLen) << 3;
}

static void duco_hash_init(duco_hash_state_t* hs, const char* prev) {
    uint32_t a=0x67452301UL,b=0xEFCDAB89UL,c=0x98BADCFEUL,
             d=0x10325476UL,e=0xC3D2E1F0UL,t;
    for (int i=0;i<10;i++)
        hs->initialWords[i] = ((uint32_t)(uint8_t)prev[4*i]<<24)
                            | ((uint32_t)(uint8_t)prev[4*i+1]<<16)
                            | ((uint32_t)(uint8_t)prev[4*i+2]<<8)
                            | (uint8_t)prev[4*i+3];
    #define R(idx) t=SHA1_ROTL5(a)+e+((b&(c^d))^d)+hs->initialWords[idx]+0x5A827999UL; \
                    e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t
    R(0);R(1);R(2);R(3);R(4);R(5);R(6);R(7);R(8);R(9);
    #undef R
    hs->tempState[0]=a;hs->tempState[1]=b;hs->tempState[2]=c;
    hs->tempState[3]=d;hs->tempState[4]=e;
}

// ================== SHA-1 70-rounds (unrolled) ==================
#define UNROLL_10_79() do { \
    /* 10..15 */ \
    t=SHA1_ROTL5(a)+((b&(c^d))^d)+e+W[10]+0x5A827999UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    t=SHA1_ROTL5(a)+((b&(c^d))^d)+e+W[11]+0x5A827999UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    t=SHA1_ROTL5(a)+((b&(c^d))^d)+e+W[12]+0x5A827999UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    t=SHA1_ROTL5(a)+((b&(c^d))^d)+e+W[13]+0x5A827999UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    t=SHA1_ROTL5(a)+((b&(c^d))^d)+e+W[14]+0x5A827999UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    t=SHA1_ROTL5(a)+((b&(c^d))^d)+e+W[15]+0x5A827999UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    /* 16..19 */ \
    W[0]=sha1_rotl(1,W[13]^W[8]^W[2]^W[0]); t=SHA1_ROTL5(a)+((b&(c^d))^d)+e+W[0]+0x5A827999UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[1]=sha1_rotl(1,W[14]^W[9]^W[3]^W[1]); t=SHA1_ROTL5(a)+((b&(c^d))^d)+e+W[1]+0x5A827999UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[2]=sha1_rotl(1,W[15]^W[10]^W[4]^W[2]); t=SHA1_ROTL5(a)+((b&(c^d))^d)+e+W[2]+0x5A827999UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[3]=sha1_rotl(1,W[0]^W[11]^W[5]^W[3]); t=SHA1_ROTL5(a)+((b&(c^d))^d)+e+W[3]+0x5A827999UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    /* 20..39 Parity K=0x6ED9EBA1 */ \
    W[4]=sha1_rotl(1,W[1]^W[12]^W[6]^W[4]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[4]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[5]=sha1_rotl(1,W[2]^W[13]^W[7]^W[5]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[5]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[6]=sha1_rotl(1,W[3]^W[14]^W[8]^W[6]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[6]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[7]=sha1_rotl(1,W[4]^W[15]^W[9]^W[7]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[7]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[8]=sha1_rotl(1,W[5]^W[0]^W[10]^W[8]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[8]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[9]=sha1_rotl(1,W[6]^W[1]^W[11]^W[9]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[9]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[10]=sha1_rotl(1,W[7]^W[2]^W[12]^W[10]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[10]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[11]=sha1_rotl(1,W[8]^W[3]^W[13]^W[11]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[11]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[12]=sha1_rotl(1,W[9]^W[4]^W[14]^W[12]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[12]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[13]=sha1_rotl(1,W[10]^W[5]^W[15]^W[13]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[13]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[14]=sha1_rotl(1,W[11]^W[6]^W[0]^W[14]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[14]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[15]=sha1_rotl(1,W[12]^W[7]^W[1]^W[15]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[15]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[0]=sha1_rotl(1,W[13]^W[8]^W[2]^W[0]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[0]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[1]=sha1_rotl(1,W[14]^W[9]^W[3]^W[1]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[1]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[2]=sha1_rotl(1,W[15]^W[10]^W[4]^W[2]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[2]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[3]=sha1_rotl(1,W[0]^W[11]^W[5]^W[3]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[3]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[4]=sha1_rotl(1,W[1]^W[12]^W[6]^W[4]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[4]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[5]=sha1_rotl(1,W[2]^W[13]^W[7]^W[5]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[5]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[6]=sha1_rotl(1,W[3]^W[14]^W[8]^W[6]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[6]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[7]=sha1_rotl(1,W[4]^W[15]^W[9]^W[7]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[7]+0x6ED9EBA1UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    /* 40..59 Majority K=0x8F1BBCDC */ \
    W[8]=sha1_rotl(1,W[5]^W[0]^W[10]^W[8]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[8]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[9]=sha1_rotl(1,W[6]^W[1]^W[11]^W[9]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[9]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[10]=sha1_rotl(1,W[7]^W[2]^W[12]^W[10]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[10]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[11]=sha1_rotl(1,W[8]^W[3]^W[13]^W[11]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[11]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[12]=sha1_rotl(1,W[9]^W[4]^W[14]^W[12]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[12]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[13]=sha1_rotl(1,W[10]^W[5]^W[15]^W[13]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[13]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[14]=sha1_rotl(1,W[11]^W[6]^W[0]^W[14]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[14]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[15]=sha1_rotl(1,W[12]^W[7]^W[1]^W[15]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[15]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[0]=sha1_rotl(1,W[13]^W[8]^W[2]^W[0]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[0]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[1]=sha1_rotl(1,W[14]^W[9]^W[3]^W[1]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[1]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[2]=sha1_rotl(1,W[15]^W[10]^W[4]^W[2]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[2]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[3]=sha1_rotl(1,W[0]^W[11]^W[5]^W[3]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[3]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[4]=sha1_rotl(1,W[1]^W[12]^W[6]^W[4]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[4]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[5]=sha1_rotl(1,W[2]^W[13]^W[7]^W[5]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[5]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[6]=sha1_rotl(1,W[3]^W[14]^W[8]^W[6]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[6]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[7]=sha1_rotl(1,W[4]^W[15]^W[9]^W[7]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[7]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[8]=sha1_rotl(1,W[5]^W[0]^W[10]^W[8]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[8]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[9]=sha1_rotl(1,W[6]^W[1]^W[11]^W[9]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[9]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[10]=sha1_rotl(1,W[7]^W[2]^W[12]^W[10]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[10]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[11]=sha1_rotl(1,W[8]^W[3]^W[13]^W[11]); t=SHA1_ROTL5(a)+((b&c)|(b&d)|(c&d))+e+W[11]+0x8F1BBCDCUL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    /* 60..79 Parity K=0xCA62C1D6 */ \
    W[12]=sha1_rotl(1,W[9]^W[4]^W[14]^W[12]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[12]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[13]=sha1_rotl(1,W[10]^W[5]^W[15]^W[13]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[13]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[14]=sha1_rotl(1,W[11]^W[6]^W[0]^W[14]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[14]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[15]=sha1_rotl(1,W[12]^W[7]^W[1]^W[15]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[15]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[0]=sha1_rotl(1,W[13]^W[8]^W[2]^W[0]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[0]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[1]=sha1_rotl(1,W[14]^W[9]^W[3]^W[1]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[1]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[2]=sha1_rotl(1,W[15]^W[10]^W[4]^W[2]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[2]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[3]=sha1_rotl(1,W[0]^W[11]^W[5]^W[3]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[3]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[4]=sha1_rotl(1,W[1]^W[12]^W[6]^W[4]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[4]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[5]=sha1_rotl(1,W[2]^W[13]^W[7]^W[5]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[5]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[6]=sha1_rotl(1,W[3]^W[14]^W[8]^W[6]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[6]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[7]=sha1_rotl(1,W[4]^W[15]^W[9]^W[7]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[7]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[8]=sha1_rotl(1,W[5]^W[0]^W[10]^W[8]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[8]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[9]=sha1_rotl(1,W[6]^W[1]^W[11]^W[9]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[9]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[10]=sha1_rotl(1,W[7]^W[2]^W[12]^W[10]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[10]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[11]=sha1_rotl(1,W[8]^W[3]^W[13]^W[11]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[11]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[12]=sha1_rotl(1,W[9]^W[4]^W[14]^W[12]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[12]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[13]=sha1_rotl(1,W[10]^W[5]^W[15]^W[13]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[13]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[14]=sha1_rotl(1,W[11]^W[6]^W[0]^W[14]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[14]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
    W[15]=sha1_rotl(1,W[12]^W[7]^W[1]^W[15]); t=SHA1_ROTL5(a)+(b^c^d)+e+W[15]+0xCA62C1D6UL; e=d;d=c;c=SHA1_ROTL30(b);b=a;a=t; \
} while(0)

// ================== Try nonce len=5 (fast path) ==================
static bool duco_hash_try_nonce_len5(const duco_hash_state_t* hs,
                                     const char* nonce,
                                     const uint32_t* targetWords) {
    uint32_t W[16];
    const uint32_t* base = hs->initialWords;
    for (int i = 0; i < 10; i++) W[i] = base[i];

    uint32_t d0 = (uint8_t)nonce[0], d1 = (uint8_t)nonce[1],
             d2 = (uint8_t)nonce[2], d3 = (uint8_t)nonce[3],
             d4 = (uint8_t)nonce[4];
    W[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
    W[11] = (d4 << 24) | 0x00800000UL;
    W[12] = 0; W[13] = 0; W[14] = 0; W[15] = 0x00000168UL;

    uint32_t a = hs->tempState[0], b = hs->tempState[1],
             c = hs->tempState[2], d = hs->tempState[3],
             e = hs->tempState[4], t;

    UNROLL_10_79();

    a += 0x67452301UL; b += 0xEFCDAB89UL; c += 0x98BADCFEUL;
    d += 0x10325476UL; e += 0xC3D2E1F0UL;

    return a == targetWords[0] && b == targetWords[1] && c == targetWords[2]
        && d == targetWords[3] && e == targetWords[4];
}

// ================== Try nonce generic ==================
static bool duco_hash_try_nonce(const duco_hash_state_t* hs,
                                const char* nonce, uint8_t nLen,
                                const uint32_t* targetWords) {
    uint32_t W[16];
    load_block_words(W, hs->initialWords, nonce, nLen);

    uint32_t a = hs->tempState[0], b = hs->tempState[1],
             c = hs->tempState[2], d = hs->tempState[3],
             e = hs->tempState[4], t;

    UNROLL_10_79();

    a += 0x67452301UL; b += 0xEFCDAB89UL; c += 0x98BADCFEUL;
    d += 0x10325476UL; e += 0xC3D2E1F0UL;

    return a == targetWords[0] && b == targetWords[1] && c == targetWords[2]
        && d == targetWords[3] && e == targetWords[4];
}

// ================== Nonce increment (ASCII) ==================
static void increment_nonce_ascii(char* s, uint8_t* len) {
    int8_t i = *len - 1;
    for (; i >= 0; --i) {
        if (s[i] != '9') { s[i]++; return; }
        s[i] = '0';
    }
    for (uint8_t j = *len; j > 0; --j) s[j] = s[j - 1];
    s[0] = '1';
    (*len)++;
    s[*len] = '\0';
}

// ================== Mining loop ==================
typedef uint32_t uintDiff;

static uintDiff ducos1a_mine(const char* prevBlockHash,
                             const uint32_t* targetWords,
                             uintDiff maxNonce,
                             const duco_hash_state_t* hashBase) {
    char nonceStr[12] = "0";
    uint8_t nonceLen = 1;

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

// ================== Main ==================
int main(void) {
    clock_init_48MHz();
    usart1_init();
    generate_ducoid();

    while (1) {
        // Wait for data
        char c;
        if (!usart1_getc_timeout(&c, 10000)) continue;

        // Read last block hash
        char lastBlockHash[41];
        lastBlockHash[0] = c;
        for (int i = 1; i < 40; i++) {
            if (!usart1_getc_timeout(&c, 2000)) goto job_err;
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) goto job_err;
            lastBlockHash[i] = c;
        }
        lastBlockHash[40] = '\0';
        if (!usart1_getc_timeout(&c, 2000) || c != ',') goto job_err;

        // Read new block hash
        char newBlockHash[41];
        for (int i = 0; i < 40; i++) {
            if (!usart1_getc_timeout(&c, 2000)) goto job_err;
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) goto job_err;
            newBlockHash[i] = c;
        }
        newBlockHash[40] = '\0';
        if (!usart1_getc_timeout(&c, 2000) || c != ',') goto job_err;

        // Read difficulty
        uint32_t difficulty = 0;
        uint8_t digits = 0;
        for (;;) {
            if (!usart1_getc_timeout(&c, 2000)) goto job_err;
            if (c == ',') break;
            if (c >= '0' && c <= '9') {
                uint8_t d = c - '0';
                if (difficulty > (0xFFFFFFFFu - d) / 10u) goto job_err;
                difficulty = difficulty * 10u + d;
                if (++digits > 9) goto job_err;
            } else goto job_err;
        }
        if (digits == 0) goto job_err;

        // Skip trailing "0\n"
        if (!usart1_getc_timeout(&c, 2000) || c != '0') goto job_err;
        if (!usart1_getc_timeout(&c, 2000)) goto job_err;
        if (c == '\r') {
            if (!usart1_getc_timeout(&c, 2000)) goto job_err;
        }
        if (c != '\n') goto job_err;

        // Flush any extra
        while (usart1_getc_timeout(&c, 0)) {}

        // Mining
        uint32_t targetWords[5];
        hex_to_words(newBlockHash, targetWords);

        uintDiff maxNonce = difficulty * 100 + 1;
        if (difficulty > 655) maxNonce = 0;

        duco_hash_state_t hasher;
        duco_hash_init(&hasher, lastBlockHash);

        uint32_t start_us = micros();
        uintDiff result = ducos1a_mine(lastBlockHash, targetWords, maxNonce, &hasher);
        uint32_t elapsed_us = micros() - start_us;

        // Output result
        usart1_print_bin_u32(result);
        usart1_putc(',');
        usart1_print_bin_u32(elapsed_us);
        usart1_putc(',');
        usart1_print(ducoid_chars);
        usart1_putc('\n');

        continue;
    job_err:
        while (usart1_getc_timeout(&c, 0)) {}
        usart1_print("ERR\n");
    }
    return 0;
}
