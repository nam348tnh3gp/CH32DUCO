/*
 * Duino-Coin Miner for CH32V003 (RISC-V RV32EC)
 * Bare-metal NoneOS – USART1, 48MHz PLL
 * NO explicit register usage – compiler handles everything
 */

#include <stdint.h>
#include <stdbool.h>
// Thêm dòng này vào đầu main.c, sau #include:
volatile uint32_t tick_ms = 0;

// ================== Minimal libc (no register asm) ==================
static void* simple_memcpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n) { *d++ = *s++; n--; }
    return dst;
}

static void* simple_memset(void* p, int c, uint32_t n) {
    uint8_t* ptr = (uint8_t*)p;
    while (n) { *ptr++ = (uint8_t)c; n--; }
    return p;
}

// ================== Hardware Registers ==================
#define RCC_BASE             0x40021000u
#define RCC_CTLR             (*(volatile uint32_t*)(RCC_BASE + 0x00))
#define RCC_CFGR0            (*(volatile uint32_t*)(RCC_BASE + 0x04))
#define RCC_APB2PCENR        (*(volatile uint32_t*)(RCC_BASE + 0x18))

#define GPIOD_BASE           0x40011400u
#define GPIOD_CFGLR          (*(volatile uint32_t*)(GPIOD_BASE + 0x00))

#define USART1_BASE          0x40013800u
#define USART1_STATR         (*(volatile uint32_t*)(USART1_BASE + 0x00))
#define USART1_DATAR         (*(volatile uint32_t*)(USART1_BASE + 0x04))
#define USART1_BRR           (*(volatile uint32_t*)(USART1_BASE + 0x08))
#define USART1_CTLR1         (*(volatile uint32_t*)(USART1_BASE + 0x0C))

#define ESIG_BASE            0x1FFFF7E0u
#define ESIG_UNIID1          (*(volatile uint32_t*)(ESIG_BASE + 0x08))
#define ESIG_UNIID2          (*(volatile uint32_t*)(ESIG_BASE + 0x0C))
#define ESIG_UNIID3          (*(volatile uint32_t*)(ESIG_BASE + 0x10))

#define FLASH_ACTLR          (*(volatile uint32_t*)0x40022000)
#define EXTEND_CTR           (*(volatile uint32_t*)0x40023800)

// ================== Clock (SysTick for timing) ==================
#define SYSTICK_BASE         0xE000F000u
#define STK_CTLR             (*(volatile uint32_t*)(SYSTICK_BASE + 0x00))
#define STK_CNTL             (*(volatile uint32_t*)(SYSTICK_BASE + 0x08))
#define STK_CMPLR            (*(volatile uint32_t*)(SYSTICK_BASE + 0x10))

static volatile uint32_t tick_ms = 0;

// SysTick interrupt handler (use it if enabled, else poll)
void SysTick_Handler(void) {
    tick_ms++;
    // Clear flag by reading STK_CTLR? No, just let it roll.
}

static uint32_t millis(void) {
    return tick_ms;
}

static uint32_t micros(void) {
    // Rough: SysTick @ 1MHz -> 1us per tick
    return STK_CNTL;
}

static void delay_ms(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) { /* spin */ }
}

static void clock_init(void) {
    // Enable HSI
    RCC_CTLR |= 1u;
    while (!(RCC_CTLR & 2u)) {}

    // PLL x2 (24MHz -> 48MHz)
    EXTEND_CTR = (EXTEND_CTR & ~0x3Fu) | 2u;
    RCC_CTLR |= (1u << 24);
    while (!(RCC_CTLR & (1u << 25))) {}

    // Flash 1 wait state for 48MHz
    FLASH_ACTLR = 1;

    // Switch system clock to PLL
    RCC_CFGR0 = (RCC_CFGR0 & ~3u) | 2u;
    while ((RCC_CFGR0 & 0xCu) != 0x8u) {}

    // Configure SysTick @ 1MHz (48MHz / 48)
    STK_CMPLR = 47;  // counting from 0..47 = 48 ticks = 1us? Actually want 1ms = 48000
    // Let's use 1ms tick: 48MHz / 48000 = 1000Hz
    STK_CMPLR = 47999;
    STK_CNTL = 0;
    STK_CTLR = 0x7;  // Enable, use HCLK, interrupt enable, auto-reload
}

// ================== USART1 ==================
static void usart1_init(void) {
    RCC_APB2PCENR |= (1u << 5) | (1u << 14);  // GPIOD + USART1

    // PD5 = TX (AF push-pull), PD6 = RX (input pull-up)
    uint32_t cfg = GPIOD_CFGLR;
    cfg &= ~(0xFFu << 20);
    cfg |= (0xBu << 20) | (0x8u << 24);  // PD6 pull-up input
    GPIOD_CFGLR = cfg;

    // 115200 @ 48MHz
    USART1_BRR = (26u << 4) | 1u;
    USART1_CTLR1 = (1u << 13) | (1u << 3) | (1u << 2);  // UE | TE | RE

    for (volatile uint32_t i = 0; i < 10000; i++) {}
}

