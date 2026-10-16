ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); }
SECTIONS {
 . = 0x100000000;
 .text : { *(.text*) } :code
 /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
