ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 1M;
 kernel_start = .;
 text_start = .;
 .text : ALIGN(4K) { KEEP(*(.multiboot)) *(.text.boot) *(.text .text.*) } :text
 . = ALIGN(4K); text_end = .;
 rodata_start = .;
 .rodata : ALIGN(4K) { *(.rodata .rodata.*) } :rodata
 . = ALIGN(4K); rodata_end = .;
 .data : ALIGN(4K) { *(.data .data.*) } :data
 .bss : ALIGN(4K) { *(COMMON) *(.bss .bss.*) } :data
 . = ALIGN(4K); kernel_end = .;
 /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) }
}
