// uart.c – UART an toàn, xử lý tất cả cờ lỗi (ORE, FE, NE, PE) và đọc có timeout
#include "uart.h"
#include "ch32v00x.h"
#include "gpio.h"
#include "delay.h"

// Sau khi sửa SystemInit chạy 48MHz, BRR cho 115200 baud được tính lại chính xác:
//   HCLK = 48MHz, baud = 115200
//   USARTDIV = 48e6 / (16 * 115200) ≈ 26.04167
//   DIV_Mantissa = 26, DIV_Fraction = round(0.04167*16) = 1
#define USART_BRR_115200_48MHZ  ((26 << 4) | 1)   // 417

// Tự động xoá mọi cờ lỗi UART bằng cách đọc STATR rồi DATAR
static inline void uart_clear_errors(void) {
    // Đọc thanh ghi trạng thái và dữ liệu để xoá tất cả cờ: ORE, FE, NE, PE
    (void)USART1->STATR;
    (void)USART1->DATAR;
}

void uart_init(uint32_t baud) {
    // Bật clock cho GPIOD và USART1
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_USART1;

    // Cấu hình chân TX (PD5) AF push-pull, RX (PD6) input pull-up
    gpio_set_mode(PD5, GPIO_AF_PP_50MHz);
    gpio_set_mode(PD6, GPIO_IN_PU);

    // Thiết lập tốc độ baud (hiện cố định 115200, giả định HCLK = 48MHz)
    USART1->BRR = USART_BRR_115200_48MHZ;

    // Cho phép UART, TX, RX
    USART1->CTLR1 = USART_CTLR1_UE | USART_CTLR1_TE | USART_CTLR1_RE;
}

void uart_putc(char c) {
    while (!(USART1->STATR & USART_STATR_TXE));
    USART1->DATAR = c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

// Đọc ký tự, xử lý triệt để mọi lỗi (ORE, FE, NE, PE)
char uart_getc(void) {
    // Nếu có bất kỳ lỗi nào, xoá chúng trước khi chờ dữ liệu
    if (USART1->STATR & (USART_STATR_ORE | USART_STATR_FE |
                         USART_STATR_NE  | USART_STATR_PE)) {
        uart_clear_errors();
    }
    // Chờ dữ liệu sẵn sàng
    while (!(USART1->STATR & USART_STATR_RXNE));
    return USART1->DATAR;
}

// Đọc ký tự có timeout (ms), trả về 1 nếu thành công, 0 nếu timeout
int uart_getc_timeout(char *c, uint32_t timeout_ms) {
    // Xoá lỗi trước khi bắt đầu chờ (giống như uart_getc)
    if (USART1->STATR & (USART_STATR_ORE | USART_STATR_FE |
                         USART_STATR_NE  | USART_STATR_PE)) {
        uart_clear_errors();
    }

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
