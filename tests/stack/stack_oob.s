; store past the end of the 64 KB data memory
    mov r1, 7
    li r2, 0x20000
    nop
    nop
    st r1, 0[r2]
    nop
    nop
    nop
    nop
