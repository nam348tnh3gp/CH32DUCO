// uart.cpp
#include "uart.h"

// Hàm khởi tạo UART (mặc định 115200)
void UART_Init(uint32_t baudrate) {
    // Bật clock cho USART1 (bit 14 của APB2PCENR)
    volatile uint32_t *apb2pcenr = (volatile uint32_t *)(APB2PERIPH_BASE + 0x18);
    *apb2pcenr |= (1 << 14);

    // Cấu hình chân PD5 (TX) và PD6 (RX) là alternate function push‑pull
    // GPIOD base = 0x40011400
    volatile uint32_t *gpiod_cfglr = (volatile uint32_t *)(0x40011400 + 0x00);
    // PD5: bit 23-20 = 0011 (output 50MHz, AF push‑pull)
    // PD6: bit 27-24 = 1000 (input pull‑up)
    *gpiod_cfglr &= ~(0xF << 20); // Xóa 4 bit cấu hình PD5
    *gpiod_cfglr |=  (0x3 << 20); // PD5: output 50MHz, AF push‑pull
    *gpiod_cfglr &= ~(0xF << 24); // Xóa 4 bit cấu hình PD6
    *gpiod_cfglr |=  (0x8 << 24); // PD6: input pull‑up

    // Tính BRR cho 48 MHz: BRR = 48000000 / 115200 ≈ 416.67 → 416 (lỗi < 0.2%)
    uint32_t brr = (SystemCoreClock + baudrate/2) / baudrate;
    USART1->BRR = brr;

    // Bật USART, TX, RX
    USART1->CTLR1 |= USART_CTLR1_UE | USART_CTLR1_TE | USART_CTLR1_RE;
}

void UART_SendChar(char c) {
    while (!(USART1->STATR & USART_STATR_TXE)); // Chờ TXE = 1
    USART1->DATAR = c;
}

void UART_SendString(const char *str) {
    while (*str) UART_SendChar(*str++);
}

char UART_ReadChar(void) {
    while (!(USART1->STATR & USART_STATR_RXNE));
    return (char)(USART1->DATAR & 0xFF);
}

int UART_Available(void) {
    return (USART1->STATR & USART_STATR_RXNE) ? 1 : 0;
}
