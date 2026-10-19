; bench_fib: iterative Fibonacci, 20 steps (expect r1 = F(20) = 6765)
; Pattern: long chains of back-to-back dependent ALU ops (forwarding paths).
    mov r1, 0
    mov r2, 1
    mov r4, 20
loop:
    add r3, r1, r2
    mov r1, r2
    mov r2, r3
    sub r4, r4, 1
    cmp r4, 0
    bgt loop
    halt
