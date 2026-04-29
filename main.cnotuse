#include <string.h>
#include <stdlib.h>
#include "ch32v00x.h"
#include "uart.h"
#include "gpio.h"
#include "delay.h"
#include "unique_id.h"
#include "duco_hash.h"

void SystemInit(void);
static void generate_ducoid(char *buf);
static void hex_to_words(const char *hex, uint32_t *words);
static void increment_nonce_ascii(char *nonce, uint8_t *len);
static void send_result(uint32_t nonce, uint32_t elapsed_ms, const char *ducoid);
static int read_job(char *last, char *newb, char *diff);
static void IWDG_Init(void);
static void IWDG_Feed(void);

#define DIFF_MAX_STR 15

int main(void) {
    SystemInit();
    delay_init();
    uart_init(115200);       // Giữ nguyên driver UART của bạn

    char ducoid[23];
    generate_ducoid(ducoid);

    IWDG_Init();

    delay_ms(2000);          // Chờ ổn định

    while (1) {
        char lastBlockHash[41], newBlockHash[41], diffStr[DIFF_MAX_STR+1];

        // Đợi job từ Python client
        if (!uart_available()) continue;   // polling nhẹ
        if (read_job(lastBlockHash, newBlockHash, diffStr))
            continue;                     // lỗi -> vòng lặp tiếp

        uint32_t difficulty = strtoul(diffStr, NULL, 10);
        if (difficulty == 0) difficulty = 10;

        uint32_t target[5];
        hex_to_words(newBlockHash, target);

        duco_hash_state_t hash;
        duco_hash_init(&hash, lastBlockHash);

        uint32_t start_time = millis();
        char nonce[12] = "0";
        uint8_t nonceLen = 1;
        uint32_t maxNonce = difficulty * 100 + 1;
        uint32_t valid_nonce = maxNonce;

        for (uint32_t i = 0; i < maxNonce; i++) {
            if (duco_hash_try_nonce(&hash, nonce, nonceLen, target)) {
                valid_nonce = i;
                break;
            }
            increment_nonce_ascii(nonce, &nonceLen);
        }
        uint32_t elapsed_ms = millis() - start_time;

        // Gửi kết quả
        send_result(valid_nonce, elapsed_ms, ducoid);

        IWDG_Feed();   // Feed watchdog sau mỗi job thành công
    }
}

/* --------------- Hàm phụ trợ --------------- */
static void generate_ducoid(char *buf) {
    uint8_t uid[8];
    unique_id_read(uid);
    memcpy(buf, "DUCOID", 6);
    char *p = buf + 6;
    for (int i = 0; i < 8; i++) {
        *p++ = "0123456789ABCDEF"[uid[i] >> 4];
        *p++ = "0123456789ABCDEF"[uid[i] & 0x0F];
    }
    *p = '\0';
}

#define HEX_NIB(c) ((c) < 'a' ? (c) - '0' : (c) - 'a' + 10)
static void hex_to_words(const char *hex, uint32_t *words) {
    for (int w = 0; w < 5; w++) {
        const char *s = hex + w * 8;
        words[w] = ((HEX_NIB(s[0]) << 4 | HEX_NIB(s[1])) << 24) |
                   ((HEX_NIB(s[2]) << 4 | HEX_NIB(s[3])) << 16) |
                   ((HEX_NIB(s[4]) << 4 | HEX_NIB(s[5])) << 8) |
                   (HEX_NIB(s[6]) << 4 | HEX_NIB(s[7]));
    }
}

static void increment_nonce_ascii(char *nonce, uint8_t *len) {
    int8_t i = *len - 1;
    for (; i >= 0; --i) {
        if (nonce[i] < '9') { nonce[i]++; return; }
        nonce[i] = '0';
    }
    for (int j = *len; j > 0; --j) nonce[j] = nonce[j-1];
    nonce[0] = '1'; (*len)++; nonce[*len] = '\0';
}

static char* u32_to_str(uint32_t num, char *str) {
    char tmp[12]; uint8_t i = 0;
    if (!num) { str[0] = '0'; str[1] = '\0'; return str; }
    while (num) { tmp[i++] = '0' + (num % 10); num /= 10; }
    uint8_t j = 0;
    while (i) str[j++] = tmp[--i];
    str[j] = '\0';
    return str;
}

static void send_result(uint32_t nonce, uint32_t elapsed_ms, const char *ducoid) {
    char buf[12];
    uart_puts(u32_to_str(nonce, buf));
    uart_putc(',');
    uart_puts(u32_to_str(elapsed_ms, buf));
    uart_putc(',');
    uart_puts(ducoid);
    uart_puts("\n");
}

static int read_job(char *last, char *newb, char *diff) {
    char c;
    // Đọc 40 ký tự last
    for (int i = 0; i < 40; i++)
        if (!uart_getc_timeout(&c, 2000)) return 1;
        else last[i] = c;
    last[40] = 0;
    if (!uart_getc_timeout(&c, 1000) || c != ',') return 1;
    for (int i = 0; i < 40; i++)
        if (!uart_getc_timeout(&c, 2000)) return 1;
        else newb[i] = c;
    newb[40] = 0;
    if (!uart_getc_timeout(&c, 1000) || c != ',') return 1;
    int dpos = 0;
    while (dpos < DIFF_MAX_STR) {
        if (!uart_getc_timeout(&c, 2000)) return 1;
        if (c == '\n' || c == '\r') break;
        if (c == ',') continue;
        diff[dpos++] = c;
    }
    diff[dpos] = 0;
    // Xả hết đến '\n'
    while (c != '\n')
        if (!uart_getc_timeout(&c, 500)) break;
    return 0;
}

static void IWDG_Init(void) {
    IWDG->CTLR = 0x5555;
    IWDG->PSCR = 0x06;    // /256 => 500 Hz
    IWDG->RLDR = 2000;    // 4 giây
    IWDG->CTLR = 0xCCCC;
}

static void IWDG_Feed(void) {
    IWDG->CTLR = 0xAAAA;
}
