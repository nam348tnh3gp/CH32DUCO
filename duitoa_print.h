#pragma once
#include <stdint.h>
#include "duino_miner_config.h"   // chứa macro DUINO_SERIAL_*
#pragma GCC optimize ("-Ofast")

static inline void duino_print_u32_bin_minimal(uint32_t n) {
  if (n == 0u) { DUINO_SERIAL_WRITE('0'); return; }
  uint32_t mask = 1UL << 31;
  while (mask && ((n & mask) == 0u)) mask >>= 1;
  for (; mask; mask >>= 1) DUINO_SERIAL_WRITE((n & mask) ? '1' : '0');
}

static inline void duino_send_result_line(uint32_t result_nonce,
                                          uint32_t elapsed_us,
                                          const char* duco_id_cstr) {
  duino_print_u32_bin_minimal(result_nonce);
  DUINO_SERIAL_WRITE(',');
  duino_print_u32_bin_minimal(elapsed_us);
  DUINO_SERIAL_WRITE(',');
  DUINO_SERIAL_PRINT(duco_id_cstr);
  DUINO_SERIAL_WRITE('\n');
}
