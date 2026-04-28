// system_ch32v00x.c – 48MHz PLL
#include "ch32v00x.h"

uint32_t SystemCoreClock = 48000000;

void SystemInit(void) {
    RCC->CTLR |= RCC_HSION;
    while (!(RCC->CTLR & RCC_HSIRDY));

    FLASH->ACTLR = FLASH_ACTLR_LATENCY_0;

    RCC->CFGR0 &= ~RCC_PLLSRC;
    RCC->CFGR0 |= RCC_PLLMULL6;
    RCC->CTLR |= RCC_PLLON;
    while (!(RCC->CTLR & RCC_PLLRDY));

    RCC->CFGR0 &= ~RCC_SW;
    RCC->CFGR0 |= RCC_SW_PLL;
    while ((RCC->CFGR0 & RCC_SWS) != RCC_SWS_PLL);

    SystemCoreClock = 48000000;
}
