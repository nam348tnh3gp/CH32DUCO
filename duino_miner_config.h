// duino_miner_config.h
#pragma once
#include "duino_fake_arduino.h"   // cung cấp PC0, HIGH, LOW, digitalWrite...
#include "uart.h"

#ifndef DUINO_MINER_BAUD
#define DUINO_MINER_BAUD 115200u
#endif
#ifndef DUINO_MINER_SERIAL_TIMEOUT_MS
#define DUINO_MINER_SERIAL_TIMEOUT_MS 2000u
#endif

#define DUINO_HASH_HEX_LEN 40u
#define DUINO_ERR_RESPONSE "ERR\n"

typedef uint32_t duino_uint_diff_t;
#define DUINO_MAX_SAFE_DIFF 1000u

#define DUINO_LED_PIN PC0

static inline void duino_led_mining_on(void)  { digitalWrite(DUINO_LED_PIN, HIGH); }
static inline void duino_led_mining_off(void) { digitalWrite(DUINO_LED_PIN, LOW);  }

#define DUINO_SERIAL_BEGIN(baud) UART_Init(baud)
#define DUINO_SERIAL_WRITE(c)    UART_SendChar(c)
#define DUINO_SERIAL_PRINT(s)    UART_SendString(s)
#define DUINO_SERIAL_AVAILABLE() UART_Available()
#define DUINO_SERIAL_READ()      UART_ReadChar()
