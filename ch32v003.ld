/* ============================================================
 * CH32V003 Linker Script (RV32EC, 16KB Flash, 2KB RAM)
 * ============================================================ */

MEMORY
{
    FLASH  (rx) : ORIGIN = 0x08000000, LENGTH = 16K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 2K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

ENTRY(_start)

SECTIONS
{
    /* Code + read-only data */
    .text :
    {
        KEEP(*(.init))           /* startup */
        KEEP(*(.text._start))
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } > FLASH

    /* Initialized data */
    .data :
    {
        _data_start = .;
        *(.data)
        *(.data.*)
        _data_end = .;
    } > RAM AT > FLASH

    _data_loadaddr = LOADADDR(.data);

    /* Zero-initialized data */
    .bss :
    {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    } > RAM
}
