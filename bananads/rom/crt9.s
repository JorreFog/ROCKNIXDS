@ banana2d test ROM, ARM9 entry: system mode, IRQs off, stack, zero .bss, main().
    .section .text.start, "ax"
    .arm
    .global _start
_start:
    mov     r0, #0x04000000
    mov     r1, #0
    str     r1, [r0, #0x208]    @ IME off
    msr     cpsr_c, #0xDF       @ system mode, IRQ/FIQ masked
    ldr     sp, =0x023F0000
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b
    bl      main
2:  b       2b
    .pool
