// uniqueID.cpp – hỗ trợ RISC-V (CH32V003)
#include "uniqueID.h"

ArduinoUniqueID::ArduinoUniqueID()
{
#if defined(ARDUINO_ARCH_AVR)
    for (size_t i = 0; i < UniqueIDsize; i++) {
        id[i] = boot_signature_byte_get(0x0E + i + (UniqueIDsize == 9 && i > 5 ? 1 : 0));
    }

#elif defined(ARDUINO_ARCH_ESP8266)
    uint32_t chipid = ESP.getChipId();
    id[0] = 0; id[1] = 0; id[2] = 0; id[3] = 0;
    id[4] = chipid >> 24; id[5] = chipid >> 16; id[6] = chipid >> 8; id[7] = chipid;

#elif defined(ARDUINO_ARCH_ESP32)
    uint64_t chipid = ESP.getEfuseMac();
    id[0] = 0; id[1] = 0;
    id[2] = chipid; id[3] = chipid >> 8; id[4] = chipid >> 16;
    id[5] = chipid >> 24; id[6] = chipid >> 32; id[7] = chipid >> 40;

// ----- RISC‑V: đọc trực tiếp từ ESIG -----
#elif defined(__riscv) || defined(RISCV) || defined(ARDUINO_ARCH_RISCV)
    // Hai thanh ghi chứa 64-bit unique ID
    volatile const uint32_t *uid_low  = (volatile const uint32_t *)0x1FFFF7E8;
    volatile const uint32_t *uid_high = (volatile const uint32_t *)0x1FFFF7EC;

    uint32_t low  = *uid_low;
    uint32_t high = *uid_high;

    // Lưu theo thứ tự big‑endian để khớp với cách sinh DUCOID
    id[0] = (high >> 24) & 0xFF;
    id[1] = (high >> 16) & 0xFF;
    id[2] = (high >>  8) & 0xFF;
    id[3] =  high        & 0xFF;
    id[4] = (low  >> 24) & 0xFF;
    id[5] = (low  >> 16) & 0xFF;
    id[6] = (low  >>  8) & 0xFF;
    id[7] =  low         & 0xFF;

#elif defined(ARDUINO_ARCH_SAM)
    // ... giữ nguyên code SAM ...

#elif defined(ARDUINO_ARCH_SAMD)
    // ... giữ nguyên code SAMD ...

#elif defined(ARDUINO_ARCH_STM32)
    // ... giữ nguyên code STM32 ...

#elif defined(TEENSYDUINO)
    // ... giữ nguyên code Teensy ...

#endif
}

ArduinoUniqueID _UniqueID;
