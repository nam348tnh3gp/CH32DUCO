// uart.c – UART driver an toàn, xử lý lỗi, polling có timeout
#include "uart.h"
#include "ch32v00x.h"
#include "gpio.h"
#include "delay.h"

#define F_CPU 48000000L  // HCLK = 48 MHz

// Baud rate 115200 với HCLK=48MHz, USARTDIV = 48e6 / (16*115200) = 26.04167
// DIV_Mantissa = 26, DIV_Fraction = round(0.04167*16) = 1
#define UART_BRR_115200  ((26 << 4) | 1)  // 0x1A1

// Prototype helper
static inline void uart_clear_errors(void) {
    (void)USART1->STATR;
    (void)USART1->DATAR;
}

void uart_init(uint32_t baud) {
    // Bật clock cho GPIOD và USART1
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_USART1;

    // Cấu hình TX (PD5) là AF push-pull, RX (PD6) input pull-up
    gpio_set_mode(PD5, GPIO_AF_PP_50);   // <--- SỬA LỖI Ở ĐÂY
    gpio_set_mode(PD6, GPIO_IN_PU);

    // Thiết lập baud rate (mặc định 115200, giả định HCLK = 48MHz)
    USART1->BRR = UART_BRR_115200;

    // Bật UART, TX, RX
    USART1->CTLR1 = USART_CTLR1_UE | USART_CTLR1_TE | USART_CTLR1_RE;
}

void uart_putc(char c) {
    while (!(USART1->STATR & USART_STATR_TXE));
    USART1->DATAR = c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

char uart_getc(void) {
    uint16_t sr;
    char c;
    do {
        while (!(USART1->STATR & USART_STATR_RXNE));
        sr = USART1->STATR;
        c = USART1->DATAR;
    } while (sr & (USART_STATR_FE | USART_STATR_NE | USART_STATR_PE));
    return c;
}

int uart_available(void) {
    return (USART1->STATR & USART_STATR_RXNE) ? 1 : 0;
}

// Hàm đọc ký tự với timeout (ms), bỏ qua các byte lỗi
int uart_getc_timeout(char *c, uint32_t timeout_ms) {
    uint16_t sr;
    uint32_t start = millis();
    do {
        while (!(USART1->STATR & USART_STATR_RXNE)) {
            if ((millis() - start) >= timeout_ms) return 0; // timeout
        }
        sr = USART1->STATR;
        *c = USART1->DATAR;
        if (sr & (USART_STATR_ORE)) {
            // ORE: đọc dummy để clear
            volatile uint32_t dummy = USART1->DATAR;
            (void)dummy;
        }
        if (!(sr & (USART_STATR_FE | USART_STATR_NE | USART_STATR_PE))) {
            return 1; // byte sạch
        }
        start = millis(); // reset timeout, tiếp tục chờ byte khác
    } while (1);
}
