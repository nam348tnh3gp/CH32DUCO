// delay.c
#include "delay.h"
void delay_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++)
        for (volatile uint32_t j = 0; j < 16000; j++) __asm("nop");
}

// Thêm hàm này
uint32_t millis(void) {
    static uint32_t fake_millis = 0;
    fake_millis += 10;
    return fake_millis;
}
