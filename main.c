// main.c – DUCO Miner NoneOS CH32V003 (UART đọc an toàn, xử lý ORE, timeout)
#include <string.h>
#include <stdlib.h>
#include "ch32v00x.h"
#include "uart.h"
#include "gpio.h"
#include "delay.h"
#include "unique_id.h"
#include "duco_hash.h"

void SystemInit(void);

typedef uint32_t uintDiff;

static char ducoid_chars[23];

static void generate_ducoid(void) {
    uint8_t uid[8];
    unique_id_read(uid);
    memcpy(ducoid_chars, "DUCOID", 6);
    char *ptr = ducoid_chars + 6;
    for (int i = 0; i < 8; i++) {
        *ptr++ = "0123456789ABCDEF"[uid[i] >> 4];
        *ptr++ = "0123456789ABCDEF"[uid[i] & 0x0F];
    }
    *ptr = '\0';
}

#define HEX_NIBBLE(c) (((c) - '0' < 10) ? ((c) - '0') : ((c) - 'a' + 10))

static void hex_to_words(const char *hex, uint32_t *words) {
    for (uint8_t w = 0; w < SHA1_HASH_LEN / 4; w++) {
        const char *src = hex + w * 8;
        words[w] = ((HEX_NIBBLE(src[0]) << 4) | HEX_NIBBLE(src[1])) << 24 |
                   ((HEX_NIBBLE(src[2]) << 4) | HEX_NIBBLE(src[3])) << 16 |
                   ((HEX_NIBBLE(src[4]) << 4) | HEX_NIBBLE(src[5])) << 8 |
                   ((HEX_NIBBLE(src[6]) << 4) | HEX_NIBBLE(src[7]));
    }
}

static void increment_nonce_ascii(char *nonceStr, uint8_t *nonceLen) {
    int8_t i = *nonceLen - 1;
    for (; i >= 0; --i) {
        if (nonceStr[i] != '9') { nonceStr[i]++; return; }
        nonceStr[i] = '0';
    }
    for (uint8_t j = *nonceLen; j > 0; --j) nonceStr[j] = nonceStr[j-1];
    nonceStr[0] = '1'; (*nonceLen)++; nonceStr[*nonceLen] = '\0';
}

static void send_result(uintDiff result, uint32_t elapsed) {
    char buf[33]; int pos = 0;
    for (int i = 31; i >= 0; i--)
        buf[pos++] = (result & (1UL << i)) ? '1' : '0';
    buf[pos] = '\0'; uart_puts(buf); uart_putc(',');
    
    pos = 0;
    for (int i = 31; i >= 0; i--)
        buf[pos++] = (elapsed & (1UL << i)) ? '1' : '0';
    buf[pos] = '\0'; uart_puts(buf); uart_putc(',');
    
    uart_puts(ducoid_chars);
    uart_puts("\n");
}

// Đọc một dòng job từ UART với timeout. Trả về 0 nếu thành công, 1 nếu lỗi/timeout.
static int read_job(char *lastBlockHash, char *newBlockHash, char *diffStr) {
    char c;
    
    // Đọc lastBlockHash (40 ký tự)
    for (int i = 0; i < 40; i++) {
        if (!uart_getc_timeout(&c, 2000)) return 1;
        lastBlockHash[i] = c;
    }
    lastBlockHash[40] = '\0';
    
    // Dấu phẩy
    if (!uart_getc_timeout(&c, 1000) || c != ',') return 1;
    
    // Đọc newBlockHash (40 ký tự)
    for (int i = 0; i < 40; i++) {
        if (!uart_getc_timeout(&c, 2000)) return 1;
        newBlockHash[i] = c;
    }
    newBlockHash[40] = '\0';
    
    // Dấu phẩy
    if (!uart_getc_timeout(&c, 1000) || c != ',') return 1;
    
    // Đọc difficulty (chuỗi số) cho đến dấu phẩy
    int dpos = 0;
    while (1) {
        if (!uart_getc_timeout(&c, 1000)) return 1;
        if (c == ',') break;
        if (dpos < 15) {
            diffStr[dpos++] = c;
        }
    }
    diffStr[dpos] = '\0';
    
    // Xóa buffer thừa (nếu có)
    while (uart_available()) {
        uart_getc(); // đọc bỏ
    }
    return 0; // thành công
}

int main(void) {
    SystemInit();
    delay_init();          // Khởi tạo TIM2 cho millis chính xác
    uart_init(115200);
    generate_ducoid();
    delay_ms(2000);        // Đợi ổn định

    while (1) {
        // Chờ có ít nhất một byte trong buffer trước khi bắt đầu đọc job
        if (!uart_available()) continue;

        char lastBlockHash[41];
        char newBlockHash[41];
        char diffStr[16];
        
        if (read_job(lastBlockHash, newBlockHash, diffStr)) {
            // Lỗi hoặc timeout, bỏ qua job này, xả buffer nếu còn
            while (uart_available()) uart_getc();
            continue;
        }

        uintDiff difficulty = strtoul(diffStr, NULL, 10);
        if (difficulty == 0) difficulty = 10;

        uint32_t targetWords[SHA1_HASH_LEN / 4];
        hex_to_words(newBlockHash, targetWords);

        duco_hash_state_t hash;
        duco_hash_init(&hash, lastBlockHash);

        uint32_t start_time = millis();

        char nonceStr[11] = "0";
        uint8_t nonceLen = 1;
        uintDiff maxNonce = difficulty * 100 + 1, nonce = 0;
        for (; nonce < maxNonce; nonce++) {
            if (duco_hash_try_nonce(&hash, nonceStr, nonceLen, targetWords)) break;
            increment_nonce_ascii(nonceStr, &nonceLen);
        }

        uint32_t elapsed = (millis() - start_time) * 1000;
        send_result(nonce, elapsed);
    }
}
