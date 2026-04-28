// duino_miner_config.h (bổ sung cho CH32V003)
#pragma once
#include <stdint.h>

#ifndef DUINO_MINER_BAUD
#define DUINO_MINER_BAUD 115200u
#endif
#ifndef DUINO_MINER_SERIAL_TIMEOUT_MS
#define DUINO_MINER_SERIAL_TIMEOUT_MS 2000u
#endif

#define DUINO_HASH_HEX_LEN 40u
#define DUINO_ERR_RESPONSE "ERR\n"

// Định nghĩa kiểu difficulty
typedef uint32_t duino_uint_diff_t;
#define DUINO_MAX_SAFE_DIFF 1000u   // Giới hạn an toàn cho CH32V003

// ----- LED built‑in -----
// Trên hầu hết các board CH32V003, LED được nối với PC0
// Nếu board của bạn khác, sửa lại cho đúng.
#define DUINO_LED_PIN PC0

static inline void duino_led_mining_on(void)  { digitalWrite(DUINO_LED_PIN, HIGH); }
static inline void duino_led_mining_off(void) { digitalWrite(DUINO_LED_PIN, LOW);  }

// ----- UART thay thế Serial -----
#include "uart.h"
#define DUINO_SERIAL_BEGIN(baud) UART_Init(baud)
#define DUINO_SERIAL_WRITE(c)    UART_SendChar(c)
#define DUINO_SERIAL_PRINT(s)    UART_SendString(s)
#define DUINO_SERIAL_AVAILABLE() UART_Available()
#define DUINO_SERIAL_READ()      UART_ReadChar()
