// uart.c – UART an toàn, xử lý tất cả cờ lỗi (ORE, FE, NE, PE) và đọc có timeout
#include "uart.h"
#include "ch32v00x.h"
#include "gpio.h"
#include "delay.h"

// Sau khi sửa SystemInit chạy 48MHz, BRR cho 115200 baud được tính lại chính xác:
//   HCLK = 48MHz, baud = 115200
//   USARTDIV = 48e6 / (16 * 115200) ≈ 26.04167
//   DIV_Mantissa = 26, DIV_Fraction = round(0.04167*16) = 1
#define USART_BRR_115200_48MHZ  ((26 << 4) | 1)   // 0x1A1

/**
 * Xóa mọi cờ lỗi UART bằng cách đọc STATR rồi DATAR.
 * Các cờ ORE, FE, NE, PE sẽ tự động được xóa sau khi đọc DATAR.
 */
static inline void uart_clear_errors(void) {
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

/**
 * Đọc 1 ký tự, bỏ qua mọi byte bị lỗi.
 * Chỉ trả về byte sạch (không lỗi frame, noise, parity).
 * Nếu không có dữ liệu sạch, hàm sẽ chờ vô hạn (hoặc đến khi có byte OK).
 */
char uart_getc(void) {
    uint16_t sr;
    char c;

    do {
        // Chờ cờ RXNE (có dữ liệu hoặc lỗi)
        while (!(USART1->STATR & USART_STATR_RXNE));
        // Đọc trạng thái và dữ liệu cùng lúc để xóa các cờ
        sr = USART1->STATR;
        c = USART1->DATAR;
        // Nếu có lỗi, bỏ qua byte này và tiếp tục chờ byte tiếp theo
    } while (sr & (USART_STATR_FE | USART_STATR_NE | USART_STATR_PE));

    return c;
}

/**
 * Đọc 1 ký tự với timeout (ms), bỏ qua byte lỗi.
 * Trả về 1 nếu nhận được byte sạch, 0 nếu timeout.
 */
int uart_getc_timeout(char *c, uint32_t timeout_ms) {
    uint16_t sr;
    uint32_t start = millis();

    do {
        // Chờ RXNE hoặc timeout
        while (!(USART1->STATR & USART_STATR_RXNE)) {
            if ((millis() - start) >= timeout_ms) {
                return 0; // Timeout
            }
        }
        // Đọc và kiểm tra lỗi
        sr = USART1->STATR;
        *c = USART1->DATAR;
        if (!(sr & (USART_STATR_FE | USART_STATR_NE | USART_STATR_PE))) {
            return 1; // Byte sạch
        }
        // Byte lỗi, reset thời gian và tiếp tục chờ byte khác
        start = millis();
    } while (1);
}

/**
 * Trả về 1 nếu có dữ liệu sẵn sàng (không phân biệt lỗi hay không).
 * Dùng để kiểm tra nhanh trước khi gọi uart_getc().
 */
int uart_available(void) {
    return (USART1->STATR & USART_STATR_RXNE) ? 1 : 0;
}