static void usart_putc(char c) {
    while (!(USART1_STATR & 0x80)) {}
    USART1_DATAR = (uint32_t)(uint8_t)c;
}

static void usart_print(const char* s) {
    while (*s) usart_putc(*s++);
}

static void usart_print_bin(uint32_t val) {
    if (val == 0) { usart_putc('0'); return; }
    uint32_t mask = 0x80000000u;
    while (mask && !(val & mask)) mask >>= 1;
    while (mask) {
        usart_putc((val & mask) ? '1' : '0');
        mask >>= 1;
    }
}

static bool usart_read_timeout(char* c, uint32_t timeout_ms) {
    uint32_t start = millis();
    while (!(USART1_STATR & 0x20)) {
        if (millis() - start >= timeout_ms) return false;
    }
    *c = (char)(USART1_DATAR & 0xFF);
    return true;
}

// ================== Unique ID ==================
static char ducoid[23];

static void generate_ducoid(void) {
    uint8_t bytes[12];
    *(uint32_t*)(bytes + 0) = ESIG_UNIID2;
    *(uint32_t*)(bytes + 4) = ESIG_UNIID1;
    *(uint32_t*)(bytes + 8) = ESIG_UNIID3;

    simple_memcpy(ducoid, "DUCOID", 6);
    char* p = ducoid + 6;
    for (uint32_t i = 4; i < 12; i++) {
        uint8_t v = bytes[i];
        *p++ = "0123456789ABCDEF"[v >> 4];
        *p++ = "0123456789ABCDEF"[v & 0x0F];
    }
    *p = '\0';
}

// ================== SHA-1 ==================
#define ROTL(x,n) (((x)<<(n))|((x)>>(32-(n))))

typedef struct {
    uint32_t w[16];
    uint32_t h[5];
} SHA1_CTX;

static void sha1_init(SHA1_CTX* ctx) {
    ctx->h[0] = 0x67452301u;
    ctx->h[1] = 0xEFCDAB89u;
    ctx->h[2] = 0x98BADCFEu;
    ctx->h[3] = 0x10325476u;
    ctx->h[4] = 0xC3D2E1F0u;
}

