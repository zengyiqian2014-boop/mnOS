.DEFAULT_GOAL := all
CC ?= gcc
LD ?= ld
# A freestanding kernel has no glibc __*_chk runtime. Hosted tests retain the
# compiler's fortification and sanitizers; only native objects undefine it.
CFLAGS = -std=c11 -O2 -g -ffreestanding -U_FORTIFY_SOURCE -fno-builtin -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -m64 -mno-red-zone -mgeneral-regs-only -Wall -Wextra -Werror -Wno-misleading-indentation -Iinclude
SOURCES = src/core.c src/string.c src/phys.c src/memory.c src/smp.c src/interrupts.c src/devices.c src/rng.c src/block.c src/mnfs.c src/files.c src/password.c src/auth.c src/ui.c src/exec.c assets/font.c
SOURCES += src/process.c src/usercopy.c src/file_api.c src/disk_layout.c src/text.c src/commands.c assets/icons.c src/guest.c src/linux_guest.c
GUEST ?= 0
OBJECTS = $(patsubst %.c,build/%.o,$(SOURCES)) build/boot.o build/ap.o build/interrupts.o build/user.o build/exec-image.o
OBJECTS += build/guest-image-$(GUEST).o
build/guest-image-$(GUEST).o: src/guest-image.S $(if $(filter 1,$(GUEST)),build/guest/Image build/guest/machine.dtb)
	mkdir -p build
	$(CC) -m64 -DMNOS_LINUX_GUEST=$(GUEST) -c $< -o $@

build/user.elf: user/x86.S user/x86.ld
	mkdir -p build
	$(CC) -m64 -c user/x86.S -o build/user-exec.o
	$(LD) -nostdlib -z max-page-size=0x1000 -T user/x86.ld -o $@ build/user-exec.o
build/exec-image.o: build/user.elf
build/exec-image.o: build/user/hello.elf build/user/spin.elf
build/user/%.elf: user/example.c user/api.h include/file_api.h user/app-x86.ld
	mkdir -p build/user
	$(CC) $(CFLAGS) -mcmodel=large -DPROGRAM_$* -c user/example.c -o build/user/$*.o
	$(LD) -nostdlib -z max-page-size=0x1000 -T user/app-x86.ld -o $@ build/user/$*.o
.PHONY: user-programs
user-programs: $(addprefix build/user/,$(addsuffix .elf,hello spin fault badptr isolate fpstate simdstate tlsstate filetest))

.PHONY: all host-tools host-tests
all: build/mnos.elf

host-tools: build/mnfs
build/mnfs: tools/mnfs.c src/mnfs.c include/mnfs.h
	mkdir -p build
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -Iinclude tools/mnfs.c src/mnfs.c -o $@

host-tests:
	mkdir -p build
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -Iinclude tests/guest.c src/guest.c -o build/guest-test
	./build/guest-test
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude tests/commands.c src/commands.c -o build/commands-test
	./build/commands-test
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude tests/text.c src/text.c assets/font.c -o build/text-test
	./build/text-test
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -Iinclude tests/disk_layout.c src/disk_layout.c -o build/disk-layout-test
	./build/disk-layout-test
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude tests/exec.c src/exec.c -o build/exec-test
	./build/exec-test
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -DMNOS_HOST_PROCESS_TEST -Iinclude tests/process.c src/process.c src/exec.c src/usercopy.c -o build/process-test
	./build/process-test
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -Iinclude tests/file_api.c src/file_api.c src/disk_layout.c src/usercopy.c src/files.c src/mnfs.c src/auth.c src/password.c -o build/file-api-test
	./build/file-api-test
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude tests/phys.c src/phys.c -o build/phys-test
	./build/phys-test
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -Iinclude tests/mnfs.c src/mnfs.c -o build/mnfs-test
	./build/mnfs-test
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -Iinclude tests/auth.c src/auth.c src/password.c -o build/auth-test
	./build/auth-test

build/mnos.elf: $(OBJECTS) linker.ld
	$(LD) -nostdlib -z max-page-size=0x1000 -T linker.ld -o $@ $(OBJECTS)

build/%.o: %.c include/kernel.h include/phys.h include/mnfs.h include/block.h include/auth.h include/exec.h include/process.h include/file_api.h include/usercopy.h include/font.h include/icons.h include/commands.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.S
	mkdir -p $(dir $@)
	$(CC) -m64 -ffreestanding -fno-pie -c $< -o $@

build/desktop/kernel-x86_64.elf: $(OBJECTS) linker.ld
	mkdir -p build/desktop
	$(LD) -nostdlib -z max-page-size=0x1000 --defsym=native_efi_link=1 -e efi_kernel_entry -T linker.ld -o $@ $(OBJECTS)
