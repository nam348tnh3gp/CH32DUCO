#include "ch32v00x.h"

uint32_t SystemCoreClock = 48000000;

void SystemInit(void)
{
    // 1. Bật HSI và chờ ổn định
    RCC->CTLR |= RCC_HSION;
    while (!(RCC->CTLR & RCC_HSIRDY));

    // 2. Đặt Flash latency = 1 (cần thiết ở 48MHz)
    FLASH->ACTLR = FLASH_ACTLR_LATENCY_1;   // 0x00000001

    // 3. Chọn HSI làm nguồn PLL, nhân 2 (=> 48MHz)
    RCC->CFGR0 &= ~RCC_PLLSRC;              // HSI selected
    RCC->CFGR0 |= RCC_PLLMULL2;             // x2
    RCC->CTLR |= RCC_PLLON;
    while (!(RCC->CTLR & RCC_PLLRDY));

    // 4. Chọn PLL làm system clock
    RCC->CFGR0 = (RCC->CFGR0 & ~RCC_SW) | RCC_SW_PLL;
    while ((RCC->CFGR0 & RCC_SWS) != RCC_SWS_PLL);

    // 5. Cập nhật biến toàn cục
    SystemCoreClock = 48000000;
}
