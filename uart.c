// uart.c
#include "uart.h"
#include "ch32v00x.h"
#include "gpio.h"

#define UART_BASE USART1_BASE

void uart_init(uint32_t baud) {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_USART1;
    gpio_set_mode(PD5, GPIO_AF_PP_50MHz);
    gpio_set_mode(PD6, GPIO_IN_PU);
    USART1->BRR = 48000000 / baud;
    USART1->CTLR1 = USART_CTLR1_UE | USART_CTLR1_TE | USART_CTLR1_RE;
}
void uart_putc(char c) {
    while (!(USART1->STATR & USART_STATR_TXE));
    USART1->DATAR = c;
}
void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
char uart_getc(void) {
    while (!(USART1->STATR & USART_STATR_RXNE));
    return USART1->DATAR;
}
int uart_available(void) {
    return (USART1->STATR & USART_STATR_RXNE) ? 1 : 0;
}
