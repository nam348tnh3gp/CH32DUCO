#include "ch32v003fun.h"
#include "kcdk_usart.h"
#include "kcdk_config.h"

void kcdk_usart_init() {
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOD | RCC_APB2Periph_USART1;

    #if (KCDK_USART_SWAP == 1)
        AFIO->PCFR1 = ((AFIO->PCFR1 & (~AFIO_PCFR1_USART1_REMAP)) | AFIO_PCFR1_USART1_REMAP_1);

        GPIOD->CFGLR &= ~(0xF << (4*5));
        GPIOD->CFGLR |= (GPIO_CNF_IN_FLOATING << (4 * 5));

        GPIOD->CFGLR &= ~(0xF << (4*6));
        GPIOD->CFGLR |= ((GPIO_CNF_OUT_PP_AF | GPIO_Speed_10MHz) << (4 * 6));
    #else
        AFIO->PCFR1 &= ((~AFIO_PCFR1_USART1_REMAP_1) & (~AFIO_PCFR1_USART1_REMAP));

        GPIOD->CFGLR &= ~(0xF << (4*5));
        GPIOD->CFGLR |= ((GPIO_CNF_OUT_PP_AF | GPIO_Speed_10MHz) << (4 * 5));

        GPIOD->CFGLR &= ~(0xF << (4*6));
        GPIOD->CFGLR |= (GPIO_CNF_IN_FLOATING << (4 * 6));
    #endif

    USART1->CTLR1 = USART_WordLength_8b | USART_Parity_No | USART_Mode_Tx | USART_Mode_Rx | USART_CTLR1_RXNEIE;
    USART1->CTLR2 = USART_StopBits_1;
    USART1->CTLR3 = 0;

    USART1->BRR = ((FUNCONF_SYSTEM_CORE_CLOCK + (KCDK_USART_BAUD / 2)) / KCDK_USART_BAUD);

    USART1->CTLR1 |= CTLR1_UE_Set;

    NVIC_EnableIRQ(USART1_IRQn);
}

void kcdk_usart_write(uint8_t *message, uint8_t length) {
    for(uint8_t i = 0; i < length; i++) {
        while(!(USART1->STATR & USART_STATR_TXE));
        USART1->DATAR = (*(message + i));
    }
}

uint8_t kcdk_usart_read(kcdk_usart_context *ctx) {
    if(ctx->available) {
        uint8_t val = ctx->buffer[ctx->tail++];
        ctx->available--;
        if(ctx->tail >= KCDK_USART_BUFFER_SIZE) ctx->tail = 0;

        return val;
    }

    return 0;
}

uint8_t kcdk_usart_peek(kcdk_usart_context *ctx, uint8_t offset) {
    if(ctx->available && offset < ctx->available) {
        uint8_t pointer = (uint8_t)(((uint16_t)ctx->tail + (uint16_t)offset) % KCDK_USART_BUFFER_SIZE);
        uint8_t val = ctx->buffer[pointer];
        return val;
    }

    return 0;
}

void kcdk_usart_discard(kcdk_usart_context *ctx, uint8_t amount) {
    if(amount > ctx->available) amount = ctx->available;
    ctx->tail = (uint8_t)(((uint16_t)ctx->tail + (uint16_t)amount) % KCDK_USART_BUFFER_SIZE);
    ctx->available -= amount;
}
