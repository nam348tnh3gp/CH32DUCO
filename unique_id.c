// unique_id.c
#include "unique_id.h"
void unique_id_read(uint8_t *buf) {
    volatile uint32_t *uid = (volatile uint32_t *)0x1FFFF7E8;
    for (int i = 0; i < 8; i++)
        buf[i] = ((uint8_t *)uid)[i];
}
