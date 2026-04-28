#include "ch32v003fun.h"
#include "kcdk_usart.h"
#include "duco_hash.h"
#include <string.h>

// -------------------------------------------------------------------
// USART context (used by kcdk_usart and ISR)
static kcdk_usart_context my_context;

// -------------------------------------------------------------------
// Microsecond timer using TIM2 (1 MHz, 16‑bit auto‑reload)
volatile uint32_t overflow_count = 0;

void TIM2_Init(void) {
    RCC->APB1PCENR |= RCC_APB1Periph_TIM2;
    TIM2->PSC = 47;                     // 48MHz/(47+1) = 1 MHz -> 1 µs
    TIM2->ATRLR = 0xFFFF;               // max 65535
    TIM2->CNT = 0;
    TIM2->DMAINTENR |= TIM_IT_Update;   // enable update interrupt
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CTLR1 |= TIM_CEN;
}

uint32_t micros(void) {
    __disable_irq();
    if (TIM2->INTFR & TIM_IT_Update) {
        TIM2->INTFR &= ~TIM_IT_Update;
        overflow_count++;
    }
    uint32_t ov = overflow_count;
    uint32_t cnt = TIM2->CNT;
    __enable_irq();
    return (ov * 65536UL) + cnt;
}

void TIM2_IRQHandler(void) __attribute__((interrupt));
void TIM2_IRQHandler(void) {
    if (TIM2->INTFR & TIM_IT_Update) {
        TIM2->INTFR &= ~TIM_IT_Update;
        overflow_count++;
    }
}

// -------------------------------------------------------------------
// DUCOID from 64‑bit unique ID (0x1FFFF7E8)
static char ducoid_chars[23];  // "DUCOID" + 16 hex + '\0'

static void generate_ducoid(void) {
    memcpy(ducoid_chars, "DUCOID", 6);
    uint8_t *uid = (uint8_t *)0x1FFFF7E8;   // 8 bytes unique ID
    char *ptr = ducoid_chars + 6;
    for (int i = 0; i < 8; i++) {
        uint8_t val = uid[i];
        *ptr++ = "0123456789ABCDEF"[val >> 4];
        *ptr++ = "0123456789ABCDEF"[val & 0xF];
    }
    *ptr = '\0';
}

// -------------------------------------------------------------------
// Helpers
static void hex_to_words(const char *hex, uint32_t *words) {
    for (int w = 0; w < 5; w++) {
        const char *src = hex + w * 8;
        uint32_t b0 = ((src[0] >= 'a' ? src[0]-'a'+10 : src[0]-'0') << 4) |
                      (src[1] >= 'a' ? src[1]-'a'+10 : src[1]-'0');
        uint32_t b1 = ((src[2] >= 'a' ? src[2]-'a'+10 : src[2]-'0') << 4) |
                      (src[3] >= 'a' ? src[3]-'a'+10 : src[3]-'0');
        uint32_t b2 = ((src[4] >= 'a' ? src[4]-'a'+10 : src[4]-'0') << 4) |
                      (src[5] >= 'a' ? src[5]-'a'+10 : src[5]-'0');
        uint32_t b3 = ((src[6] >= 'a' ? src[6]-'a'+10 : src[6]-'0') << 4) |
                      (src[7] >= 'a' ? src[7]-'a'+10 : src[7]-'0');
        words[w] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    }
}

static void increment_nonce_ascii(char *nonceStr, uint8_t *nonceLen) {
    int8_t i = *nonceLen - 1;
    for (; i >= 0; --i) {
        if (nonceStr[i] != '9') { nonceStr[i]++; return; }
        nonceStr[i] = '0';
    }
    for (uint8_t j = *nonceLen; j > 0; --j) nonceStr[j] = nonceStr[j-1];
    nonceStr[0] = '1';
    (*nonceLen)++;
    nonceStr[*nonceLen] = '\0';
}

static uint32_t ducos1a_mine(const char *prevBlockHash,
                             const uint32_t *targetWords,
                             uint32_t maxNonce) {
    duco_hash_state_t hash;
    duco_hash_init(&hash, prevBlockHash);

    char nonceStr[11] = "0";
    uint8_t nonceLen = 1;

    for (uint32_t nonce = 0; nonce < maxNonce; nonce++) {
        if (duco_hash_try_nonce(&hash, nonceStr, nonceLen, targetWords))
            return nonce;
        increment_nonce_ascii(nonceStr, &nonceLen);
    }
    return 0;
}

