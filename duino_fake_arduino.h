// duino_fake_arduino.h – Các định nghĩa thay thế Arduino cho CH32V003
#pragma once
#include <stdint.h>
#include "ch32v00x.h"

// ---------- GPIO ----------
#define HIGH 1
#define LOW  0
#define OUTPUT 1
#define INPUT  0

// Chân GPIO (CH32V003: PD0..PD7, PC0..PC7)
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
    uint16_t pinmask;
    if (pin < 8) { port = GPIOD; pinmask = (1 << pin); }
    else { port = GPIOC; pinmask = (1 << (pin - 8)); }
    if (mode == OUTPUT) {
        port->CFGLR &= ~((uint32_t)0xF << (4 * (pin % 8)));
        port->CFGLR |= (uint32_t)0x3 << (4 * (pin % 8)); // output 50MHz push-pull
    } else { // INPUT
        port->CFGLR &= ~((uint32_t)0xF << (4 * (pin % 8)));
        port->CFGLR |= (uint32_t)0x8 << (4 * (pin % 8)); // input pull-up
    }
}

static inline void digitalWrite(uint8_t pin, uint8_t val) {
    GPIO_TypeDef *port;
    uint16_t pinmask;
    if (pin < 8) { port = GPIOD; pinmask = (1 << pin); }
    else { port = GPIOC; pinmask = (1 << (pin - 8)); }
    if (val) port->BSHR = pinmask;   // set bit
    else     port->BCR = pinmask;   // clear bit
}

// ---------- Thời gian ----------
// Dùng SysTick đã được framework khởi tạo, ghi lại số mili giây
extern volatile uint32_t _millis_tick; // sẽ được khai báo trong CH32.cpp

void delay(uint32_t ms) {
    uint32_t start = _millis_tick;
    while ((_millis_tick - start) < ms);
}

uint32_t millis(void) {
    return _millis_tick;
}

uint32_t micros(void) {
    // Giả lập micros bằng millis*1000 (độ chính xác thấp nhưng đủ dùng)
    return _millis_tick * 1000;
}
