ENTRY(_start)
SECTIONS {
 . = 0x40080000;
 image_start = .;
 .text : { KEEP(*(.boot)) *(.text .text.*) }
 . = ALIGN(4096); text_end = .;
 ro_start = .;
 .rodata : { *(.rodata .rodata.*) }
 . = ALIGN(4096); ro_end = .;
 .data : { *(.data .data.*) }
 .bss (NOLOAD) : { . = ALIGN(16); bss_start = .; *(.bss .bss.*) *(COMMON) . = ALIGN(16); bss_end = .; }
 . = ALIGN(4096); image_end = .;
 image_size = image_end - image_start;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
