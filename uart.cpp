// uart.cpp – triển khai UART cho CH32V003, tự lực
#include "uart.h"
#include <Arduino.h>   // chỉ để có F_CPU và pinMode (không dùng Serial)

// Hàm nội bộ để ghi thanh ghi
static inline void uart_write_reg(uint32_t base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(base + offset) = value;
}
static inline uint32_t uart_read_reg(uint32_t base, uint32_t offset) {
    return *(volatile uint32_t *)(base + offset);
}

void UART_Init(uint32_t baudrate) {
    // Bật clock USART1: set bit 14 trong APB2PCENR (RCC->APB2PCENR)
    *(volatile uint32_t *)(0x40021018UL) |= (1 << 14);

    // Cấu hình chân PD5 (TX) và PD6 (RX) dùng GPIO
    pinMode(PD5, OUTPUT_AF_PP);   // TX
    pinMode(PD6, INPUT_PULLUP);   // RX

    // Tính BRR: F_CPU được định nghĩa bởi PlatformIO (48000000L)
    uint32_t brr = F_CPU / baudrate;
    uart_write_reg(CH32_UART1_BASE, CH32_UART_BRR_OFFSET, brr);

    // Bật USART, TX, RX
    uint32_t ctlr1 = 0;
    ctlr1 = USART_CTLR1_UE | USART_CTLR1_TE | USART_CTLR1_RE;
    uart_write_reg(CH32_UART1_BASE, CH32_UART_CTLR1_OFFSET, ctlr1);
}

void UART_SendChar(char c) {
    // Chờ TXE = 1
    while (!(uart_read_reg(CH32_UART1_BASE, CH32_UART_STATR_OFFSET) & USART_STATR_TXE));
    uart_write_reg(CH32_UART1_BASE, CH32_UART_DATAR_OFFSET, (uint32_t)c);
}

void UART_SendString(const char *str) {
    while (*str) UART_SendChar(*str++);
}

char UART_ReadChar(void) {
    // Chờ RXNE = 1
    while (!(uart_read_reg(CH32_UART1_BASE, CH32_UART_STATR_OFFSET) & USART_STATR_RXNE));
    return (char)(uart_read_reg(CH32_UART1_BASE, CH32_UART_DATAR_OFFSET) & 0xFF);
}

int UART_Available(void) {
    return (uart_read_reg(CH32_UART1_BASE, CH32_UART_STATR_OFFSET) & USART_STATR_RXNE) ? 1 : 0;
}
