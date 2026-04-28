// system_ch32v00x.c
#include "ch32v00x.h"

void SystemInit(void) {
    // Chỉ bật HSI, không dùng PLL để tránh lỗi phức tạp
    RCC->CTLR |= RCC_HSION;                // Bật HSI
    while (!(RCC->CTLR & RCC_HSIRDY));     // Chờ HSI sẵn sàng

    // Chọn HSI làm clock hệ thống
    RCC->CFGR0 &= ~RCC_SW;
    RCC->CFGR0 |= RCC_SW_HSI;
    while ((RCC->CFGR0 & RCC_SWS) != RCC_SWS_HSI);
}
