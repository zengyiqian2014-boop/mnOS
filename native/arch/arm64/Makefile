# SPDX-License-Identifier: GPL-3.0-or-later
# Invoke from native/: make -f arch/arm64/Makefile
CLANG ?= clang-19
LLD ?= ld.lld-19
LINK ?= lld-link-19
OBJCOPY ?= llvm-objcopy-19
COMMON = src/string.c src/phys.c src/mnfs.c src/files.c src/password.c src/auth.c src/ui.c assets/font.c
ARM = arch/arm64/acpi.c arch/arm64/core.c arch/arm64/fdt.c arch/arm64/memory.c arch/arm64/interrupts.c arch/arm64/virtio.c
FLAGS = -std=gnu11 -O2 -g -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -mgeneral-regs-only -mstrict-align -mno-outline-atomics -Wall -Wextra -Werror -Wno-misleading-indentation -Iarch/arm64/freestanding -Iinclude -Iarch/arm64 -MMD -MP
ELF = $(patsubst %.c,build/arm64/elf/%.o,$(COMMON) $(ARM)) build/arm64/elf/entry.o build/arm64/elf/user.o
PE = $(patsubst %.c,build/arm64/pe/%.obj,$(COMMON) $(ARM) arch/arm64/efi.c) build/arm64/pe/entry.obj build/arm64/pe/user.obj
.PHONY: all
all: build/arm64/Image build/arm64/BOOTAA64.EFI
build/arm64/elf/%.o: %.c
	mkdir -p $(dir $@)
	$(CLANG) --target=aarch64-none-elf $(FLAGS) -c $< -o $@
build/arm64/pe/%.obj: %.c
	mkdir -p $(dir $@)
	$(CLANG) --target=aarch64-unknown-windows $(FLAGS) -fno-stack-check -fshort-wchar -c $< -o $@
build/arm64/elf/%.o: arch/arm64/%.S
	mkdir -p $(dir $@)
	$(CLANG) --target=aarch64-none-elf -c $< -o $@
build/arm64/pe/%.obj: arch/arm64/%.S
	mkdir -p $(dir $@)
	$(CLANG) --target=aarch64-unknown-windows -c $< -o $@
build/arm64/mnos.elf: $(ELF) arch/arm64/linker.ld
	$(LLD) -nostdlib -T arch/arm64/linker.ld -o $@ $(ELF)
build/arm64/Image: build/arm64/mnos.elf
	$(OBJCOPY) -O binary $< $@
build/arm64/BOOTAA64.EFI: $(PE)
	$(LINK) /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:arm64 /base:0x100000 /filealign:512 /out:$@ $(PE)
build/arm64/BOOTAA64-TEST.EFI: $(PE)
	$(LINK) /subsystem:efi_application /entry:efi_test_main /nodefaultlib /machine:arm64 /base:0x100000 /filealign:512 /out:$@ $(PE)
-include $(ELF:.o=.d) $(PE:.obj=.d)
