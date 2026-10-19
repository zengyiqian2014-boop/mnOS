.section .rdata,"dr"
.balign 16
.global kernel_payload, kernel_payload_end
kernel_payload:
.incbin "build/desktop/kernel-x86_64.elf"
kernel_payload_end:
