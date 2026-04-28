// delay.c – dùng TIM2 tạo ngắt 1ms chính xác
#include "delay.h"
#include "ch32v00x.h"

volatile uint32_t system_tick = 0;

// Trình phục vụ ngắt TIM2
void TIM2_IRQHandler(void) __attribute__((interrupt));
void TIM2_IRQHandler(void)
{
    if (TIM2->INTFR & TIM_UIF)          // kiểm tra cờ Update
    {
        TIM2->INTFR = (uint16_t)~TIM_UIF; // xoá cờ (ghi 0 vào bit)
        system_tick++;
    }
}

// Khởi tạo TIM2 với clock 48MHz → ngắt 1ms
void delay_init(void)
{
    // Bật clock cho TIM2 (bit 0 trong APB1PCENR)
    RCC->APB1PCENR |= RCC_TIM2EN;

    // Prescaler = 47 → timer clock = 48MHz / 48 = 1MHz
    TIM2->PSC = 47;
    // Period = 999 → 1000 xung = 1ms
    TIM2->ATRLR = 999;
    TIM2->CNT = 0;

    // Cho phép ngắt Update
    TIM2->DMAINTENR |= TIM_UIE;

    // Kích hoạt ngắt trong NVIC
    NVIC_EnableIRQ(TIM2_IRQn);

    // Bắt đầu đếm
    TIM2->CTLR1 |= TIM_CEN;
}

uint32_t millis(void)
{
    return system_tick;
}

void delay_ms(uint32_t ms)
{
    uint32_t start = millis();
    while ((millis() - start) < ms);
}
