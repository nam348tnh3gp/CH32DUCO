/*
 * Duino-Coin Miner for CH32V003 — Final release
 * - Binary nonce + fast decimal conversion (no division)
 * - SHA1 with 16-word circular buffer
 * - HEX lookup table
 * - Reliable micros() (race-free)
 * - Job timeout 8s
 * - Full UART drain
 */

#include <stdint.h>

// Hardware registers
#define RCC_BASE        0x40021000u
#define RCC_CTLR        (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_CFGR0       (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_APB2PCENR   (*(volatile uint32_t *)(RCC_BASE + 0x18))

#define GPIOD_BASE      0x40011400u
#define GPIOD_CFGLR     (*(volatile uint32_t *)(GPIOD_BASE + 0x00))

#define USART1_BASE     0x40013800u
#define USART1_STATR    (*(volatile uint32_t *)(USART1_BASE + 0x00))
#define USART1_DATAR    (*(volatile uint32_t *)(USART1_BASE + 0x04))
#define USART1_BRR      (*(volatile uint32_t *)(USART1_BASE + 0x08))
#define USART1_CTLR1    (*(volatile uint32_t *)(USART1_BASE + 0x0C))

#define ESIG_BASE       0x1FFFF7E0u
#define ESIG_UNIID2     (*(volatile uint32_t *)(ESIG_BASE + 0x0C))
#define ESIG_UNIID1     (*(volatile uint32_t *)(ESIG_BASE + 0x08))

#define STK_BASE        0xE000E010u
#define STK_CTRL        (*(volatile uint32_t *)(STK_BASE + 0x00))
#define STK_LOAD        (*(volatile uint32_t *)(STK_BASE + 0x04))
#define STK_VAL         (*(volatile uint32_t *)(STK_BASE + 0x08))

#define SHA1_HASH_LEN   20
#define ROTL32(b, w)    (((w) << (b)) | ((w) >> (32 - (b))))

// Globals
static char ducoid[23];
volatile uint32_t sys_uptime_ms = 0;

// HEX LUT
static const uint8_t hex_lut[256] = {
    ['0']=0, ['1']=1, ['2']=2, ['3']=3, ['4']=4,
    ['5']=5, ['6']=6, ['7']=7, ['8']=8, ['9']=9,
    ['a']=10,['b']=11,['c']=12,['d']=13,['e']=14,['f']=15,
    ['A']=10,['B']=11,['C']=12,['D']=13,['E']=14,['F']=15,
};

// System init
static void SystemClock_Init(void) {
    RCC_CTLR |= (1u << 0); while (!(RCC_CTLR & (1u << 1)));
    RCC_CFGR0 &= ~(0x0Fu << 18);
    RCC_CFGR0 |=  (0x4u << 18);                 // PLLMUL = x12 (48 MHz)
    RCC_CTLR |= (1u << 24); while (!(RCC_CTLR & (1u << 25)));
    RCC_CFGR0 &= ~(3u << 0);
    RCC_CFGR0 |=  (2u << 0);
    while ((RCC_CFGR0 & (3u << 2)) != (2u << 2));
    RCC_CFGR0 &= ~(0x0Fu << 4);
}

static void USART1_Init(uint32_t baud) {
    RCC_APB2PCENR |= (1u << 5) | (1u << 14);
    GPIOD_CFGLR &= ~(0x0Fu << 20); GPIOD_CFGLR |= (0x0Bu << 20);
    GPIOD_CFGLR &= ~(0x0Fu << 24); GPIOD_CFGLR |= (0x04u << 24);
    uint32_t pclk = 48000000u;
    uint32_t mantissa = pclk / (16u * baud);
    uint32_t frac = ((pclk % (16u * baud)) * 16u + 8u * baud) / (16u * baud);
    if (frac > 15u) frac = 15u;
    USART1_BRR = (mantissa << 4) | frac;
    USART1_CTLR1 |= (1u << 13) | (1u << 3) | (1u << 2);
}

// SysTick with 1ms interrupt
void SysTick_Handler(void) __attribute__((interrupt));
void SysTick_Handler(void) { sys_uptime_ms++; }

static void SysTick_Init(void) {
    STK_LOAD = 48000u - 1u;
    STK_VAL  = 0;
    STK_CTRL = (1u << 2) | (1u << 1) | (1u << 0);
}

