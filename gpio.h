#ifndef __GPIO_H
#define __GPIO_H
#include "ch32v00x.h"

typedef enum {
    GPIO_IN_FLOATING = 0x04,
    GPIO_IN_PU       = 0x08,
    GPIO_OUT_PP_10   = 0x01,
    GPIO_OUT_PP_2    = 0x02,
    GPIO_OUT_PP_50   = 0x03,
    GPIO_AF_PP_50    = 0x0B       // multiplexed push-pull 50MHz
} GPIOMode_t;

void gpio_set_mode(uint8_t pin, GPIOMode_t mode);
void gpio_write(uint8_t pin, uint8_t val);
uint8_t gpio_read(uint8_t pin);

#endif
