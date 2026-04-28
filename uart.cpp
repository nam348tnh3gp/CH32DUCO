#include "uart.h"
#include <stdint.h>

#define CH32_UART1_BASE  0x40013800UL

#define UART_STATR_OFFSET  0x00
#define UART_DATAR_OFFSET  0x04
#define UART_BRR_OFFSET    0x08
#define UART_CTLR1_OFFSET  0x0C

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
    // Bật clock USART1 (bit 14 của RCC->APB2PCENR)
    *(volatile uint32_t *)(0x40021018UL) |= (1 << 14);

    // Cấu hình chân PD5 (TX) và PD6 (RX) qua thanh ghi GPIOD->CFGLR
    volatile uint32_t *gpiod_cfglr = (volatile uint32_t *)(0x40011400UL);
    uint32_t cfglr = *gpiod_cfglr;

    // PD5: xóa bit 23-20, set = 0011 (AF push-pull, 50MHz)
    cfglr &= ~((uint32_t)0xF << 20);
    cfglr |= (0x3 << 20);

    // PD6: xóa bit 27-24, set = 1000 (input pull-up)
    cfglr &= ~((uint32_t)0xF << 24);
    cfglr |= (0x8 << 24);

    *gpiod_cfglr = cfglr;

    // Baud rate (F_CPU được PlatformIO định nghĩa toàn cục)
    uint32_t brr = F_CPU / baudrate;
    reg_write(UART_BRR_OFFSET, brr);

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
