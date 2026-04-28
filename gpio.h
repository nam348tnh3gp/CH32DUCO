// gpio.h
#pragma once
#include <stdint.h>
#define GPIO_IN_PU 0x8
#define GPIO_OUT_PP_50MHz 0x3
#define GPIO_AF_PP_50MHz 0xB
void gpio_set_mode(uint8_t pin, uint8_t mode);
void gpio_write(uint8_t pin, uint8_t val);
