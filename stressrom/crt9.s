@ dsstress ARM9 entry. The config block at offset 4 is patched by build.py.
    .section .text.start, "ax"
    .arm
    .global _start, cfg
_start:
    b       1f
cfg:
    .word   0x53525453          @ magic "STRS"
    .word   1                   @ mode: 0 fixed, 1 ramp
    .word   10                  @ level (fixed mode)
    .word   300                 @ ramp: frames per level
    .word   10                  @ max level
    .word   0                   @ cpu: extra loop iterations per frame
1:
    mov     r0, #0x04000000
    mov     r1, #0
    str     r1, [r0, #0x208]    @ IME off
    msr     cpsr_c, #0xDF       @ system mode, IRQ/FIQ masked
    ldr     sp, =0x023F0000
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
2:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     2b
    bl      main
3:  b       3b
    .pool
