start:
    mov r1, 10
loop_start:
    sub r1, r1, 1
    cmp r1, 0
    bgt loop_start
    push r1
    call finish
    b end
finish:
    pop r2
    ret
end:
    nop