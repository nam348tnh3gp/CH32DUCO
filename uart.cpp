// uart.cpp
#include "uart.h"
#include <Arduino.h>   // cần cho pinMode, F_CPU

// Các định nghĩa nội bộ – không lộ ra ngoài
#define CH32_UART1_BASE  0x40013800UL

#define UART_STATR_OFFSET  0x00
#define UART_DATAR_OFFSET  0x04
#define UART_BRR_OFFSET    0x08
#define UART_CTLR1_OFFSET  0x0C

// Bit mask – đặt trong .cpp để tránh xung đột
static const uint32_t CTLR1_UE  = (1 << 13);
static const uint32_t CTLR1_TE  = (1 << 3);
static const uint32_t CTLR1_RE  = (1 << 2);
static const uint32_t STATR_TXE = (1 << 7);
static const uint32_t STATR_RXNE = (1 << 5);

static inline void reg_write(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(CH32_UART1_BASE + offset) = value;
}
static inline uint32_t reg_read(uint32_t offset) {
    return *(volatile uint32_t *)(CH32_UART1_BASE + offset);
}

void UART_Init(uint32_t baudrate) {
    // Bật clock USART1: set bit 14 trong RCC->APB2PCENR
    *(volatile uint32_t *)(0x40021018UL) |= (1 << 14);

    // Cấu hình chân PD5 (TX) và PD6 (RX)
    pinMode(PD5, OUTPUT_AF_PP);
    pinMode(PD6, INPUT_PULLUP);

    // Baud rate
    uint32_t brr = F_CPU / baudrate;
    reg_write(UART_BRR_OFFSET, brr);

    // Bật UE, TE, RE
    reg_write(UART_CTLR1_OFFSET, CTLR1_UE | CTLR1_TE | CTLR1_RE);
}

void UART_SendChar(char c) {
    while (!(reg_read(UART_STATR_OFFSET) & STATR_TXE));
    reg_write(UART_DATAR_OFFSET, (uint32_t)c);
}

void UART_SendString(const char *str) {
    while (*str) UART_SendChar(*str++);
}

char UART_ReadChar(void) {
    while (!(reg_read(UART_STATR_OFFSET) & STATR_RXNE));
    return (char)(reg_read(UART_DATAR_OFFSET) & 0xFF);
}

int UART_Available(void) {
    return (reg_read(UART_STATR_OFFSET) & STATR_RXNE) ? 1 : 0;
}
