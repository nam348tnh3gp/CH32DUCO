#ifndef DUCO_HASH_H
#define DUCO_HASH_H

#include <stdint.h>

#define SHA1_HASH_LEN 20

typedef struct {
    uint32_t initialWords[10];
    uint32_t tempState[5];
} duco_hash_state_t;

void duco_hash_init(duco_hash_state_t *hasher, const char *prevHash);
int  duco_hash_try_nonce(duco_hash_state_t *hasher, const char *nonce,
                         uint8_t nonceLen, const uint32_t *targetWords);

#endif
