; bench_sum: sum of 1..100 in r2 (expect r2 = 5050)
; Pattern: tight counting loop, one taken branch per iteration.
    mov r1, 100
    mov r2, 0
loop:
    add r2, r2, r1
    sub r1, r1, 1
    cmp r1, 0
    bgt loop
    halt
