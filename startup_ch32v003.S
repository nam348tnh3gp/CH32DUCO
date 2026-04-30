/* ============================================================
 * CH32V003 startup – RV32EC (16 registers only)
 * SysTick_Handler properly increments tick_ms
 * ============================================================ */

    .section .init
    .globl _start
    .type _start, @function

_start:
    /* Set stack pointer */
    la sp, _estack

    /* Copy .data from FLASH to RAM */
    la a0, _data_start
    la a1, _data_end
    la a2, _data_loadaddr
    beq a0, a1, .L_skip_data
.L_copy_data:
    lw a3, 0(a2)
    sw a3, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    blt a0, a1, .L_copy_data
.L_skip_data:

    /* Zero .bss */
    la a0, _bss_start
    la a1, _bss_end
    beq a0, a1, .L_skip_bss
.L_zero_bss:
    sw zero, 0(a0)
    addi a0, a0, 4
    blt a0, a1, .L_zero_bss
.L_skip_bss:

    /* Jump to main */
    j main


/* ===== SysTick Handler – increment tick_ms ===== */
    .section .text.SysTick_Handler, "ax"
    .weak   SysTick_Handler
    .globl  SysTick_Handler
    .type   SysTick_Handler, @function

SysTick_Handler:
    /* Load address of tick_ms into a4 */
    la a4, tick_ms
    /* Load current value */
    lw a5, 0(a4)
    /* Increment */
    addi a5, a5, 1
    /* Store back */
    sw a5, 0(a4)
    ret


/* ===== Default interrupt handlers ===== */
    .section .text.Default_Handler, "ax"
    .weak   NMI_Handler
    .weak   HardFault_Handler
    .weak   SW_Handler
    .weak   WWDG_Handler
    .weak   PVD_Handler
    .weak   FLASH_Handler
    .weak   RCC_Handler
    .weak   EXTI7_0_Handler
    .weak   AWU_Handler
    .weak   DMA1_CH1_Handler
    .weak   DMA1_CH2_Handler
    .weak   DMA1_CH3_Handler
    .weak   DMA1_CH4_Handler
    .weak   DMA1_CH5_Handler
    .weak   DMA1_CH6_Handler
    .weak   DMA1_CH7_Handler
    .weak   ADC_Handler
    .weak   I2C1_EV_Handler
    .weak   I2C1_ER_Handler
    .weak   USART1_Handler
    .weak   SPI1_Handler
    .weak   TIM1_BRK_Handler
    .weak   TIM1_UP_Handler
    .weak   TIM1_TRG_Handler
    .weak   TIM1_CC_Handler
    .weak   TIM2_Handler

    .globl  Default_Handler
    .type   Default_Handler, %function

NMI_Handler:
HardFault_Handler:
SW_Handler:
WWDG_Handler:
PVD_Handler:
FLASH_Handler:
RCC_Handler:
EXTI7_0_Handler:
AWU_Handler:
DMA1_CH1_Handler:
DMA1_CH2_Handler:
DMA1_CH3_Handler:
DMA1_CH4_Handler:
DMA1_CH5_Handler:
DMA1_CH6_Handler:
DMA1_CH7_Handler:
ADC_Handler:
I2C1_EV_Handler:
I2C1_ER_Handler:
USART1_Handler:
SPI1_Handler:
TIM1_BRK_Handler:
TIM1_UP_Handler:
TIM1_TRG_Handler:
TIM1_CC_Handler:
TIM2_Handler:
Default_Handler:
    j .

    .weak   SystemInit
SystemInit:
    ret
