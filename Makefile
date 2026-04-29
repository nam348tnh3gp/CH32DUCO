# ============================================================
# Duino-Coin Miner for CH32V003 – NoneOS Makefile (gộp 1 file)
# ============================================================

PROJECT   := firmware

CC        := riscv-none-elf-gcc
OBJCOPY   := riscv-none-elf-objcopy
OBJDUMP   := riscv-none-elf-objdump
SIZE      := riscv-none-elf-size
NM        := riscv-none-elf-nm

ARCH_FLAGS := -march=rv32ec -mabi=ilp32e
OPT_FLAGS  := -Os -ffunction-sections -fdata-sections
DBG_FLAGS  := -g -Wall -Wextra
DEFINES    := -DCH32V003
INCLUDES   := -I.

CFLAGS     := $(ARCH_FLAGS) $(OPT_FLAGS) $(DBG_FLAGS) $(DEFINES) $(INCLUDES) \
              -ffreestanding -fno-builtin

LDFLAGS    := -T ch32v003.ld -nostartfiles -nodefaultlibs -nostdlib \
              -Wl,--gc-sections -Wl,--print-memory-usage -lgcc

# ===== Chỉ build 2 file =====
SRCS := main.c startup_ch32v003.S
OBJS := main.o startup_ch32v003.o

all: $(PROJECT).elf $(PROJECT).bin $(PROJECT).hex

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

startup_ch32v003.o: startup_ch32v003.S
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

$(PROJECT).elf: $(OBJS)
	$(CC) $(ARCH_FLAGS) $(OBJS) $(LDFLAGS) -o $@

$(PROJECT).bin: $(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

$(PROJECT).hex: $(PROJECT).elf
	$(OBJCOPY) -O ihex $< $@

.PHONY: clean disasm size

clean:
	rm -f *.o *.elf *.bin *.hex disasm.txt

disasm:
	$(OBJDUMP) -d $(PROJECT).elf > disasm.txt

size:
	$(SIZE) $(PROJECT).elf
