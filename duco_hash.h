#pragma once

#include <Arduino.h>

#define SHA1_HASH_LEN 20

struct duco_hash_state_t {
    uint32_t initialWords[10];
    uint32_t tempState[5];
};

void duco_hash_init(duco_hash_state_t *hasher, char const *prevHash);

bool duco_hash_try_nonce(duco_hash_state_t *hasher,
                         char const *nonce,
                         uint8_t nonceLen,
                         uint32_t const *targetWords);
