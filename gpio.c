#include "gpio.h"

static GPIO_TypeDef* get_port(uint8_t pin, uint8_t *pinpos) {
    if (pin < 8) {
        *pinpos = pin; return GPIOA;
    } else if (pin < 16) {
        *pinpos = pin - 8; return GPIOC;
    } else {
        *pinpos = pin - 16; return GPIOD;
    }
}

void gpio_set_mode(uint8_t pin, GPIOMode_t mode) {
    uint8_t pos;
    GPIO_TypeDef *port = get_port(pin, &pos);
    uint32_t offset = (pos & 0x7) * 4;
    uint32_t mask = (0xFu << offset);
    port->CFGLR = (port->CFGLR & ~mask) | ((mode & 0xFu) << offset);
}

void gpio_write(uint8_t pin, uint8_t val) {
    uint8_t pos;
    GPIO_TypeDef *port = get_port(pin, &pos);
    if (val) port->BSHR = (1u << pos);
    else     port->BCR  = (1u << pos);
}

uint8_t gpio_read(uint8_t pin) {
    uint8_t pos;
    GPIO_TypeDef *port = get_port(pin, &pos);
    return (port->INDR >> pos) & 1u;
}
