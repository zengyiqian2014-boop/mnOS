ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x700000000000;
 .text : { *(.text*) *(.rodata*) } :code
 ASSERT(SIZEOF(.text) <= 4096, "native code exceeds one page")
 . = 0x700000001000;
 .data : { BYTE(0); *(.data*) } :data
 .bss (NOLOAD) : { *(.bss*) *(COMMON) } :data
 ASSERT(. <= 0x700000002000, "native data exceeds one page")
 /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
