// duino_job_io.h (điều chỉnh cho CH32V003)
#pragma once
#include "duino_miner_config.h"

static inline void duino_serial_flush_read(void) {
    while (DUINO_SERIAL_AVAILABLE()) { (void)DUINO_SERIAL_READ(); }
}

static inline bool duino_wait_serial_byte(uint32_t timeout_ms) {
    uint32_t start = millis();
    while (!DUINO_SERIAL_AVAILABLE()) {
        if ((uint32_t)(millis() - start) >= timeout_ms) return false;
        delay(1);
    }
    return true;
}

static inline bool duino_read_char_with_timeout(char *out) {
    if (!duino_wait_serial_byte(DUINO_MINER_SERIAL_TIMEOUT_MS)) return false;
    int c = DUINO_SERIAL_READ();
    if (c < 0) return false;
    *out = (char)c;
    return true;
}

static inline bool duino_is_lower_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static inline bool duino_read_hash_field_until_comma(char *out_hash, uint8_t hex_len) {
    char c;
    for (uint8_t i = 0; i < hex_len; i++) {
        if (!duino_read_char_with_timeout(&c) || !duino_is_lower_hex_char(c)) return false;
        out_hash[i] = c;
    }
    out_hash[hex_len] = '\0';
    if (!duino_read_char_with_timeout(&c)) return false;
    return c == ',';
}

static inline bool duino_read_difficulty_until_comma(duino_uint_diff_t *diff) {
    uint32_t v = 0;
    uint8_t digits = 0;
    char c;
    for (;;) {
        if (!duino_read_char_with_timeout(&c)) return false;
        if (c == ',') break;
        if (c >= '0' && c <= '9') {
            uint8_t d = c - '0';
            if (v > (0xFFFFFFFFu - d) / 10u) return false;
            v = v * 10u + d;
            if (++digits > 9u) return false;
        } else return false;
    }
    if (digits == 0) return false;
    *diff = (duino_uint_diff_t)v;
    return true;
}
