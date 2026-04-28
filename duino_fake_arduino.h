// duino_fake_arduino.h – Thay thế các hàm/macro cơ bản của Arduino
#pragma once
#include <stdint.h>
#include "ch32v00x.h"

#define HIGH 1
#define LOW  0
#define OUTPUT 1
#define INPUT  0

// Chân GPIO
#define PD0 0
#define PD1 1
#define PD2 2
#define PD3 3
#define PD4 4
#define PD5 5
#define PD6 6
#define PD7 7
#define PC0 8
#define PC1 9
#define PC2 10
#define PC3 11
#define PC4 12
#define PC5 13
#define PC6 14
#define PC7 15

static inline void pinMode(uint8_t pin, uint8_t mode) {
    GPIO_TypeDef *port;
    uint8_t pinpos = pin % 8;
    if (pin < 8) {
        port = GPIOD;
    } else {
        port = GPIOC;
    }
    uint32_t cfglr = port->CFGLR;
    cfglr &= ~((uint32_t)0xF << (4 * pinpos));
    if (mode == OUTPUT) {
        cfglr |= (uint32_t)0x3 << (4 * pinpos);
    } else {
        cfglr |= (uint32_t)0x8 << (4 * pinpos);
    }
    port->CFGLR = cfglr;
}

static inline void digitalWrite(uint8_t pin, uint8_t val) {
    GPIO_TypeDef *port;
    uint16_t pinmask;
    if (pin < 8) {
        port = GPIOD;
        pinmask = (1 << pin);
    } else {
        port = GPIOC;
        pinmask = (1 << (pin - 8));
    }
    if (val) port->BSHR = pinmask;
    else     port->BCR = pinmask;
}

// ---------- DELAY DÙNG NOP (không phụ thuộc SysTick) ----------
static inline void delay(uint32_t ms) {
    // 48MHz: mỗi vòng lặp ~3 chu kỳ, cần 16000 vòng cho 1ms
    for (uint32_t i = 0; i < ms; i++) {
        for (volatile uint32_t j = 0; j < 16000; j++) {
            __NOP();
        }
    }
}

// millis/micros dùng biến đếm thô (đủ cho miner hoạt động)
volatile uint32_t _millis = 0;

uint32_t millis(void) {
    return _millis;
}

uint32_t micros(void) {
    return _millis * 1000;
}
