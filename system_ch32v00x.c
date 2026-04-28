// system_ch32v00x.c
#include "ch32v00x.h"

uint32_t SystemCoreClock = 48000000;   // <<< THÊM DÒNG NÀY

void SystemInit(void) {
    // ... (phần còn lại giữ nguyên)
}
    // 1. Bật HSI (High Speed Internal oscillator 8MHz)
    RCC->CTLR |= RCC_CTLR_HSION;
    while (!(RCC->CTLR & RCC_CTLR_HSIRDY)); // Chờ HSI sẵn sàng

    // 2. Cấu hình Flash latency cho 48MHz (0 wait state là đủ)
    FLASH->ACTLR = FLASH_ACTLR_LATENCY_0;

    // 3. Cấu hình PLL: nguồn HSI, nhân 6 để được 48MHz
    RCC->CFGR0 &= ~RCC_CFGR0_PLLSRC; // PLLSRC = HSI (0)
    RCC->CFGR0 |= RCC_CFGR0_PLLMUL6; // PLLMUL = 6
    RCC->CTLR |= RCC_CTLR_PLLON;      // Bật PLL
    while (!(RCC->CTLR & RCC_CTLR_PLLRDY)); // Chờ PLL sẵn sàng

    // 4. Chọn PLL làm clock hệ thống
    RCC->CFGR0 &= ~RCC_CFGR0_SW;
    RCC->CFGR0 |= RCC_CFGR0_SW_PLL;   // SW = PLL
    while ((RCC->CFGR0 & RCC_CFGR0_SWS) != RCC_CFGR0_SWS_PLL); // Chờ SW = PLL

    // 5. Cập nhật biến SystemCoreClock cho delay nếu cần
    SystemCoreClock = 48000000;
}
