#!/bin/bash
# Floating-point unit tests.  From the project root:  bash tests/fpu_tests.sh   (also run by: make test)
#  1. ALU-level: fadd/fsub/fmul/fdiv compared bit-for-bit with the host's IEEE-754 hardware
#  2. program-level: bench_float.s gives the right result on BOTH pipelines
#  3. fdiv by zero gives infinity (no trap); illegal FPU encodings raise ILLEGAL_INSTRUCTION
#  4. assembler rejects an immediate operand on fadd

cd "$(dirname "$0")/.." || exit 1
if [ ! -x ./sim ] || [ ! -x ./asm_tool ]; then echo "Build first with: make"; exit 1; fi
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
passed=0; failed=0
pass() { echo "[PASS] $1"; passed=$((passed + 1)); }
fail() { echo "[FAIL] $1"; failed=$((failed + 1)); }

if g++ -std=c++20 -ffp-contract=off -Ialu -Iisa alu/ALU.cpp alu/fpu_test.cpp -o "$tmp/fpu_test" 2> "$tmp/cc.txt" &&
   "$tmp/fpu_test" | grep -q "ALL TESTS PASSED"; then pass "ALU: fadd/fsub/fmul/fdiv match IEEE-754 hardware"
else fail "ALU floating-point unit test"; cat "$tmp/cc.txt"; fi

./asm_tool benchmarks/bench_float.s "$tmp/bf.hex" --hex > /dev/null || fail "assemble bench_float"
for m in 4 6; do
    out=$(printf 'mode %s\nload %s\ncontinue\nregs\nquit\n' "$m" "$tmp/bf.hex" | ./sim)
    if echo "$out" | grep -q "r6 : 0x3ff00000" && echo "$out" | grep -q "r7 : 0x3fb00000" && echo "$out" | grep -q "HALTED (clean)"; then
        pass "bench_float result 1.375 (${m}-stage pipeline)"
    else fail "bench_float (${m}-stage pipeline)"; echo "$out" | tail -8; fi
done

# 1.0 / 0.0 = +inf, -1.0 / 0.0 = -inf, no fault
cat > "$tmp/dz.s" <<'ASM'
        li   r1, 0x3F800000
        li   r2, 0
        fdiv r3, r1, r2
        li   r4, 0xBF800000
        fdiv r5, r4, r2
        halt
ASM
./asm_tool "$tmp/dz.s" "$tmp/dz.hex" --hex > /dev/null
out=$(printf 'load %s\ncontinue\nregs\nquit\n' "$tmp/dz.hex" | ./sim)
if echo "$out" | grep -q "r3 : 0x7f800000" && echo "$out" | grep -q "r5 : 0xff800000" && ! echo "$out" | grep -q "FAULT"; then
    pass "fdiv by zero -> +/-infinity, no trap"
else fail "fdiv by zero"; echo "$out" | tail -8; fi

# illegal encodings: opcode 29 with the immediate bit set / with reserved bits [11:0] set
for w in 0xEC000000 0xE8000001; do
    printf '%s\n' "$w" > "$tmp/bad.hex"
    out=$(printf 'load %s\nstep 10\nquit\n' "$tmp/bad.hex" | ./sim)
    if echo "$out" | grep -q "ILLEGAL_INSTRUCTION"; then pass "illegal FPU word $w -> ILLEGAL_INSTRUCTION"
    else fail "illegal FPU word $w"; echo "$out" | tail -5; fi
done

printf 'fadd r1, r2, 5\nhalt\n' > "$tmp/imm.s"
if ./asm_tool "$tmp/imm.s" "$tmp/imm.hex" --hex > /dev/null 2>&1; then fail "assembler should reject fadd with an immediate"
else pass "assembler rejects fadd with an immediate"; fi

echo; echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
