// main.c – DUCO Miner NoneOS CH32V003 (UART an toàn, có timeout, chịu lỗi)
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

/* ---------- DUCOID từ UID ---------- */
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

/* ---------- Chuyển hex 40 ký tự -> mảng 5 word (big‑endian) ---------- */
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

/* ---------- Tăng nonce dạng chuỗi ASCII ---------- */
static void increment_nonce_ascii(char *nonceStr, uint8_t *nonceLen) {
    int8_t i = *nonceLen - 1;
    for (; i >= 0; --i) {
        if (nonceStr[i] != '9') { nonceStr[i]++; return; }
        nonceStr[i] = '0';
    }
    for (uint8_t j = *nonceLen; j > 0; --j) nonceStr[j] = nonceStr[j-1];
    nonceStr[0] = '1'; (*nonceLen)++; nonceStr[*nonceLen] = '\0';
}

/* ---------- itoa nhẹ (decimal) ---------- */
static char* u32_to_str(uint32_t num, char* str) {
    char temp[12];
    uint8_t i = 0;
    if (num == 0) {
        str[0] = '0'; str[1] = '\0';
        return str;
    }
    while (num > 0) {
        temp[i++] = '0' + (num % 10);
        num /= 10;
    }
    uint8_t j = 0;
    while (i > 0) str[j++] = temp[--i];
    str[j] = '\0';
    return str;
}

/* ---------- Gửi kết quả (số thập phân) ---------- */
static void send_result(uintDiff nonce, uint32_t elapsed_ms) {
    char buf[12];
    uart_puts(u32_to_str(nonce, buf));
    uart_putc(',');
    uart_puts(u32_to_str(elapsed_ms, buf));
    uart_putc(',');
    uart_puts(ducoid_chars);
    uart_puts("\n");
}

/**
 * Đọc một job từ UART (polling, có timeout).
 * Job có dạng: lastBlockHash(40),newBlockHash(40),difficulty,\n
 * Trả về 0 nếu thành công, 1 nếu lỗi/timeout.
 */
static int read_job(char *lastBlockHash, char *newBlockHash, char *diffStr) {
    char c;
    
    // 1. Đọc lastBlockHash (40 ký tự hex)
    for (int i = 0; i < 40; i++) {
        if (!uart_getc_timeout(&c, 2000)) return 1;
        lastBlockHash[i] = c;
    }
    lastBlockHash[40] = '\0';
    
    // 2. Dấu phẩy thứ nhất
    if (!uart_getc_timeout(&c, 1000) || c != ',') return 1;
    
    // 3. Đọc newBlockHash (40 ký tự hex)
    for (int i = 0; i < 40; i++) {
        if (!uart_getc_timeout(&c, 2000)) return 1;
        newBlockHash[i] = c;
    }
    newBlockHash[40] = '\0';
    
    // 4. Dấu phẩy thứ hai
    if (!uart_getc_timeout(&c, 1000) || c != ',') return 1;
    
    // 5. Đọc difficulty – bỏ qua dấu phẩy cuối cùng, dừng ở '\n'
    int dpos = 0;
    while (1) {
        if (!uart_getc_timeout(&c, 2000)) return 1;
        if (c == '\n') break;           // kết thúc dòng
        if (c == '\r') continue;        // bỏ qua carriage return nếu có
        if (c == ',') continue;         // bỏ qua dấu phẩy phân cách cuối cùng
        if (dpos < 15) {
            diffStr[dpos++] = c;
        }
    }
    diffStr[dpos] = '\0';
    
    // Không cần xóa buffer thừa vì đã đọc đến newline
    return 0;
}

/* ---------- main ---------- */
int main(void) {
    SystemInit();
    delay_init();          // TIM2 cho millis()
    uart_init(115200);
    generate_ducoid();
    delay_ms(2000);        // Chờ ổn định

    while (1) {
        char lastBlockHash[41];
        char newBlockHash[41];
        char diffStr[16];
        
        // Đợi có dữ liệu trước khi đọc cả job (tránh timeout vô ích)
        if (!uart_available()) continue;
        
        if (read_job(lastBlockHash, newBlockHash, diffStr)) {
            // Lỗi hoặc timeout -> bỏ qua, xả hết buffer rồi thử lại
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
        uintDiff maxNonce = difficulty * 100 + 1;
        uintDiff nonce = 0;
        for (; nonce < maxNonce; nonce++) {
            if (duco_hash_try_nonce(&hash, nonceStr, nonceLen, targetWords)) break;
            increment_nonce_ascii(nonceStr, &nonceLen);
        }
        
        uint32_t elapsed_ms = millis() - start_time;   // đơn vị mili giây
        send_result(nonce, elapsed_ms);
    }
}
