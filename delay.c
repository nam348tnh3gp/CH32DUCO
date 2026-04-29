#include "delay.h"
#include "ch32v00x.h"

volatile uint32_t system_tick = 0;

void TIM2_IRQHandler(void) __attribute__((interrupt));
void TIM2_IRQHandler(void) {
    if (TIM2->INTFR & TIM_UIF) {
        TIM2->INTFR = (uint16_t)~TIM_UIF; // clear flag
        system_tick++;
    }
}

void delay_init(void) {
    RCC->APB1PCENR |= RCC_TIM2EN;
    TIM2->PSC = 47;                // 48MHz / 48 = 1MHz
    TIM2->ATRLR = 999;             // 1ms
    TIM2->CNT = 0;
    TIM2->DMAINTENR |= TIM_UIE;
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CTLR1 |= TIM_CEN;
}

uint32_t millis(void) {
    return system_tick;
}

void delay_ms(uint32_t ms) {
    uint32_t start = millis();
    while ((millis() - start) < ms);
}