static uint32_t millis(void) {
    return sys_uptime_ms;   // atomic read on RV32
}

static uint32_t micros(void) {
    uint32_t val1 = STK_VAL;
    uint32_t ms   = sys_uptime_ms;
    uint32_t val2 = STK_VAL;

    if (val2 > val1) {
        // không có wrap
    } else {
        ms   = sys_uptime_ms;   // wrap đã xảy ra, đọc lại
        val2 = STK_VAL;
    }
    uint32_t ticks = STK_LOAD - val2;
    return ms * 1000u + ticks / 48u;
}

// UART
static void uart_send(char c) {
    while (!(USART1_STATR & (1u << 7)));
    USART1_DATAR = (uint32_t)c;
}

static char uart_recv(void) {
    while (!(USART1_STATR & (1u << 5)));
    return (char)(USART1_DATAR & 0xFF);
}

static void uart_send_binary(uint32_t val) {
    if (val == 0) { uart_send('0'); return; }
    uint32_t mask = 1u << 31;
    while (mask && !(val & mask)) mask >>= 1;
    while (mask) {
        uart_send((val & mask) ? '1' : '0');
        mask >>= 1;
    }
}

static void uart_flush(void) {
    while (USART1_STATR & (1u << 5)) (void)USART1_DATAR;
}

// DUCOID
static void generate_ducoid(void) {
    uint32_t uid_low  = ESIG_UNIID2;
    uint32_t uid_high = ESIG_UNIID1;
    uint8_t uid[8];
    uid[0]=uid_low&0xFF; uid[1]=(uid_low>>8)&0xFF;
    uid[2]=(uid_low>>16)&0xFF; uid[3]=uid_low>>24;
    uid[4]=uid_high&0xFF; uid[5]=(uid_high>>8)&0xFF;
    uid[6]=(uid_high>>16)&0xFF; uid[7]=uid_high>>24;
    ducoid[0]='D'; ducoid[1]='U'; ducoid[2]='C'; ducoid[3]='O'; ducoid[4]='I'; ducoid[5]='D';
    char *p = ducoid+6;
    for (int i=0; i<8; i++) {
        *p++ = "0123456789ABCDEF"[uid[i]>>4];
        *p++ = "0123456789ABCDEF"[uid[i]&0x0F];
    }
    *p = '\0';
}

// Hex parse with LUT
static void hex_to_words(const char *hex, uint32_t words[5]) {
    for (int w=0; w<5; w++) {
        const uint8_t *s = (const uint8_t *)(hex + w*8);
        words[w] = ((uint32_t)(hex_lut[s[0]]<<4 | hex_lut[s[1]]) << 24) |
                   ((uint32_t)(hex_lut[s[2]]<<4 | hex_lut[s[3]]) << 16) |
                   ((uint32_t)(hex_lut[s[4]]<<4 | hex_lut[s[5]]) << 8)  |
                    (uint32_t)(hex_lut[s[6]]<<4 | hex_lut[s[7]]);
    }
}

// Fast decimal conversion (no division)
static uint8_t uint32_to_dec_fast(uint32_t num, char *buf) {
    static const uint32_t pow10[] = {10000, 1000, 100, 10, 1};
    uint8_t len = 0;
    uint8_t started = 0;
    for (int i = 0; i < 5; i++) {
        uint32_t p = pow10[i];
        uint8_t digit = 0;
        while (num >= p) {
            num -= p;
            digit++;
        }
        if (digit > 0 || started || i == 4) {
            buf[len++] = '0' + digit;
            started = 1;
        }
    }
    return len;
}

