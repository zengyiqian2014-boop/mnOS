/* Tiny bundled-image fixture for the runtime's consent tests. The full desktop
 * VM test separately boots the pinned Linux image on both host architectures. */
.section .rodata
.global linux_image_start, linux_image_end, linux_dtb_start, linux_dtb_end
linux_image_start:
.byte 1,2,3,4,5,6,7,8
linux_image_end:
linux_dtb_start:
.byte 0xd0,0x0d,0xfe,0xed
linux_dtb_end:
.section .note.GNU-stack,"",@progbits
