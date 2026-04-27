all: build

build: | ../build
	mv -f main.bin main.elf main.hex main.lst main.map ../build/ 2>/dev/null || true

../build:
	mkdir -p ../build/

clean:
	rm -rf ../build/ || true

TARGET := main
ADDITIONAL_C_FILES := kcdk_usart.c duco_hash.c
PREFIX = riscv64-unknown-elf

include ../lib/ch32fun/ch32v003fun/ch32v003fun.mk
