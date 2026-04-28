TOOLCHAIN=riscv-none-elf
CC=$(TOOLCHAIN)-gcc
AS=$(TOOLCHAIN)-as
LD=$(TOOLCHAIN)-ld
OBJCOPY=$(TOOLCHAIN)-objcopy
CFLAGS=-march=rv32ec -mabi=ilp32e -Os -I. -DCH32V003
LDFLAGS=-T ch32v003.ld -nostartfiles

OBJS=startup_ch32v003.o system_ch32v00x.o main.o uart.o gpio.o delay.o unique_id.o duco_hash.o

all: firmware.bin

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(AS) -march=rv32ec -mabi=ilp32e $< -o $@

firmware.elf: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

firmware.bin: firmware.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin
