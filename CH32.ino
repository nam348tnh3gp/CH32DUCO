// Arduino_Code_TURBO.ino (phiên bản CH32V003)
#pragma GCC optimize ("-Ofast")
#include <stdint.h>
#include <string.h>
#include "uniqueID.h"
#include "duco_hash.h"
#include "uart.h"
#include "duino_miner_config.h"
#include "duino_job_io.h"
#include "duitoa_print.h"

// LED built‑in được định nghĩa trong duino_miner_config.h

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

void setup() {
    pinMode(DUINO_LED_PIN, OUTPUT);
    generate_ducoid();

    DUINO_SERIAL_BEGIN(DUINO_MINER_BAUD);
    // Chờ UART sẵn sàng (không cần while(!Serial) vì không có Serial)
    delay(100);
}

// Các hàm hex_to_words, increment_nonce_ascii, ducos1a, ducos1a_mine giữ nguyên

void loop() {
    if (!DUINO_SERIAL_AVAILABLE()) return;

    char lastBlockHash[41];
    char newBlockHash[41];

    // Đọc job từ server (đã dùng UART thay Serial)
    if (!duino_read_hash_field_until_comma(lastBlockHash, 40)) return;
    if (!duino_read_hash_field_until_comma(newBlockHash, 40)) return;

    duino_uint_diff_t difficulty;
    if (!duino_read_difficulty_until_comma(&difficulty)) return;

    // Bỏ qua byte thừa
    duino_serial_flush_read();

    duino_led_mining_off();

    uint32_t startTime = micros();
    uintDiff result = ducos1a(lastBlockHash, newBlockHash, difficulty);
    uint32_t elapsed = micros() - startTime;

    duino_led_mining_on();

    duino_serial_flush_read();

    // Gửi kết quả: nonce (BIN), thời gian (BIN), DUCOID
    duino_send_result_line(result, elapsed, ducoid_chars);
}
