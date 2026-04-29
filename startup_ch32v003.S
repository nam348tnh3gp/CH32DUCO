/* ============================================================
 * CH32V003 startup – minimal NoneOS
 * ============================================================ */

    .section .init
    .globl _start
    .type _start, @function

_start:
    /* Set stack pointer */
    la sp, _estack

    /* Copy .data from FLASH to RAM */
    la t0, _data_start
    la t1, _data_end
    la t2, _data_loadaddr
    beq t0, t1, .L_skip_data
.L_copy_data:
    lw t3, 0(t2)
    sw t3, 0(t0)
    addi t0, t0, 4
    addi t2, t2, 4
    blt t0, t1, .L_copy_data
.L_skip_data:

    /* Zero .bss */
    la t0, _bss_start
    la t1, _bss_end
    beq t0, t1, .L_skip_bss
.L_zero_bss:
    sw zero, 0(t0)
    addi t0, t0, 4
    blt t0, t1, .L_zero_bss
.L_skip_bss:

    /* Jump to main */
    j main


/* ===== Default interrupt handler (REQUIRED – prevents undefined refs) ===== */
    .section .text.Default_Handler, "ax"
    .weak   NMI_Handler
    .weak   HardFault_Handler
    .weak   SysTick_Handler
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
SysTick_Handler:
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
