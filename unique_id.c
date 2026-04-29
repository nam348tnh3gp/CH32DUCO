#include "unique_id.h"
#include "ch32v00x.h"

void unique_id_read(uint8_t uid[8]) {
    uint32_t *p = (uint32_t *)0x1FFFF7E8;
    uid[0] = p[0] >> 0;
    uid[1] = p[0] >> 8;
    uid[2] = p[0] >> 16;
    uid[3] = p[0] >> 24;
    uid[4] = p[1] >> 0;
    uid[5] = p[1] >> 8;
    uid[6] = p[1] >> 16;
    uid[7] = p[1] >> 24;
}
