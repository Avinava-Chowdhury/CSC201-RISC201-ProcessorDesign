; bench_rawchain: 25 rounds of a 4-op dependent chain on r1 (expect r1 = 67108867 = 0x04000003)
; Pattern: pure read-after-write dependencies, no loads -> forwarding, zero stalls.
    mov r1, 1
    mov r4, 25
loop:
    add r1, r1, r1
    sub r1, r1, 1
    xor r1, r1, 5
    add r1, r1, 3
    sub r4, r4, 1
    cmp r4, 0
    bgt loop
    halt
