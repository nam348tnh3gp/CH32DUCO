// duino_fake_arduino.h – tự động bật clock GPIO, delay NOP
#pragma once
#include <stdint.h>
#include "ch32v00x.h"

#define HIGH 1
#define LOW  0
#define OUTPUT 1
#define INPUT  0

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
    // Bật clock cho cổng tương ứng
    if (pin < 8) {
        RCC->APB2PCENR |= (1 << 4); // GPIOD
    } else {
        RCC->APB2PCENR |= (1 << 2); // GPIOC
    }

    GPIO_TypeDef *port = (pin < 8) ? GPIOD : GPIOC;
    uint8_t pinpos = pin % 8;
    uint32_t cfglr = port->CFGLR;
    cfglr &= ~((uint32_t)0xF << (4 * pinpos));
    cfglr |= (mode == OUTPUT) ? (0x3 << (4 * pinpos)) : (0x8 << (4 * pinpos));
    port->CFGLR = cfglr;
}

static inline void digitalWrite(uint8_t pin, uint8_t val) {
    GPIO_TypeDef *port = (pin < 8) ? GPIOD : GPIOC;
    uint16_t pinmask = (pin < 8) ? (1 << pin) : (1 << (pin - 8));
    if (val) port->BSHR = pinmask;
    else port->BCR = pinmask;
}

static inline void delay(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        for (volatile uint32_t j = 0; j < 16000; j++) __NOP();
    }
}

static inline uint32_t millis(void) { return 0; }
static inline uint32_t micros(void) { return 0; }
