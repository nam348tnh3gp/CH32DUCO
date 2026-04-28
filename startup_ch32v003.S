.section .vector_table, "ax"
.global Reset_Handler
Reset_Handler:
    la sp, _estack
    call main
    ebreak
