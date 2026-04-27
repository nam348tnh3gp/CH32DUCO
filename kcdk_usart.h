#ifndef KCDK_USART_H
#define KCDK_USART_H

#include <stdint.h>

#define KCDK_USART_BUFFER_SIZE 100

typedef struct {
    volatile uint8_t buffer[KCDK_USART_BUFFER_SIZE];
    volatile uint8_t available;
    volatile uint8_t head;
    volatile uint8_t tail;
} kcdk_usart_context;

void kcdk_usart_init();
void kcdk_usart_write(uint8_t *message, uint8_t length);
uint8_t kcdk_usart_read(kcdk_usart_context *ctx);
uint8_t kcdk_usart_peek(kcdk_usart_context *ctx, uint8_t offset);

#endif
