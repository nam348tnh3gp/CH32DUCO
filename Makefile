# Makefile for CH32V003 NoneOS DUCO Miner
CROSS = riscv-wch-elf-
CC    = $(CROSS)gcc
CXX   = $(CROSS)g++
AS    = $(CROSS)as
LD    = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
SIZE    = $(CROSS)size

CFLAGS   = -march=rv32ec -mabi=ilp32e -Os -Wall -I. -DCH32V003 -ffunction-sections -fdata-sections -nostdlib -fno-builtin
CXXFLAGS = $(CFLAGS) -std=c++11 -fno-rtti -fno-exceptions
LDFLAGS  = -T ch32v003.ld -nostartfiles -Wl,--gc-sections

SRCS_C   = main.c uart.c gpio.c delay.c unique_id.c system_ch32v00x.c
SRCS_CPP = duco_hash.cpp
ASMS     = startup_ch32v003.S
OBJS     = $(SRCS_C:.c=.o) $(SRCS_CPP:.cpp=.o) $(ASMS:.S=.o)

all: firmware.bin

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.S
	$(AS) -march=rv32ec -mabi=ilp32e $< -o $@

firmware.elf: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

firmware.bin: firmware.elf
	$(OBJCOPY) -O binary $< $@
	$(SIZE) $<

clean:
	rm -f *.o *.elf *.bin
