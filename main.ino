/*
   ____  __  __  ____  _  _  _____       ___  _____  ____  _  _
  (  _ \(  )(  )(_  _)( \( )(  _  )___  / __)(  _  )(_  _)( \( )
   )(_) ))(__)(  _)(_  )  (  )(_)((___)( (__  )(_)(  _)(_  )  (
  (____/(______)(____)(_)\_)(_____)     \___)(_____)(____)(_)\_)
  Official code for Arduino boards (and relatives)   version 4.3
  Duino-Coin Team & Community 2019-2024 © MIT Licensed
  Ported to CH32V003 - Bare-metal USART (no Arduino Serial)
*/

#pragma GCC optimize ("-Ofast")

#include <stdint.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 13
#endif

#define SEP_TOKEN ","
#define END_TOKEN "\n"

typedef uint32_t uintDiff;

#include <string.h>
#include "duco_hash.h"
#include "ch32v003fun.h"
#include "kcdk_usart.h"

// ======================== USART Context & ISR ========================
kcdk_usart_context my_context = { 0 };

void USART1_IRQHandler(void) __attribute__((interrupt));
void USART1_IRQHandler(void) {
    if (USART1->STATR & USART_STATR_RXNE) {
        uint8_t data = (uint8_t)(USART1->DATAR & 0xFF);
        my_context.buffer[my_context.head] = data;
        my_context.head = (my_context.head + 1) % KCDK_USART_BUFFER_SIZE;
        if (my_context.available < KCDK_USART_BUFFER_SIZE)
            my_context.available++;
        else
            my_context.tail = (my_context.tail + 1) % KCDK_USART_BUFFER_SIZE;
    }
}

// ======================== Unique ID (CH32V003 ESIG) ========================
static char ducoid_chars[23];

static void generate_ducoid() {
    memcpy(ducoid_chars, "DUCOID", 6);
    uint8_t* uid = (uint8_t*)0x1FFFF7E8;
    char* ptr = ducoid_chars + 6;
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t val = uid[i];
        *ptr++ = "0123456789ABCDEF"[val >> 4];
        *ptr++ = "0123456789ABCDEF"[val & 0x0F];
    }
    *ptr = '\0';
}

// ======================== Helpers ========================
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

static uint32_t ducos1a_mine(const char* prevBlockHash,
                             const uint32_t* targetWords,
                             uint32_t maxNonce) {
    static duco_hash_state_t hash;
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

// ======================== kcdk_usart helpers ========================

// Đọc 1 byte từ buffer vòng (có timeout đơn giản)
static int usart_read_byte_timeout(uint32_t timeout_us) {
    uint32_t start = micros();
    while (my_context.available == 0) {
        if (micros() - start > timeout_us) return -1;
    }
    uint8_t c = my_context.buffer[my_context.tail];
    my_context.tail = (my_context.tail + 1) % KCDK_USART_BUFFER_SIZE;
    my_context.available--;
    return c;
}

// Đọc cho đến khi gặp dấu ',' hoặc timeout
static int usart_read_until(char* buf, int max_len, char delim, uint32_t timeout_us) {
    int i = 0;
    while (i < max_len - 1) {
        int c = usart_read_byte_timeout(timeout_us);
        if (c < 0) return -1;
        if (c == delim) {
            buf[i] = '\0';
            return i;
        }
        buf[i++] = (char)c;
    }
    buf[i] = '\0';
    return i;
}

// Đọc 1 dòng (kết thúc bằng '\n')
static int usart_read_line(char* buf, int max_len, uint32_t timeout_us) {
    int len = usart_read_until(buf, max_len, '\n', timeout_us);
    if (len > 0 && buf[len - 1] == '\r') buf[len - 1] = '\0';
    return len;
}

// Chuyển uint32 sang chuỗi nhị phân tối giản
static void uint32_to_bin_str(uint32_t val, char* buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    uint32_t mask = 1UL << 31;
    while (mask && ((val & mask) == 0)) mask >>= 1;
    int i = 0;
    for (; mask; mask >>= 1) buf[i++] = (val & mask) ? '1' : '0';
    buf[i] = '\0';
}

// Gửi kết quả: nonce,thời gian,DUCOID\n
static void send_result(uint32_t nonce, uint32_t elapsed_us) {
    char buf[64];

    uint32_to_bin_str(nonce, buf);
    kcdk_usart_write((uint8_t*)buf, strlen(buf));
    kcdk_usart_write((uint8_t*)SEP_TOKEN, 1);

    uint32_to_bin_str(elapsed_us, buf);
    kcdk_usart_write((uint8_t*)buf, strlen(buf));
    kcdk_usart_write((uint8_t*)SEP_TOKEN, 1);

    kcdk_usart_write((uint8_t*)ducoid_chars, strlen(ducoid_chars));
    kcdk_usart_write((uint8_t*)END_TOKEN, 1);
}

// ======================== Arduino entry points ========================

void setup() {
    SystemInit();
    kcdk_usart_init();

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);   // LED tắt (mặc định)

    generate_ducoid();
}

void loop() {
    char line[101];

    // Đọc dòng lệnh từ USART
    int len = usart_read_line(line, sizeof(line), 2000000UL);
    if (len <= 0) {
        kcdk_usart_write((uint8_t*)"ERR\n", 4);
        return;
    }

    // Tách: lastBlockHash,newBlockHash,difficulty
    char* saveptr;
    char* lastHash = strtok_r(line, ",", &saveptr);
    char* newHash  = strtok_r(NULL, ",", &saveptr);
    char* diffStr  = strtok_r(NULL, ",", &saveptr);

    if (!lastHash || !newHash || !diffStr ||
        strlen(lastHash) != 40 || strlen(newHash) != 40) {
        kcdk_usart_write((uint8_t*)"ERR\n", 4);
        return;
    }

    uint32_t difficulty = 0;
    for (char* p = diffStr; *p; p++) {
        if (*p < '0' || *p > '9') { difficulty = 0; break; }
        difficulty = difficulty * 10 + (*p - '0');
    }
    if (difficulty == 0) {
        kcdk_usart_write((uint8_t*)"ERR\n", 4);
        return;
    }

    uint32_t targetWords[5];
    hex_to_words(newHash, targetWords);
    uint32_t maxNonce = difficulty * 100 + 1;

    // LED ON (đang mine)
    digitalWrite(LED_BUILTIN, LOW);

    uint32_t t0 = micros();
    uint32_t result = ducos1a_mine(lastHash, targetWords, maxNonce);
    uint32_t elapsed = micros() - t0;

    // LED OFF (xong)
    digitalWrite(LED_BUILTIN, HIGH);

    // Dọn buffer USART thừa
    while (my_context.available) {
        my_context.tail = (my_context.tail + 1) % KCDK_USART_BUFFER_SIZE;
        my_context.available--;
    }

    send_result(result, elapsed);
}
