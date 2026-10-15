CC ?= gcc
LD ?= ld
CFLAGS = -std=c11 -O2 -g -ffreestanding -fno-builtin -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -m64 -mno-red-zone -mgeneral-regs-only -Wall -Wextra -Werror -Wno-misleading-indentation -Iinclude
SOURCES = src/core.c src/phys.c src/memory.c src/smp.c src/interrupts.c src/devices.c src/block.c src/mnfs.c src/files.c src/ui.c assets/font.c
OBJECTS = $(patsubst %.c,build/%.o,$(SOURCES)) build/boot.o build/ap.o build/interrupts.o build/user.o

.PHONY: all host-tools host-tests
all: build/mnos.elf

host-tools: build/mnfs
build/mnfs: tools/mnfs.c src/mnfs.c include/mnfs.h
	mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -Iinclude tools/mnfs.c src/mnfs.c -o $@

host-tests:
	mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude tests/phys.c src/phys.c -o build/phys-test
	./build/phys-test
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -Iinclude tests/mnfs.c src/mnfs.c -o build/mnfs-test
	./build/mnfs-test

build/mnos.elf: $(OBJECTS) linker.ld
	$(LD) -nostdlib -z max-page-size=0x1000 -T linker.ld -o $@ $(OBJECTS)

build/%.o: %.c include/kernel.h include/phys.h include/mnfs.h include/block.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.S
	mkdir -p $(dir $@)
	$(CC) -m64 -ffreestanding -fno-pie -c $< -o $@
