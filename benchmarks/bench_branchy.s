; bench_branchy: count the multiples of 3 in 1..60 (expect r2 = 20)
; Pattern: branch-heavy, taken and not-taken branches mixed (flush penalty).
    mov r1, 60
    mov r2, 0
loop:
    mod r3, r1, 3
    cmp r3, 0
    beq hit
    b next
hit:
    add r2, r2, 1
next:
    sub r1, r1, 1
    cmp r1, 0
    bgt loop
    halt
