// CH32.cpp – DUCO Miner for CH32V003 (hoàn toàn không dùng Arduino.h)
#pragma GCC optimize ("-Ofast")
#include <stdint.h>
#include <string.h>
#include "ch32v00x.h"            // cung cấp USART_TypeDef, GPIO, RCC, SysTick, v.v.
#include "uart.h"
#include "uniqueID.h"
#include "duco_hash.h"
#include "duino_miner_config.h"
#include "duino_job_io.h"
#include "duitoa_print.h"

typedef uint32_t uintDiff;

// ---------- Thay thế các macro/hàm Arduino cần dùng ----------
#define HIGH 1
#define LOW  0
#define OUTPUT 1
#define INPUT  0

static inline void pinMode(uint8_t pin, uint8_t mode) {
    // Chuyển đổi pin số sang cổng và bit
    GPIO_TypeDef *port;
    uint16_t pinmask;
    if (pin < 8) { port = GPIOD; pinmask = (1 << pin); }
    else { port = GPIOC; pinmask = (1 << (pin - 8)); } // giả định PC0..PC7
    if (mode == OUTPUT) {
        port->CFGLR &= ~((uint32_t)0xF << (4 * (pin % 8)));
        port->CFGLR |= (uint32_t)0x3 << (4 * (pin % 8)); // output 50MHz push-pull
    } else { // INPUT
        port->CFGLR &= ~((uint32_t)0xF << (4 * (pin % 8)));
        port->CFGLR |= (uint32_t)0x8 << (4 * (pin % 8)); // input pull-up
    }
}

static inline void digitalWrite(uint8_t pin, uint8_t val) {
    GPIO_TypeDef *port;
    uint16_t pinmask;
    if (pin < 8) { port = GPIOD; pinmask = (1 << pin); }
    else { port = GPIOC; pinmask = (1 << (pin - 8)); }
    if (val) port->BSHR = pinmask;   // set bit
    else     port->BCR = pinmask;   // clear bit
}

volatile uint32_t _millis = 0;   // giả lập millis bằng SysTick (nếu cần)
// Khởi tạo SysTick cho delay (nếu chưa có, có thể dùng vòng lặp thô)
void delay(uint32_t ms) {
    uint32_t start = SysTick->VAL; // không chính xác lắm, nhưng tạm
    // Cách đơn giản: dùng vòng lặp NOP, giả sử 48MHz, 1ms ~ 48000 chu kỳ
    for (uint32_t i = 0; i < ms * 48000; i++) {
        __NOP();
    }
}

uint32_t millis(void) {
    // Nếu không cần độ chính xác cao, dùng biến toàn cục cập nhật bởi SysTick_Handler
    return _millis;
}

uint32_t micros(void) {
    // Tương tự, có thể đọc SysTick
    return 0; // tạm, có thể cải thiện sau
}
// ------------------------------------------------------------

static char ducoid_chars[23];

static void generate_ducoid() {
    memcpy(ducoid_chars, "DUCOID", 6);
    char *ptr = ducoid_chars + 6;
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t val = UniqueID8[i];
        *ptr++ = "0123456789ABCDEF"[val >> 4];
        *ptr++ = "0123456789ABCDEF"[val & 0x0F];
    }
    *ptr = '\0';
}

#define HEX_NIBBLE(c) (((c) - '0' < 10) ? ((c) - '0') : ((c) - 'a' + 10))

static void hex_to_words(const char* hex, uint32_t* words) {
    for (uint8_t w = 0; w < SHA1_HASH_LEN / 4; w++) {
        const char* src = hex + w * 8;
        uint32_t b0 = (HEX_NIBBLE(src[0]) << 4) | HEX_NIBBLE(src[1]);
        uint32_t b1 = (HEX_NIBBLE(src[2]) << 4) | HEX_NIBBLE(src[3]);
        uint32_t b2 = (HEX_NIBBLE(src[4]) << 4) | HEX_NIBBLE(src[5]);
        uint32_t b3 = (HEX_NIBBLE(src[6]) << 4) | HEX_NIBBLE(src[7]);
        words[w] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    }
}

static void increment_nonce_ascii(char* nonceStr, uint8_t* nonceLen) {
    int8_t i = *nonceLen - 1;
    for (; i >= 0; --i) {
        if (nonceStr[i] != '9') {
            nonceStr[i]++;
            return;
        }
        nonceStr[i] = '0';
    }
    for (uint8_t j = *nonceLen; j > 0; --j)
        nonceStr[j] = nonceStr[j - 1];
    nonceStr[0] = '1';
    (*nonceLen)++;
    nonceStr[*nonceLen] = '\0';
}

// Forward declaration cần thiết
uintDiff ducos1a_mine(const char* prevBlockHash, const uint32_t* targetWords, uintDiff maxNonce);

uintDiff ducos1a(const char* prevBlockHash, const char* targetBlockHash, uintDiff difficulty) {
    uint32_t targetWords[SHA1_HASH_LEN / 4];
    hex_to_words(targetBlockHash, targetWords);
    uintDiff maxNonce = difficulty * 100 + 1;
    return ducos1a_mine(prevBlockHash, targetWords, maxNonce);
}

uintDiff ducos1a_mine(const char* prevBlockHash, const uint32_t* targetWords, uintDiff maxNonce) {
    static duco_hash_state_t hash;
    duco_hash_init(&hash, prevBlockHash);
    char nonceStr[10 + 1] = "0";
    uint8_t nonceLen = 1;
    for (uintDiff nonce = 0; nonce < maxNonce; nonce++) {
        if (duco_hash_try_nonce(&hash, nonceStr, nonceLen, targetWords)) {
            return nonce;
        }
        increment_nonce_ascii(nonceStr, &nonceLen);
    }
    return 0;
}

void setup() {
    // Cấu hình LED (dùng hàm tự viết)
    pinMode(DUINO_LED_PIN, OUTPUT);
    generate_ducoid();
    DUINO_SERIAL_BEGIN(DUINO_MINER_BAUD);
    delay(100);
}

void loop() {
    if (!DUINO_SERIAL_AVAILABLE()) return;

    char lastBlockHash[41];
    char newBlockHash[41];

    if (!duino_read_hash_field_until_comma(lastBlockHash, 40)) return;
    if (!duino_read_hash_field_until_comma(newBlockHash, 40)) return;

    duino_uint_diff_t difficulty;
    if (!duino_read_difficulty_until_comma(&difficulty)) return;

    duino_serial_flush_read();
    duino_led_mining_off();

    uint32_t startTime = micros();
    uintDiff result = ducos1a(lastBlockHash, newBlockHash, difficulty);
    uint32_t elapsed = micros() - startTime;

    duino_led_mining_on();
    duino_serial_flush_read();

    duino_send_result_line(result, elapsed, ducoid_chars);
}
