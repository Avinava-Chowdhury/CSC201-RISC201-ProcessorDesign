; bench_gcd: Euclid's algorithm, gcd(1071, 462) (expect r1 = 21)
; Pattern: data-dependent loop with beq exit and an unconditional jump.
    mov r1, 1071
    mov r2, 462
gcd:
    cmp r2, 0
    beq done
    mod r3, r1, r2
    mov r1, r2
    mov r2, r3
    b gcd
done:
    halt
