CC ?= gcc
LD ?= ld
CFLAGS = -std=c11 -O2 -g -ffreestanding -fno-builtin -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -m64 -mno-red-zone -mgeneral-regs-only -Wall -Wextra -Werror -Wno-misleading-indentation -Iinclude
SOURCES = src/core.c src/memory.c src/interrupts.c src/devices.c src/ui.c assets/font.c
OBJECTS = $(patsubst %.c,build/%.o,$(SOURCES)) build/boot.o build/interrupts.o build/user.o

.PHONY: all
all: build/mnos.elf

build/mnos.elf: $(OBJECTS) linker.ld
	$(LD) -nostdlib -z max-page-size=0x1000 -T linker.ld -o $@ $(OBJECTS)

build/%.o: %.c include/kernel.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.S
	mkdir -p $(dir $@)
	$(CC) -m64 -ffreestanding -fno-pie -c $< -o $@
