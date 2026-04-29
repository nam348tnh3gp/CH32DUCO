// uart.c - UART với xử lý ORE và đọc an toàn
#include "uart.h"
#include "ch32v00x.h"
#include "gpio.h"
#include "delay.h"

#define USART_BRR_115200_48MHZ 416

void uart_init(uint32_t baud) {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_USART1;
    gpio_set_mode(PD5, GPIO_AF_PP_50MHz);
    gpio_set_mode(PD6, GPIO_IN_PU);
    USART1->BRR = USART_BRR_115200_48MHZ;
    USART1->CTLR1 = USART_CTLR1_UE | USART_CTLR1_TE | USART_CTLR1_RE;
}

void uart_putc(char c) {
    while (!(USART1->STATR & USART_STATR_TXE));
    USART1->DATAR = c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

// Đọc ký tự với xử lý ORE
char uart_getc(void) {
    // Kiểm tra cờ ORE (Overrun Error)
    if (USART1->STATR & USART_STATR_ORE) {
        // Đọc thanh ghi trạng thái và dữ liệu để xóa cờ
        (void)USART1->STATR;
        (void)USART1->DATAR;
    }
    // Chờ dữ liệu sẵn sàng
    while (!(USART1->STATR & USART_STATR_RXNE));
    return USART1->DATAR;
}

// Đọc ký tự với timeout (ms)
// Trả về 1 nếu đọc thành công, 0 nếu timeout
int uart_getc_timeout(char *c, uint32_t timeout_ms) {
    uint32_t start = millis();
    while (!(USART1->STATR & USART_STATR_RXNE)) {
        if ((millis() - start) >= timeout_ms) {
            return 0; // Timeout
        }
    }
    *c = USART1->DATAR;
    return 1;
}

int uart_available(void) {
    return (USART1->STATR & USART_STATR_RXNE) ? 1 : 0;
}
