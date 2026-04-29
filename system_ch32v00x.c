// system_ch32v00x.c – 48MHz PLL (đã sửa)
#include "ch32v00x.h"

uint32_t SystemCoreClock = 48000000;

void SystemInit(void) {
    // 1. Bật HSI (24MHz), chờ sẵn sàng
    RCC->CTLR |= RCC_HSION;
    while (!(RCC->CTLR & RCC_HSIRDY));

    // 2. Cấu hình Flash: 1 wait state (bắt buộc khi SYSCLK > 24 MHz)
    //    Theo Reference Manual: LATENCY=0 dùng cho 0-24MHz, LATENCY=1 cho 24-48MHz
    FLASH->ACTLR = FLASH_ACTLR_LATENCY_1;   // 0x00000001

    // 3. PLL nguồn HSI (bit PLLSRC = 0), đã mặc định sau reset
    RCC->CFGR0 &= ~RCC_PLLSRC;             // giữ nguyên

    // 4. Đặt hệ số nhân PLL = 2 để có 48 MHz (24MHz * 2)
    //    PLLMUL = multiplier - 2   =>  2 -> 0. Do đó phải xoá tất cả bit PLLMUL[3:0]
    //    Macro RCC_PLLMULL6 (nhân 6) là sai cho CH32V003, sẽ gây ra 144MHz -> hỏng chip.
    RCC->CFGR0 &= ~((uint32_t)0x003C0000); // Xóa bit 21:18 (PLLMUL) = 0

    // 5. Bật PLL, đợi khóa
    RCC->CTLR |= RCC_PLLON;
    while (!(RCC->CTLR & RCC_PLLRDY));

    // 6. Chuyển system clock sang PLL
    RCC->CFGR0 &= ~RCC_SW;                 // SW = 00 (HSI ban đầu)
    RCC->CFGR0 |= RCC_SW_PLL;              // SW = 10 (PLL)
    while ((RCC->CFGR0 & RCC_SWS) != RCC_SWS_PLL); // Đợi chuyển xong

    // 7. Cập nhật biến toàn cục
    SystemCoreClock = 48000000;
}
