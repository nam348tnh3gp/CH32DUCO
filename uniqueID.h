// uniqueID.h – hỗ trợ RISC-V (CH32V003)
#pragma once
#include <stdint.h>

#if defined(ARDUINO_ARCH_AVR)
  #include <avr/boot.h>
  #if defined(__AVR_ATmega328PB__)
    #define UniqueIDsize 10
  #else
    #define UniqueIDsize 9
  #endif
  #define UniqueIDbuffer UniqueIDsize

#elif defined(ARDUINO_ARCH_ESP8266)
  #define UniqueIDsize 4
  #define UniqueIDbuffer 8

#elif defined(ARDUINO_ARCH_ESP32)
  #define UniqueIDsize 6
  #define UniqueIDbuffer 8

// ----- THÊM HỖ TRỢ RISC‑V -----
#elif defined(__riscv) || defined(RISCV) || defined(ARDUINO_ARCH_RISCV)
  #define UniqueIDsize  8      // 64-bit = 8 byte
  #define UniqueIDbuffer 8

// ----- Các kiến trúc khác giữ nguyên -----
#elif defined(ARDUINO_ARCH_SAM)
  #define UniqueIDsize 16
  #define UniqueIDbuffer 16

#elif defined(ARDUINO_ARCH_SAMD)
  #define UniqueIDsize 16
  #define UniqueIDbuffer 16

#elif defined(ARDUINO_ARCH_STM32)
  #define UniqueIDsize 12
  #define UniqueIDbuffer 12

#elif defined(TEENSYDUINO)
  #define UniqueIDsize 16
  #define UniqueIDbuffer 16

#else
  #error "ArduinoUniqueID: architecture not supported"
#endif

#define UniqueID8  (_UniqueID.id + UniqueIDbuffer - 8)
#define UniqueID   (_UniqueID.id + UniqueIDbuffer - UniqueIDsize)

class ArduinoUniqueID {
public:
    ArduinoUniqueID();
    uint8_t id[UniqueIDbuffer];
};

extern ArduinoUniqueID _UniqueID;