static void sha1_transform(SHA1_CTX* ctx) {
    uint32_t* w = ctx->w;
    for (uint32_t t = 16; t < 80; t++) {
        w[t & 15] = ROTL(1, w[(t-3)&15] ^ w[(t-8)&15] ^ w[(t-14)&15] ^ w[(t-16)&15]);
    }
    uint32_t a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3], e = ctx->h[4];
    for (uint32_t t = 0; t < 80; t++) {
        uint32_t f, k;
        if (t < 20)          { f = (b & c) | (~b & d);           k = 0x5A827999u; }
        else if (t < 40)     { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
        else if (t < 60)     { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDCu; }
        else                 { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }
        uint32_t tmp = ROTL(5, a) + f + e + w[t & 15] + k;
        e = d; d = c; c = ROTL(30, b); b = a; a = tmp;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d; ctx->h[4] += e;
}

static void hex_to_words(const char* hex, uint32_t* words) {
    for (uint32_t w = 0; w < 5; w++) {
        const char* s = hex + w * 8;
        uint32_t b0 = (s[0] <= '9') ? (s[0] - '0') : (s[0] - 'a' + 10);
        uint32_t b1 = (s[1] <= '9') ? (s[1] - '0') : (s[1] - 'a' + 10);
        uint32_t b2 = (s[2] <= '9') ? (s[2] - '0') : (s[2] - 'a' + 10);
        uint32_t b3 = (s[3] <= '9') ? (s[3] - '0') : (s[3] - 'a' + 10);
        b0 = (b0 << 4) | ((s[4] <= '9') ? (s[4] - '0') : (s[4] - 'a' + 10));
        b1 = (b1 << 4) | ((s[5] <= '9') ? (s[5] - '0') : (s[5] - 'a' + 10));
        b2 = (b2 << 4) | ((s[6] <= '9') ? (s[6] - '0') : (s[6] - 'a' + 10));
        b3 = (b3 << 4) | ((s[7] <= '9') ? (s[7] - '0') : (s[7] - 'a' + 10));
        words[w] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    }
}

static bool duco_hash_one(const char* prev, const char* nonce, uint8_t nLen,
                          const uint32_t* target) {
    SHA1_CTX ctx;
    sha1_init(&ctx);
    for (uint32_t i = 0; i < 16; i++) ctx.w[i] = 0;

    for (uint32_t i = 0; i < 10; i++) {
        ctx.w[i] = ((uint32_t)(uint8_t)prev[4*i]   << 24)
                 | ((uint32_t)(uint8_t)prev[4*i+1] << 16)
                 | ((uint32_t)(uint8_t)prev[4*i+2] <<  8)
                 | ((uint32_t)(uint8_t)prev[4*i+3]);
    }

    if (nLen <= 5) {
        uint32_t d0 = (uint8_t)nonce[0], d1 = (uint8_t)nonce[1],
                 d2 = (uint8_t)nonce[2], d3 = (uint8_t)nonce[3],
                 d4 = (uint8_t)nonce[4];
        switch (nLen) {
            case 1: ctx.w[10] = (d0 << 24) | 0x00800000u; break;
            case 2: ctx.w[10] = (d0 << 24) | (d1 << 16) | 0x00008000u; break;
            case 3: ctx.w[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | 0x00000080u; break;
            case 4: ctx.w[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
                    ctx.w[11] = 0x80000000u; break;
            case 5: ctx.w[10] = (d0 << 24) | (d1 << 16) | (d2 << 8) | d3;
                    ctx.w[11] = (d4 << 24) | 0x00800000u; break;
        }
        static const uint32_t kLen[6] = {0, 0x148, 0x150, 0x158, 0x160, 0x168};
        ctx.w[15] = kLen[nLen];
    } else {
        for (uint32_t i = 0; i < nLen; i++) {
            uint32_t wi = 10 + (i >> 2);
            uint32_t sh = 24 - ((i & 3) << 3);
            ctx.w[wi] |= (uint32_t)(uint8_t)nonce[i] << sh;
        }
        uint32_t wi = 10 + (nLen >> 2);
        uint32_t sh = 24 - ((nLen & 3) << 3);
        ctx.w[wi] |= 0x80u << sh;
        ctx.w[15] = (uint32_t)(40 + nLen) << 3;
    }

    sha1_transform(&ctx);
    return ctx.h[0] == target[0] && ctx.h[1] == target[1] &&
           ctx.h[2] == target[2] && ctx.h[3] == target[3] &&
           ctx.h[4] == target[4];
}

// ================== Mining ==================
typedef uint32_t uintDiff;

static void inc_nonce(char* s, uint8_t* len) {
    int8_t i = (int8_t)(*len) - 1;
    for (; i >= 0; i--) {
        if (s[i] != '9') { s[i]++; return; }
        s[i] = '0';
    }
    for (uint8_t j = *len; j > 0; j--) s[j] = s[j - 1];
    s[0] = '1';
    (*len)++;
    s[*len] = '\0';
}

static uintDiff ducos1a_mine(const char* prev, const uint32_t* target,
                             uintDiff maxNonce) {
    char nonce[12] = "0";
    uint8_t nLen = 1;
    for (uintDiff n = 0; n < maxNonce; n++) {
        if (duco_hash_one(prev, nonce, nLen, target)) return n;
        inc_nonce(nonce, &nLen);
    }
    return 0;
}

// ================== Main ==================
int main(void) {
    clock_init();
    usart1_init();
    generate_ducoid();

    for (;;) {
        char c;
        if (!usart_read_timeout(&c, 10000)) continue;

        char last[41];
        last[0] = c;
        for (int i = 1; i < 40; i++) {
            if (!usart_read_timeout(&c, 2000)) goto err;
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) goto err;
            last[i] = c;
        }
        last[40] = '\0';
        if (!usart_read_timeout(&c, 2000) || c != ',') goto err;

        char newh[41];
        for (int i = 0; i < 40; i++) {
            if (!usart_read_timeout(&c, 2000)) goto err;
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) goto err;
            newh[i] = c;
        }
        newh[40] = '\0';
        if (!usart_read_timeout(&c, 2000) || c != ',') goto err;

        uint32_t diff = 0;
        uint8_t dg = 0;
        for (;;) {
            if (!usart_read_timeout(&c, 2000)) goto err;
            if (c == ',') break;
            if (c >= '0' && c <= '9') {
                uint8_t dv = c - '0';
                if (diff > (0xFFFFFFFFu - dv) / 10u) goto err;
                diff = diff * 10u + dv;
                if (++dg > 9) goto err;
            } else goto err;
        }
        if (!dg) goto err;

        if (!usart_read_timeout(&c, 2000) || c != '0') goto err;
        if (!usart_read_timeout(&c, 2000)) goto err;
        if (c == '\r') { if (!usart_read_timeout(&c, 2000)) goto err; }
        if (c != '\n') goto err;

        while (usart_read_timeout(&c, 0)) {}

        uint32_t target[5];
        hex_to_words(newh, target);
        uintDiff maxN = diff * 100u + 1u;
        if (diff > 655u) maxN = 0;

        uint32_t st = micros();
        uintDiff res = ducos1a_mine(last, target, maxN);
        uint32_t el = micros() - st;

        usart_print_bin(res);
        usart_putc(',');
        usart_print_bin(el);
        usart_putc(',');
        usart_print(ducoid);
        usart_putc('\n');
        continue;

    err:
        while (usart_read_timeout(&c, 0)) {}
        usart_print("ERR\n");
    }
    return 0;
}
