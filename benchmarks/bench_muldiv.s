; bench_muldiv: sum of (i*i mod 7) + (i*i / 3) for i = 30..1 (expect r2 = 3206)
; Pattern: multiply / divide / modulo heavy ALU work.
    mov r1, 30
    mov r2, 0
loop:
    mul r3, r1, r1
    mod r4, r3, 7
    add r2, r2, r4
    div r5, r3, 3
    add r2, r2, r5
    sub r1, r1, 1
    cmp r1, 0
    bgt loop
    halt
