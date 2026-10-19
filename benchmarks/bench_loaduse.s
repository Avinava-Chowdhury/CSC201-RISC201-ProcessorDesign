; bench_loaduse: fill 16 words on the stack, then sum them (expect r5 = 288)
; Pattern: every load is used by the NEXT instruction (load-use hazard stalls).
    sub sp, sp, 64
    add r6, sp, 0
    mov r1, 16
    mov r2, 3
fill:
    st r2, 0[r6]
    add r2, r2, 2
    add r6, r6, 4
    sub r1, r1, 1
    cmp r1, 0
    bgt fill
    add r6, sp, 0
    mov r1, 16
    mov r5, 0
sumloop:
    ld r3, 0[r6]
    add r5, r5, r3
    add r6, r6, 4
    sub r1, r1, 1
    cmp r1, 0
    bgt sumloop
    add sp, sp, 64
    halt
