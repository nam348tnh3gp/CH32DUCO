// main.c – DUCO Miner NoneOS CH32V003
#include <string.h>
#include "ch32v00x.h"
#include "uart.h"
#include "gpio.h"
#include "delay.h"
#include "unique_id.h"
#include "duco_hash.h"

#define SEP_TOKEN ","
#define END_TOKEN "\n"
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
    // In nonce dạng nhị phân 32 bit
    for (int i = 31; i >= 0; i--)
        buf[pos++] = (result & (1UL << i)) ? '1' : '0';
    buf[pos] = '\0'; uart_puts(buf); uart_putc(',');
    
    // In thời gian dạng nhị phân 32 bit
    pos = 0;
    for (int i = 31; i >= 0; i--)
        buf[pos++] = (elapsed & (1UL << i)) ? '1' : '0';
    buf[pos] = '\0'; uart_puts(buf); uart_putc(',');
    
    uart_puts(ducoid_chars);
    uart_puts(END_TOKEN);
}

int main(void) {
    SystemInit();
    uart_init(115200);
    generate_ducoid();
    delay_ms(2000);

    while (1) {
        // Chờ dữ liệu từ UART
        if (!uart_available()) continue;

        // Đọc job: lastBlockHash, newBlockHash, difficulty
        char lastBlockHash[41] = {0};
        for (int i = 0; i < 40; i++) lastBlockHash[i] = uart_getc();
        if (uart_getc() != ',') continue;

        char newBlockHash[41] = {0};
        for (int i = 0; i < 40; i++) newBlockHash[i] = uart_getc();
        if (uart_getc() != ',') continue;

        char diffStr[16] = {0};
        int dpos = 0;
        while (1) {
            char c = uart_getc();
            if (c == ',') break;
            diffStr[dpos++] = c;
        }
        // Xóa buffer thừa
        while (uart_available()) uart_getc();

        uintDiff difficulty = strtoul(diffStr, NULL, 10);
        if (difficulty == 0) difficulty = 10;

        uint32_t targetWords[SHA1_HASH_LEN / 4];
        hex_to_words(newBlockHash, targetWords);

        duco_hash_state_t hash;
        duco_hash_init(&hash, lastBlockHash);

        char nonceStr[11] = "0";
        uint8_t nonceLen = 1;
        uintDiff maxNonce = difficulty * 100 + 1, nonce = 0;
        for (; nonce < maxNonce; nonce++) {
            if (duco_hash_try_nonce(&hash, nonceStr, nonceLen, targetWords)) break;
            increment_nonce_ascii(nonceStr, &nonceLen);
        }

        send_result(nonce, 0); // elapsed = 0 tạm thời
    }
}
