// uart.h
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void UART_Init(uint32_t baudrate);
void UART_SendChar(char c);
void UART_SendString(const char *str);
char UART_ReadChar(void);
int  UART_Available(void);

#ifdef __cplusplus
}
#endif
