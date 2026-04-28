// uart.h – UART minimal cho CH32V003, không xung đột với framework
#pragma once
#include <stdint.h>

// Địa chỉ base của USART1 – từ framework đã có, nhưng ta dùng trực tiếp số
#define CH32_UART1_BASE  0x40013800UL

// Offset các thanh ghi (theo datasheet)
#define CH32_UART_STATR_OFFSET  0x00
#define CH32_UART_DATAR_OFFSET  0x04
#define CH32_UART_BRR_OFFSET    0x08
#define CH32_UART_CTLR1_OFFSET  0x0C

// Bit mask cho CTLR1 và STATR – chỉ định nghĩa nếu chưa có (tránh redefined)
#ifndef USART_CTLR1_UE
  #define USART_CTLR1_UE   (1 << 13)
#endif
#ifndef USART_CTLR1_TE
  #define USART_CTLR1_TE   (1 << 3)
#endif
#ifndef USART_CTLR1_RE
  #define USART_CTLR1_RE   (1 << 2)
#endif
#ifndef USART_STATR_TXE
  #define USART_STATR_TXE  (1 << 7)
#endif
#ifndef USART_STATR_RXNE
  #define USART_STATR_RXNE (1 << 5)
#endif

void UART_Init(uint32_t baudrate);
void UART_SendChar(char c);
void UART_SendString(const char *str);
char UART_ReadChar(void);
int  UART_Available(void);
