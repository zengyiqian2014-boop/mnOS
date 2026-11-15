/* Tiny image and valid DTB fixture for consent/memory tests. */
.section .rodata
.macro be32 value
.byte ((\value) >> 24)&255, ((\value) >> 16)&255, ((\value) >> 8)&255, (\value)&255
.endm
.global linux_image_start, linux_image_end, linux_dtb_start, linux_dtb_end, linux_root_start, linux_root_end
linux_root_start:
linux_root_end:
linux_image_start:
.byte 1,2,3,4,5,6,7,8
linux_image_end:
linux_dtb_start:
be32 0xd00dfeed
be32 linux_dtb_end-linux_dtb_start
be32 tree-linux_dtb_start
be32 strings-linux_dtb_start
be32 reserve-linux_dtb_start
be32 17
be32 16
be32 0
be32 linux_dtb_end-strings
be32 tree_end-tree
reserve:
.zero 16
tree:
be32 1
.zero 4
be32 1
.asciz "memory@80000000"
be32 3
be32 16
be32 0
be32 0
be32 0x80000000
be32 0
be32 0x04000000
be32 2
be32 2
be32 9
tree_end:
strings:
.asciz "reg"
linux_dtb_end:
.section .note.GNU-stack,"",@progbits
