MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 16K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 2K
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.vector_table))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .data : AT(ADDR(.text) + SIZEOF(.text))
    {
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > RAM

    .bss :
    {
        _bss_start = .;
        *(.bss*)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM

    /* Stack definition */
    _estack = ORIGIN(RAM) + LENGTH(RAM) - 4;
}
