// gpio.c
#include "gpio.h"
#include "ch32v00x.h"

void gpio_set_mode(uint8_t pin, uint8_t mode) {
    volatile uint32_t *cfglr = (pin < 8) ? &GPIOD->CFGLR : &GPIOC->CFGLR;
    uint8_t pos = pin % 8;
    *cfglr = (*cfglr & ~(0xF << (pos*4))) | (mode << (pos*4));
}
void gpio_write(uint8_t pin, uint8_t val) {
    if (pin < 8) {
        if (val) GPIOD->BSHR = 1 << pin;
        else GPIOD->BCR = 1 << pin;
    } else {
        if (val) GPIOC->BSHR = 1 << (pin-8);
        else GPIOC->BCR = 1 << (pin-8);
    }
}
