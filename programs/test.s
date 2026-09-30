mov r1, 5
mov r2, 0
loop:
add r2, r2, r1
sub r1, r1, 1
cmp r1, 0
bgt loop
push r2
pop r3