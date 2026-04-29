.section .vector_table, "ax"
.global g_pfnVectors
.type g_pfnVectors, @object
g_pfnVectors:
    .word   _estack
    .word   Reset_Handler           /* 0 */
    .word   NMI_Handler             /* 1 */
    .word   HardFault_Handler       /* 2 */
    .word   0
    .word   0
    /* ... */
    .word   TIM2_IRQHandler         /* 38 */
    .word   0                       /* padding to 64 entries if needed */

.section .text.Default_Handler, "ax"
.weak Reset_Handler
.weak NMI_Handler
.weak HardFault_Handler
.weak TIM2_IRQHandler

Reset_Handler:
    la sp, _estack
    call SystemInit
    call main
    ebreak

NMI_Handler:
HardFault_Handler:
TIM2_IRQHandler:
1:  j 1b
