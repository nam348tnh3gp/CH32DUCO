// ch32v00x.h – Minimal peripheral header for CH32V003 NoneOS
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Pin definitions ========== */
#define PD0 0
#define PD1 1
#define PD2 2
#define PD3 3
#define PD4 4
#define PD5 5
#define PD6 6
#define PD7 7
#define PC0 8
#define PC1 9
#define PC2 10
#define PC3 11
#define PC4 12
#define PC5 13
#define PC6 14
#define PC7 15

/* ========== Base addresses (giữ nguyên) ========== */
#define PERIPH_BASE        0x40000000UL
#define APB2PERIPH_BASE    (PERIPH_BASE + 0x10000UL)
#define RCC_BASE           (APB2PERIPH_BASE + 0x1000UL)
#define GPIOD_BASE         (APB2PERIPH_BASE + 0x1400UL)
#define GPIOC_BASE         (APB2PERIPH_BASE + 0x0800UL)
#define USART1_BASE        (APB2PERIPH_BASE + 0x3800UL)
#define FLASH_BASE         (0x40022000UL)

/* ========== Phần còn lại giữ nguyên ========== */
// ... (RCC, GPIO, USART, FLASH structs and macros)

/* ========== RCC ========== */
typedef struct {
    volatile uint32_t CTLR;
    volatile uint32_t CFGR0;
    volatile uint32_t INTR;
    volatile uint32_t INTFR;
    volatile uint32_t reserved[4];
    volatile uint32_t APB2PRSTR;
    volatile uint32_t reserved2[2];
    volatile uint32_t APB2PCENR;
} RCC_TypeDef;

#define RCC ((RCC_TypeDef *)RCC_BASE)

#define RCC_CTLR_HSION    (1 << 0)
#define RCC_CTLR_HSIRDY   (1 << 1)
#define RCC_CTLR_PLLON    (1 << 24)
#define RCC_CTLR_PLLRDY   (1 << 25)

#define RCC_CFGR0_PLLSRC  (1 << 16)
#define RCC_CFGR0_PLLMUL6 (0x3 << 18)
#define RCC_CFGR0_SW      (0x3 << 0)
#define RCC_CFGR0_SW_PLL  (0x2 << 0)
#define RCC_CFGR0_SWS     (0x3 << 2)
#define RCC_CFGR0_SWS_PLL (0x2 << 2)

#define RCC_APB2Periph_GPIOD  (1 << 4)
#define RCC_APB2Periph_GPIOC  (1 << 2)
#define RCC_APB2Periph_USART1 (1 << 14)

/* ========== GPIO ========== */
typedef struct {
    volatile uint32_t CFGLR;
    volatile uint32_t CFGHR;
    volatile uint32_t INDR;
    volatile uint32_t OUTDR;
    volatile uint32_t BSHR;
    volatile uint32_t BCR;
    volatile uint32_t LCKR;
} GPIO_TypeDef;

#define GPIOD ((GPIO_TypeDef *)GPIOD_BASE)
#define GPIOC ((GPIO_TypeDef *)GPIOC_BASE)

/* ========== USART ========== */
typedef struct {
    volatile uint32_t STATR;
    volatile uint32_t DATAR;
    volatile uint32_t BRR;
    volatile uint32_t CTLR1;
    volatile uint32_t CTLR2;
    volatile uint32_t CTLR3;
    volatile uint32_t GPR;
} USART_TypeDef;

#define USART1 ((USART_TypeDef *)USART1_BASE)

#define USART_CTLR1_UE  (1 << 13)
#define USART_CTLR1_TE  (1 << 3)
#define USART_CTLR1_RE  (1 << 2)
#define USART_STATR_TXE (1 << 7)
#define USART_STATR_RXNE (1 << 5)

/* ========== FLASH ========== */
typedef struct {
    volatile uint32_t ACTLR;
    volatile uint32_t KEYR;
    volatile uint32_t OBKEYR;
    volatile uint32_t STATR;
    volatile uint32_t CTLR;
    volatile uint32_t ADDR;
    volatile uint32_t reserved;
    volatile uint32_t OBR;
    volatile uint32_t WPR;
} FLASH_TypeDef;

#define FLASH ((FLASH_TypeDef *)FLASH_BASE)
#define FLASH_ACTLR_LATENCY_0 0x00000000

/* ========== System globals ========== */
extern uint32_t SystemCoreClock;

#ifdef __cplusplus
}
#endif