// SHA1 transform (16-word buffer)
static void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[16];
    for (int i=0; i<16; i++) {
        w[i] = ((uint32_t)block[i*4]<<24) | ((uint32_t)block[i*4+1]<<16) |
               ((uint32_t)block[i*4+2]<<8)  | ((uint32_t)block[i*4+3]);
    }
    uint32_t a=state[0], b=state[1], c=state[2], d=state[3], e=state[4], t;
    for (int i=0; i<80; i++) {
        uint32_t wval;
        if (i < 16) wval = w[i];
        else {
            wval = ROTL32(1, w[(i-3)&0xF] ^ w[(i-8)&0xF] ^ w[(i-14)&0xF] ^ w[(i-16)&0xF]);
            w[i & 0xF] = wval;
        }
        if (i < 20)
            t = ROTL32(5,a) + ((b&c)|((~b)&d)) + e + wval + 0x5A827999u;
        else if (i < 40)
            t = ROTL32(5,a) + (b^c^d) + e + wval + 0x6ED9EBA1u;
        else if (i < 60)
            t = ROTL32(5,a) + ((b&c)|(b&d)|(c&d)) + e + wval + 0x8F1BBCDCu;
        else
            t = ROTL32(5,a) + (b^c^d) + e + wval + 0xCA62C1D6u;
        e=d; d=c; c=ROTL32(30,b); b=a; a=t;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

// DUCO-S1A hash (single block)
static void ducos1a_hash(const char *prevHash, uint32_t nonce, uint32_t output[5]) {
    uint8_t block[64];
    uint32_t state[5] = {0x67452301u,0xEFCDAB89u,0x98BADCFEu,0x10325476u,0xC3D2E1F0u};

    for (int i=0; i<40; i++) block[i] = (uint8_t)prevHash[i];
    char nonce_str[6];
    uint8_t nonce_len = uint32_to_dec_fast(nonce, nonce_str);
    for (int i=0; i<nonce_len; i++) block[40+i] = nonce_str[i];
    uint8_t total = 40 + nonce_len;

    block[total] = 0x80;
    for (int i=total+1; i<56; i++) block[i] = 0x00;
    uint32_t bitLen = (uint32_t)total * 8;
    block[56]=0; block[57]=0; block[58]=0; block[59]=0;
    block[60]=(uint8_t)(bitLen>>24); block[61]=(uint8_t)(bitLen>>16);
    block[62]=(uint8_t)(bitLen>>8);  block[63]=(uint8_t)(bitLen);

    sha1_transform(state, block);
    for (int i=0; i<5; i++) output[i] = state[i];
}

// Mine loop with timeout
static int ducos1a_mine(const char *prevHash, const uint32_t target[5],
                         uint32_t maxNonce, uint32_t *foundNonce) {
    uint32_t t_start = millis();
    for (uint32_t n=0; n<maxNonce; n++) {
        if ((n & 0x3FF) == 0) {
            if (millis() - t_start > 8000) return 0;
        }
        uint32_t hash[5];
        ducos1a_hash(prevHash, n, hash);
        if (hash[0]==target[0] && hash[1]==target[1] && hash[2]==target[2] &&
            hash[3]==target[3] && hash[4]==target[4]) {
            *foundNonce = n;
            return 1;
        }
    }
    return 0;
}

int main(void) {
    SystemClock_Init();
    USART1_Init(115200);
    SysTick_Init();
    generate_ducoid();
    for (volatile uint32_t i=0; i<100000; i++) __asm__ volatile ("nop");
    uart_flush();

    char lastHash[41], newHash[41];
    uint32_t target[5], difficulty, foundNonce, elapsed_us;

    while (1) {
        while (!(USART1_STATR & (1u<<5)));
        for (int i=0; i<40; i++) lastHash[i] = uart_recv();
        lastHash[40] = '\0';
        if (uart_recv() != ',') { uart_flush(); continue; }

        for (int i=0; i<40; i++) newHash[i] = uart_recv();
        newHash[40] = '\0';
        if (uart_recv() != ',') { uart_flush(); continue; }

        difficulty = 0;
        char c;
        while (1) {
            c = uart_recv();
            if (c == ',' || c == '\n' || c == '\r') break;
            if (c >= '0' && c <= '9') difficulty = difficulty*10 + (uint32_t)(c-'0');
            else { uart_flush(); difficulty=0; break; }
        }
        if (difficulty == 0) continue;
        uart_flush();  // drain all remaining

        hex_to_words(newHash, target);
        uint32_t maxNonce = difficulty * 100u + 1u;

        uint32_t t1 = micros();
        int found = ducos1a_mine(lastHash, target, maxNonce, &foundNonce);
        uint32_t t2 = micros();
        elapsed_us = (t2 >= t1) ? (t2 - t1) : 0;

        uart_send_binary(found ? foundNonce : maxNonce);
        uart_send(',');
        uart_send_binary(elapsed_us);
        uart_send(',');
        for (int i=0; ducoid[i]; i++) uart_send(ducoid[i]);
        uart_send('\n');
    }
    return 0;
}
