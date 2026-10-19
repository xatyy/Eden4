// SPDX-License-Identifier: GPL-3.0-or-later
// A self-contained, unencrypted Switch NRO. No firmware, keys, or SDK runtime.
.section .text
.global _start
_start:
    b entry
    .word 0
    .ascii "HOMEBREW"
    .ascii "NRO0"
    .word 0
    .word 0x3000
    .word 0
    .word 0
    .word 0x1000
    .word 0x1000
    .word 0x1000
    .word 0x2000
    .word 0x1000
    .word 0x1000
    .space 0x44
entry:
    adr x4, test_values
    ldp x2, x3, [x4]
    mul x2, x2, x3
    str x2, [x4, #16]
    ldr x5, [x4, #16]
    cmp x5, #42
    b.ne failed
    adr x0, passed_message
    mov x1, #passed_length
    svc #0x27
    svc #0x7
    b .
failed:
    adr x0, failed_message
    mov x1, #failed_length
    svc #0x27
    svc #0x7
    b .
    .balign 0x1000
passed_message:
    .ascii "EDEN_PS4_CPU_SMOKE_PASS: 6*7=42, RAM=PASS\n"
.set passed_length, . - passed_message
failed_message:
    .ascii "EDEN_PS4_CPU_SMOKE_FAIL\n"
.set failed_length, . - failed_message
    .balign 0x1000
test_values:
    .quad 6, 7, 0
    .space 0x1000 - 24
