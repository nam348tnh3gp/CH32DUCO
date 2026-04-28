#pragma once
#include <Arduino.h>
#pragma GCC optimize ("-Ofast")

static inline void duino_print_u32_bin_minimal(uint32_t n) {
  if (n == 0u) { Serial.write('0'); return; }
  uint32_t mask = 1UL << 31;
  while (mask && ((n & mask) == 0u)) mask >>= 1;
  for (; mask; mask >>= 1) Serial.write((n & mask) ? '1' : '0');
}
static inline void duino_send_result_line(uint32_t result_nonce,
                                          uint32_t elapsed_us,
                                          const char* duco_id_cstr) {
  duino_print_u32_bin_minimal(result_nonce);
  Serial.write(',');
  duino_print_u32_bin_minimal(elapsed_us);
  Serial.write(',');
  Serial.print(duco_id_cstr);
  Serial.write('\n');
}
