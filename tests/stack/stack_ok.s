; a normal push and pop done by hand, nothing here should fault
; (nops keep dependent instructions apart so it runs on any pipeline)
    mov r1, 42
    sub sp, sp, 4
    nop
    nop
    st r1, 0[sp]
    nop
    nop
    ld r2, 0[sp]
    nop
    nop
    add sp, sp, 4
    nop
    nop
    nop
    nop
    halt
