; bench_float.s -- floating-point unit demo (IEEE-754 single precision)
; Computes ((1.5 + 2.25) * 2.0) / 4.0 - 0.5 = 1.375
; Floats are loaded as raw bit patterns with the 'li' macro (fadd/fsub/fmul/fdiv have no immediate form).
        li   r1, 0x3FC00000     ; 1.5
        li   r2, 0x40100000     ; 2.25
        li   r3, 0x40000000     ; 2.0
        li   r4, 0x40800000     ; 4.0
        li   r5, 0x3F000000     ; 0.5
        fadd r6, r1, r2         ; 3.75   = 0x40700000
        fmul r6, r6, r3         ; 7.5    = 0x40F00000
        fdiv r6, r6, r4         ; 1.875  = 0x3FF00000
        fsub r7, r6, r5         ; 1.375  = 0x3FB00000
        halt
