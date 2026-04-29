// uart.h
#pragma once
#include <stdint.h>

void uart_init(uint32_t baud);
void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc(void);
int  uart_available(void);
int  uart_getc_timeout(char *c, uint32_t timeout_ms); // <<< THÊM DÒNG NÀY
