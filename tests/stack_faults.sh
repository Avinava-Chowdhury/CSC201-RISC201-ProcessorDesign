#!/bin/bash
# Runs the programs in tests/stack through the simulator and checks the result.
# From the project root:  make test   (or: bash tests/stack_faults.sh)

cd "$(dirname "$0")/.." || exit 1

if [ ! -x ./sim ] || [ ! -x ./asm_tool ]; then
    echo "Build first with: make"
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
passed=0
failed=0

# run_case <program> <text that must show up> [more text that must show up...]
run_case() {
    prog=$1
    shift

    if ! ./asm_tool "tests/stack/$prog.s" "$tmp/$prog.hex" --hex > /dev/null; then
        echo "[FAIL] $prog (assembler error)"
        failed=$((failed + 1))
        return
    fi

    out=$(printf 'load %s\nstep 40\nregs\nquit\n' "$tmp/$prog.hex" | ./sim)

    for expected in "$@"; do
        if ! echo "$out" | grep -q -- "$expected"; then
            echo "[FAIL] $prog (missing: $expected)"
            failed=$((failed + 1))
            return
        fi
    done

    echo "[PASS] $prog"
    passed=$((passed + 1))
}

# normal push/pop: value comes back, sp is restored, no fault
run_case stack_ok         "r2 : 0x0000002a" "r14: 0x00010000"
if ./asm_tool tests/stack/stack_ok.s "$tmp/ok.hex" --hex > /dev/null &&
   printf 'load %s\nstep 40\nquit\n' "$tmp/ok.hex" | ./sim | grep -q "HARDWARE FAULT"; then
    echo "[FAIL] stack_ok (raised a fault)"
    failed=$((failed + 1))
fi

# every fault must be reported, and sp must not be left holding the bad value
run_case stack_overflow    "STACK_OVERFLOW"   "r14: 0x0000c000"
run_case stack_underflow   "STACK_UNDERFLOW"  "r14: 0x00010000"
run_case stack_misaligned  "MISALIGNED"       "r14: 0x00010000"
run_case stack_corrupt     "STACK_CORRUPTION" "address : 0x0000fffc"
run_case stack_oob         "OUT_OF_BOUNDS"    "address : 0x00020000"
run_case div_zero          "DIV_BY_ZERO"
run_case invalid_opcode    "INVALID_OPCODE"

echo
echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
