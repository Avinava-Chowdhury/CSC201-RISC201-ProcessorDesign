; bench_factorial: recursive factorial(8) (expect r2 = 40320)
; Pattern: call/ret, push/pop, multiplication, stack traffic.
    mov r1, 8
    call fact
    halt
fact:
    cmp r1, 1
    bgt recurse
    mov r2, 1
    ret
recurse:
    push r15
    push r1
    sub r1, r1, 1
    call fact
    pop r1
    pop r15
    mul r2, r2, r1
    ret