static void uint32_to_binary_string(uint32_t val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    uint32_t mask = 1UL << 31;
    while (mask && ((val & mask) == 0)) mask >>= 1;
    int i = 0;
    for (; mask; mask >>= 1) buf[i++] = (val & mask) ? '1' : '0';
    buf[i] = '\0';
}

static void send_result(uint32_t nonce, uint32_t elapsed_us,
                        const char *ducoid) {
    char buf[64];
    uint32_to_binary_string(nonce, buf);
    kcdk_usart_write((uint8_t*)buf, strlen(buf));
    kcdk_usart_write((uint8_t*)",", 1);
    uint32_to_binary_string(elapsed_us, buf);
    kcdk_usart_write((uint8_t*)buf, strlen(buf));
    kcdk_usart_write((uint8_t*)",", 1);
    kcdk_usart_write((uint8_t*)ducoid, strlen(ducoid));
    kcdk_usart_write((uint8_t*)"\n", 1);
}

static int read_line(char *buf, int max_len) {
    int i = 0;
    uint32_t start_wait = 0;
    int waiting = 0;
    while (i < max_len - 1) {
        if (my_context.available > 0) {
            char c = kcdk_usart_read(&my_context);
            if (c == '\n') { buf[i] = '\0'; return i; }
            buf[i++] = c;
            waiting = 0;
        } else {
            if (!waiting) { waiting = 1; start_wait = micros(); }
            else if ((micros() - start_wait) > 2000000UL) return -1;
            __WFI();
        }
    }
    return -1;
}

// -------------------------------------------------------------------
// USART interrupt handler (RX only)
void USART1_IRQHandler(void) __attribute__((interrupt));
void USART1_IRQHandler(void) {
    if (USART1->STATR & USART_STATR_RXNE) {
        uint8_t data = (uint8_t)(USART1->DATAR & 0xFF);
        my_context.buffer[my_context.head++] = data;
        if (my_context.head >= KCDK_USART_BUFFER_SIZE)
            my_context.head = 0;
        if (my_context.available < KCDK_USART_BUFFER_SIZE)
            my_context.available++;
        else
            my_context.tail = (my_context.tail + 1) % KCDK_USART_BUFFER_SIZE;
    }
}

// -------------------------------------------------------------------
// Arduino setup & loop (thay cho main)
void setup() {
    SystemInit();
    kcdk_usart_init();
    TIM2_Init();
    generate_ducoid();
}

void loop() {
    char line[101];
    int len = read_line(line, sizeof(line));
    if (len <= 0) {
        kcdk_usart_write((uint8_t*)"ERR\n", 4);
        return;
    }

    char *saveptr;
    char *lastHash = strtok_r(line, ",", &saveptr);
    char *newHash  = strtok_r(NULL, ",", &saveptr);
    char *diffStr  = strtok_r(NULL, ",", &saveptr);

    if (!lastHash || !newHash || !diffStr ||
        strlen(lastHash) != 40 || strlen(newHash) != 40) {
        kcdk_usart_write((uint8_t*)"ERR\n", 4);
        return;
    }

    uint32_t difficulty = 0;
    for (char *p = diffStr; *p; p++) {
        if (*p < '0' || *p > '9') { difficulty = 0; break; }
        difficulty = difficulty * 10 + (*p - '0');
    }
    if (difficulty == 0) { kcdk_usart_write((uint8_t*)"ERR\n", 4); return; }

    uint32_t targetWords[5];
    hex_to_words(newHash, targetWords);
    uint32_t maxNonce = difficulty * 100 + 1;

    // Tắt ngắt USART trong lúc đào để đo thời gian chính xác
    USART1->CTLR1 &= ~USART_CTLR1_RXNEIE;
    uint32_t t0 = micros();
    uint32_t result = ducos1a_mine(lastHash, targetWords, maxNonce);
    uint32_t elapsed = micros() - t0;
    USART1->CTLR1 |= USART_CTLR1_RXNEIE;

    // Dọn buffer USART thừa (nếu có)
    while (my_context.available) kcdk_usart_read(&my_context);

    send_result(result, elapsed, ducoid_chars);
}
