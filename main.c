/*
 * Duino-Coin Miner for CH32V003 (RISC-V RV32EC)
 * Bare-metal NoneOS – USART1, 48MHz PLL
 * All-in-one file, no external libs
 */

#include <stdint.h>
#include <stdbool.h>

// ================== Minimal libc replacements ==================
__attribute__((used))
static void* my_memcpy(void* dst, const void* src, unsigned n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}

__attribute__((used))
static void* my_memmove(void* dst, const void* src, unsigned n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d <= s) while (n--) *d++ = *s++;
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

__attribute__((used))
static void* my_memset(void* s, int c, unsigned n) {
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

// ================== Register definitions ==================
#define RCC_BASE             0x40021000u
#define RCC_CTLR             (*(volatile uint32_t*)(RCC_BASE + 0x00))
#define RCC_CFGR0            (*(volatile uint32_t*)(RCC_BASE + 0x04))
#define RCC_APB2PCENR        (*(volatile uint32_t*)(RCC_BASE + 0x18))

#define GPIOD_BASE           0x40011400u
#define GPIOx_CFGLR(x)       (*(volatile uint32_t*)((x) + 0x00))

#define USART1_BASE          0x40013800u
#define USART1_STATR         (*(volatile uint32_t*)(USART1_BASE + 0x00))
#define USART1_DATAR         (*(volatile uint32_t*)(USART1_BASE + 0x04))
#define USART1_BRR           (*(volatile uint32_t*)(USART1_BASE + 0x08))
#define USART1_CTLR1         (*(volatile uint32_t*)(USART1_BASE + 0x0C))

#define ESIG_UNIID1          (*(volatile uint32_t*)(0x1FFFF7E8))
#define ESIG_UNIID2          (*(volatile uint32_t*)(0x1FFFF7EC))
#define ESIG_UNIID3          (*(volatile uint32_t*)(0x1FFFF7F0))

#define FLASH_ACTLR          (*(volatile uint32_t*)0x40022000)
#define EXTEND_CTR           (*(volatile uint32_t*)0x40023800)

#define HCLK_MHZ             48u

// ================== Clock ==================
static inline uint32_t micros(void) {
    uint32_t cyc;
    asm volatile ("rdcycle %0" : "=r"(cyc));
    return cyc / HCLK_MHZ;
}

static void clock_init(void) {
    RCC_CTLR |= 1u;
    while (!(RCC_CTLR & 2u));
    EXTEND_CTR = (EXTEND_CTR & ~0x3Fu) | 2u;  // PLLMUL = 2 (24*2=48)
    RCC_CTLR |= (1u << 24);
    while (!(RCC_CTLR & (1u << 25)));
    FLASH_ACTLR = 1;
    RCC_CFGR0 = (RCC_CFGR0 & ~3u) | (2u);
    while ((RCC_CFGR0 & 0xCu) != 0x8u);
}

// ================== USART ==================
static void usart1_init(void) {
    RCC_APB2PCENR |= (1u << 5) | (1u << 14);
    uint32_t tmp = GPIOx_CFGLR(GPIOD_BASE);
    tmp &= ~(0xFFu << 20);
    tmp |= (0xBu << 20) | (0x4u << 24);
    GPIOx_CFGLR(GPIOD_BASE) = tmp;
    USART1_BRR = (26u << 4) | 1u;  // 115200 @ 48MHz
    USART1_CTLR1 = (1u << 13) | (1u << 3) | (1u << 2);
    for (volatile int i=0; i<10000; i++);
}

static void usart1_putc(char c) {
    while (!(USART1_STATR & 0x80));
    USART1_DATAR = c;
}
static void usart1_print(const char* s) {
    while (*s) usart1_putc(*s++);
}
static void usart1_print_bin_u32(uint32_t n) {
    if (!n) { usart1_putc('0'); return; }
    uint32_t m = 1u<<31;
    while (m && !(n&m)) m>>=1;
    for (; m; m>>=1) usart1_putc((n&m)?'1':'0');
}
static bool usart1_getc_timeout(char* c, uint32_t ms) {
    uint32_t st = micros();
    while (!(USART1_STATR & 0x20))
        if (micros()-st >= ms*1000u) return false;
    *c = USART1_DATAR & 0xFF;
    return true;
}

// ================== Unique ID ==================
static char ducoid_chars[23];
static void generate_ducoid(void) {
    uint8_t uid[12];
    *(uint32_t*)(uid+0) = ESIG_UNIID2;
    *(uint32_t*)(uid+4) = ESIG_UNIID1;
    *(uint32_t*)(uid+8) = ESIG_UNIID3;
    my_memcpy(ducoid_chars, "DUCOID", 6);
    char* p = ducoid_chars + 6;
    for (int i=4; i<12; i++) {
        *p++ = "0123456789ABCDEF"[uid[i]>>4];
        *p++ = "0123456789ABCDEF"[uid[i]&0xF];
    }
    *p = '\0';
}

// ================== SHA-1 core (compact – only essential rounds) ==================
// To save flash, we use a LOOP instead of full unrolling for rounds 10..79

#define sha1_rotl(b,w) (((w)<<(b))|((w)>>(32-(b))))

typedef struct {
    uint32_t W[16];
    uint32_t state[5];   // a,b,c,d,e
} sha1_context_t;

static void sha1_transform(sha1_context_t* ctx) {
    uint32_t *W = ctx->W;
    // Expand W[16..79]
    for (int t=16; t<80; t++) {
        W[t&15] = sha1_rotl(1, W[(t-3)&15] ^ W[(t-8)&15] ^ W[(t-14)&15] ^ W[(t-16)&15]);
    }
    uint32_t a=ctx->state[0], b=ctx->state[1], c=ctx->state[2],
             d=ctx->state[3], e=ctx->state[4], tmp, f, k;
    for (int t=0; t<80; t++) {
        if (t<20)      { f = (b&c)|(~b&d);          k=0x5A827999u; }
        else if (t<40) { f = b^c^d;                 k=0x6ED9EBA1u; }
        else if (t<60) { f = (b&c)|(b&d)|(c&d);     k=0x8F1BBCDCu; }
        else           { f = b^c^d;                 k=0xCA62C1D6u; }
        tmp = sha1_rotl(5,a) + f + e + W[t&15] + k;
        e=d; d=c; c=sha1_rotl(30,b); b=a; a=tmp;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c;
    ctx->state[3]+=d; ctx->state[4]+=e;
}

static void sha1_init(sha1_context_t* ctx) {
    ctx->state[0]=0x67452301u; ctx->state[1]=0xEFCDAB89u;
    ctx->state[2]=0x98BADCFEu; ctx->state[3]=0x10325476u;
    ctx->state[4]=0xC3D2E1F0u;
}

// ================== DUCO-S1A specific ==================
typedef struct {
    uint32_t baseState[5];
} duco_hasher_t;

static void duco_hash_init(duco_hasher_t* hs, const char* prev) {
    sha1_context_t ctx;
    sha1_init(&ctx);
    // Load 10 words from prev hash (40 hex chars)
    for (int i=0; i<10; i++) {
        ctx.W[i] = ((uint32_t)(uint8_t)prev[4*i]<<24)
                 | ((uint32_t)(uint8_t)prev[4*i+1]<<16)
                 | ((uint32_t)(uint8_t)prev[4*i+2]<<8)
                 | (uint8_t)prev[4*i+3];
    }
    ctx.W[10]=ctx.W[11]=ctx.W[12]=ctx.W[13]=ctx.W[14]=ctx.W[15]=0;
    sha1_transform(&ctx);  // This computes the first 16 rounds AND expands & runs 80 rounds?
    // Actually, the DUCO protocol runs SHA-1 on block0 first (80 rounds), then saves state.
    // But the init function above already did 80 rounds on the INITIAL block? No – duco_hash_init
    // runs 10 rounds of the first 16-W block. Let me implement EXACTLY the original protocol.
    // For space reasons, I'll implement a precise DUCO-S1A init + try_nonce below.
    // (Rewriting compact but correct)
}

// Wait – the original protocol is:
// 1) hash_init: take prevBlockHash (40 hex), load as first 10 W's, set W[10..15]=0,
//    run SHA-1 for 80 rounds to get the "midstate" (a,b,c,d,e after round 10? No, after full 80?)
// Actually the DUCO miner runs 10 rounds of INIT (unrolled) and saves state. Then for each nonce,
// it loads the block words with the nonce, and runs rounds 10..79 of SHA-1.
//
// To save space, I'll implement a COMPLETE SHA-1 from scratch each time (no midstate).
// It's slower but MUCH smaller and fits in flash.

static void hex_to_words(const char* hex, uint32_t* words) {
    for (int w=0; w<5; w++) {
        const char* s = hex + w*8;
        uint32_t b0 = (s[0]<='9'?s[0]-'0':s[0]-'a'+10);
        uint32_t b1 = (s[1]<='9'?s[1]-'0':s[1]-'a'+10);
        uint32_t b2 = (s[2]<='9'?s[2]-'0':s[2]-'a'+10);
        uint32_t b3 = (s[3]<='9'?s[3]-'0':s[3]-'a'+10);
        b0=(b0<<4)|(s[4]<='9'?s[4]-'0':s[4]-'a'+10);
        b1=(b1<<4)|(s[5]<='9'?s[5]-'0':s[5]-'a'+10);
        b2=(b2<<4)|(s[6]<='9'?s[6]-'0':s[6]-'a'+10);
        b3=(b3<<4)|(s[7]<='9'?s[7]-'0':s[7]-'a'+10);
        words[w]=(b0<<24)|(b1<<16)|(b2<<8)|b3;
    }
}

static bool duco_hash_full(const char* prev, const char* nonce, uint8_t nLen,
                           const uint32_t* target) {
    sha1_context_t ctx;
    sha1_init(&ctx);
    // Load prev hash words
    for (int i=0; i<10; i++) {
        ctx.W[i] = ((uint32_t)(uint8_t)prev[4*i]<<24)
                 | ((uint32_t)(uint8_t)prev[4*i+1]<<16)
                 | ((uint32_t)(uint8_t)prev[4*i+2]<<8)
                 | (uint8_t)prev[4*i+3];
    }
    // Load nonce
    ctx.W[10]=ctx.W[11]=ctx.W[12]=ctx.W[13]=ctx.W[14]=ctx.W[15]=0;
    if (nLen<=5) {
        uint32_t d0=(uint8_t)nonce[0], d1=(uint8_t)nonce[1],
                 d2=(uint8_t)nonce[2], d3=(uint8_t)nonce[3],
                 d4=(uint8_t)nonce[4];
        switch (nLen) {
            case 1: ctx.W[10]=(d0<<24)|0x00800000u; break;
            case 2: ctx.W[10]=(d0<<24)|(d1<<16)|0x00008000u; break;
            case 3: ctx.W[10]=(d0<<24)|(d1<<16)|(d2<<8)|0x00000080u; break;
            case 4: ctx.W[10]=(d0<<24)|(d1<<16)|(d2<<8)|d3; ctx.W[11]=0x80000000u; break;
            case 5: ctx.W[10]=(d0<<24)|(d1<<16)|(d2<<8)|d3;
                    ctx.W[11]=(d4<<24)|0x00800000u; break;
        }
        static const uint32_t kLen[6]={0,0x148,0x150,0x158,0x160,0x168};
        ctx.W[15]=kLen[nLen];
    } else {
        for (int i=0; i<nLen; i++) {
            uint8_t wi=10+(i>>2), sh=24-((i&3)<<3);
            ctx.W[wi]|=(uint32_t)(uint8_t)nonce[i]<<sh;
        }
        uint8_t wi=10+(nLen>>2), sh=24-((nLen&3)<<3);
        ctx.W[wi]|=0x80u<<sh;
        ctx.W[15]=(40+nLen)<<3;
    }
    sha1_transform(&ctx);
    return ctx.state[0]==target[0] && ctx.state[1]==target[1]
        && ctx.state[2]==target[2] && ctx.state[3]==target[3]
        && ctx.state[4]==target[4];
}

// ================== Nonce increment ==================
static void inc_nonce(char* s, uint8_t* len) {
    int8_t i=*len-1;
    for (; i>=0; --i) {
        if (s[i]!='9') { s[i]++; return; }
        s[i]='0';
    }
    for (uint8_t j=*len; j>0; --j) s[j]=s[j-1];
    s[0]='1'; (*len)++; s[*len]='\0';
}

// ================== Mining ==================
typedef uint32_t uintDiff;

static uintDiff ducos1a_mine(const char* prev, const uint32_t* target,
                             uintDiff maxNonce) {
    char nonce[12]="0";
    uint8_t nLen=1;
    for (uintDiff n=0; n<maxNonce; n++) {
        if (duco_hash_full(prev, nonce, nLen, target)) return n;
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
        if (!usart1_getc_timeout(&c, 10000)) continue;

        char last[41];
        last[0]=c;
        for (int i=1; i<40; i++) {
            if (!usart1_getc_timeout(&c,2000)) goto err;
            if (!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) goto err;
            last[i]=c;
        }
        last[40]=0;
        if (!usart1_getc_timeout(&c,2000)||c!=',') goto err;

        char newh[41];
        for (int i=0; i<40; i++) {
            if (!usart1_getc_timeout(&c,2000)) goto err;
            if (!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) goto err;
            newh[i]=c;
        }
        newh[40]=0;
        if (!usart1_getc_timeout(&c,2000)||c!=',') goto err;

        uint32_t diff=0; uint8_t dg=0;
        for (;;) {
            if (!usart1_getc_timeout(&c,2000)) goto err;
            if (c==',') break;
            if (c>='0'&&c<='9') {
                uint8_t d=c-'0';
                if (diff>(0xFFFFFFFFu-d)/10u) goto err;
                diff=diff*10u+d;
                if (++dg>9) goto err;
            } else goto err;
        }
        if (!dg) goto err;

        if (!usart1_getc_timeout(&c,2000)||c!='0') goto err;
        if (!usart1_getc_timeout(&c,2000)) goto err;
        if (c=='\r'&&!usart1_getc_timeout(&c,2000)) goto err;
        if (c!='\n') goto err;

        while (usart1_getc_timeout(&c,0)) {}

        uint32_t target[5];
        hex_to_words(newh, target);
        uintDiff maxN=diff*100+1;
        if (diff>655) maxN=0;

        uint32_t st=micros();
        uintDiff res=ducos1a_mine(last, target, maxN);
        uint32_t el=micros()-st;

        usart1_print_bin_u32(res); usart1_putc(',');
        usart1_print_bin_u32(el);  usart1_putc(',');
        usart1_print(ducoid_chars); usart1_putc('\n');
        continue;
    err:
        while (usart1_getc_timeout(&c,0)) {}
        usart1_print("ERR\n");
    }
    return 0;
}
