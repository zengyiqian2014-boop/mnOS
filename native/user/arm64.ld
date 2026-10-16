ENTRY(arm_user_start)
PHDRS { code PT_LOAD FLAGS(5); }
SECTIONS {
 . = 0x700000000000;
 .text : { *(.text*) } :code
 /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
