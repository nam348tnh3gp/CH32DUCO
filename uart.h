// uart.h – UART tối giản cho CH32V003
#pragma once
#include <stdint.h>

// Địa chỉ ngoại vi
#define PERIPH_BASE         ((uint32_t)0x40000000)
#define APB1PERIPH_BASE     (PERIPH_BASE)
#define APB2PERIPH_BASE     (PERIPH_BASE + 0x00010000)

#define USART1_BASE         (APB2PERIPH_BASE + 0x3800)

typedef struct {
    volatile uint32_t STATR;   // 0x00
    volatile uint32_t DATAR;   // 0x04
    volatile uint32_t BRR;     // 0x08
    volatile uint32_t CTLR1;   // 0x0C
    volatile uint32_t CTLR2;   // 0x10
    volatile uint32_t CTLR3;   // 0x14
    volatile uint32_t GPR;     // 0x18
} USART_TypeDef;

#define USART1 ((USART_TypeDef *)USART1_BASE)

// Bit định nghĩa
#define USART_CTLR1_UE   (1 << 13)
#define USART_CTLR1_TE   (1 << 3)
#define USART_CTLR1_RE   (1 << 2)
#define USART_STATR_TXE  (1 << 7)
#define USART_STATR_RXNE (1 << 5)

void UART_Init(uint32_t baudrate);
void UART_SendChar(char c);
void UART_SendString(const char *str);
char UART_ReadChar(void);
int  UART_Available(void);
