# SPDX-License-Identifier: GPL-3.0-or-later
# Invoke from native/: make -f arch/arm64/Makefile
.DEFAULT_GOAL := all
CLANG ?= clang-19
LLD ?= ld.lld-19
EFI_LD ?= lld-link-19
OBJCOPY ?= llvm-objcopy-19
COMMON = src/string.c src/phys.c src/mnfs.c src/files.c src/password.c src/auth.c src/ui.c src/exec.c assets/font.c
COMMON += src/process.c src/usercopy.c src/file_api.c src/disk_layout.c src/text.c src/commands.c assets/icons.c
COMMON += src/guest.c src/guest_float.c src/guest_devices.c src/net.c src/guest_memory.c src/guest_dtb.c src/linux_guest.c
COMMON += src/factor.c src/clock.c vendor/monocypher/monocypher.c
COMMON += vendor/qrcodegen/qrcodegen.c
COMMON += src/uvc.c src/usb_platform.c src/usb_xhci.c
GUEST ?= 0
ifneq ($(GUEST),0)
ifneq ($(GUEST),1)
$(error GUEST must be 0 or 1)
endif
endif
.PHONY: FORCE_GUEST
FORCE_GUEST:
build/arm64/guest-config: FORCE_GUEST
	@mkdir -p build/arm64
	@printf '%s\n' '$(GUEST)' > $@.tmp
	@cmp -s $@.tmp $@ || cp $@.tmp $@
FLAVOR ?= full
ifneq ($(filter $(FLAVOR),full lite),$(FLAVOR))
$(error FLAVOR must be full or lite)
endif
ARM = arch/arm64/acpi.c arch/arm64/core.c arch/arm64/fdt.c arch/arm64/memory.c arch/arm64/interrupts.c arch/arm64/virtio.c
FLAGS = -std=gnu11 -O2 -g -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -mgeneral-regs-only -mstrict-align -mno-outline-atomics -Wall -Wextra -Werror -Wno-misleading-indentation -Iarch/arm64/freestanding -Iinclude -Iarch/arm64 -MMD -MP
FLAGS += -DMNOS_LITE=$(if $(filter lite,$(FLAVOR)),1,0)
.PHONY: FORCE_FLAVOR
FORCE_FLAVOR:
build/arm64/flavor-config: FORCE_FLAVOR
	@mkdir -p build/arm64
	@printf '%s\n' '$(FLAVOR)' > $@.tmp
	@cmp -s $@.tmp $@ || cp $@.tmp $@
ELF = $(patsubst %.c,build/arm64/elf/%.o,$(COMMON) $(ARM)) build/arm64/elf/entry.o build/arm64/elf/user.o
PE = $(patsubst %.c,build/arm64/pe/%.obj,$(COMMON) $(ARM) arch/arm64/efi.c) build/arm64/pe/entry.obj build/arm64/pe/user.obj
ELF += build/arm64/elf/guest-image-$(GUEST).o
PE += build/arm64/pe/guest-image-$(GUEST).obj
build/arm64/elf/guest-image-$(GUEST).o: src/guest-image.S $(if $(filter 1,$(GUEST)),build/guest/Image build/guest/machine.dtb build/guest/rootfs.squashfs)
	mkdir -p $(dir $@)
	$(CLANG) --target=aarch64-none-elf -DMNOS_LINUX_GUEST=$(GUEST) -c $< -o $@
build/arm64/pe/guest-image-$(GUEST).obj: src/guest-image.S $(if $(filter 1,$(GUEST)),build/guest/Image build/guest/machine.dtb build/guest/rootfs.squashfs)
	mkdir -p $(dir $@)
	$(CLANG) --target=aarch64-unknown-windows -DMNOS_LINUX_GUEST=$(GUEST) -c $< -o $@
.PHONY: all
all: build/arm64/Image build/arm64/BOOTAA64.EFI
ELF += build/arm64/elf/exec-image.o
PE += build/arm64/pe/exec-image.obj
build/arm64/user.elf: build/arm64/elf/user.o user/arm64.ld
	$(LLD) -nostdlib -z max-page-size=0x1000 -T user/arm64.ld -o $@ $<
build/arm64/elf/exec-image.o build/arm64/pe/exec-image.obj: build/arm64/user.elf
build/arm64/elf/exec-image.o build/arm64/pe/exec-image.obj: build/arm64/user/hello.elf build/arm64/user/spin.elf
build/arm64/user/%.elf: user/example.c user/api.h include/file_api.h user/app-arm64.ld
	mkdir -p build/arm64/user
	$(CLANG) --target=aarch64-none-elf $(FLAGS) -DPROGRAM_$* -c user/example.c -o build/arm64/user/$*.o
	$(LLD) -nostdlib -z max-page-size=0x1000 -T user/app-arm64.ld -o $@ build/arm64/user/$*.o
.PHONY: user-programs
user-programs: $(addprefix build/arm64/user/,$(addsuffix .elf,hello spin fault badptr isolate fpstate simdstate tlsstate filetest))
build/arm64/elf/%.o: %.c build/arm64/flavor-config
	mkdir -p $(dir $@)
	$(CLANG) --target=aarch64-none-elf $(FLAGS) -Iinclude/freestanding -c $< -o $@
build/arm64/pe/%.obj: %.c build/arm64/flavor-config
	mkdir -p $(dir $@)
	# Native stacks are pre-mapped (64 KiB); there is no Windows CRT __chkstk.
	$(CLANG) --target=aarch64-unknown-windows $(FLAGS) -Iinclude/freestanding -fno-stack-check -mno-stack-arg-probe -fshort-wchar -c $< -o $@
build/arm64/elf/%.o: arch/arm64/%.S
	mkdir -p $(dir $@)
	$(CLANG) --target=aarch64-none-elf -c $< -o $@
build/arm64/pe/%.obj: arch/arm64/%.S
	mkdir -p $(dir $@)
	$(CLANG) --target=aarch64-unknown-windows -c $< -o $@
build/arm64/mnos.elf: $(ELF) arch/arm64/linker.ld build/arm64/guest-config
	$(LLD) -nostdlib -T arch/arm64/linker.ld -o $@ $(ELF)
build/arm64/Image: build/arm64/mnos.elf
	$(OBJCOPY) -O binary $< $@
build/arm64/BOOTAA64.EFI: $(PE) build/arm64/guest-config
	$(EFI_LD) /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:arm64 /base:0x100000 /filealign:512 /out:$@ $(PE)
build/arm64/BOOTAA64-TEST.EFI: $(PE) build/arm64/guest-config
	$(EFI_LD) /subsystem:efi_application /entry:efi_test_main /nodefaultlib /machine:arm64 /base:0x100000 /filealign:512 /out:$@ $(PE)
-include $(ELF:.o=.d) $(PE:.obj=.d)
