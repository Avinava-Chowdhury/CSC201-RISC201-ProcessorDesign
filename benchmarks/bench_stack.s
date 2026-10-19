; bench_stack: push 12..1, pop them back and add (expect r2 = 78)
; Pattern: heavy push/pop traffic (stack stores followed by stack loads).
    mov r1, 12
    mov r2, 0
pushloop:
    push r1
    sub r1, r1, 1
    cmp r1, 0
    bgt pushloop
    mov r1, 12
poploop:
    pop r3
    add r2, r2, r3
    sub r1, r1, 1
    cmp r1, 0
    bgt poploop
    halt
