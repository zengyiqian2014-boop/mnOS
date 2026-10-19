.section .rdata,"dr"
.balign 16
.global system_payload, system_payload_end
system_payload:
.incbin PAYLOAD
system_payload_end:
