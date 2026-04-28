// CH32.ino – DUCO Miner for CH32V003 (đầy đủ các hàm)
#pragma GCC optimize ("-Ofast")
#include <stdint.h>
#include <string.h>
#include "uniqueID.h"
#include "duco_hash.h"
#include "uart.h"
#include "duino_miner_config.h"
#include "duino_job_io.h"
#include "duitoa_print.h"

typedef uint32_t uintDiff;

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

// ---------- Các hàm helper từ bản gốc ----------
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

uintDiff ducos1a(const char* prevBlockHash, const char* targetBlockHash, uintDiff difficulty) {
#if defined(ARDUINO_ARCH_AVR) || defined(ARDUINO_ARCH_MEGAAVR)
    if (difficulty > 655) return 0;
#endif
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

// ---------- setup & loop ----------
void setup() {
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
