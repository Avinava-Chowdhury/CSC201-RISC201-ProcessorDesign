#!/bin/bash
# Checks the VERTICAL control unit against the HORIZONTAL one.
# From the project root:  bash tests/vertical_tests.sh   (also run by: make test)
#
#  1. the decode layer reproduces every horizontal microword (both pipelines)
#  2. every benchmark gives byte-identical registers/memory AND identical cycle counts
#     with the vertical control unit (cycle counts must match: control encoding must not change timing)
#  3. the vertical unit also runs the interactive debugger correctly (cu v, ucontrol vertical)

cd "$(dirname "$0")/.." || exit 1
if [ ! -x ./sim ] || [ ! -x ./asm_tool ]; then echo "Build first with: make"; exit 1; fi
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
passed=0; failed=0
pass() { echo "[PASS] $1"; passed=$((passed + 1)); }
fail() { echo "[FAIL] $1"; failed=$((failed + 1)); }

for m in 4 6; do
    out=$(printf 'mode %s\nucontrol compare\nquit\n' "$m" | ./sim)
    if echo "$out" | grep -q "decode check      : OK"; then pass "decode layer matches horizontal ROM (${m}-stage)"
    else fail "decode layer mismatch (${m}-stage)"; echo "$out"; fi
done

for f in benchmarks/*.s; do ./asm_tool "$f" "$tmp/$(basename "${f%.s}").hex" --hex > /dev/null || { fail "assemble $f"; }; done
./sim --compare "$tmp"/bench_*.hex --csv "$tmp/h.csv"             > "$tmp/h.txt"; hrc=$?
./sim --compare "$tmp"/bench_*.hex --csv "$tmp/v.csv" --vertical  > "$tmp/v.txt"; vrc=$?
[ $hrc -eq 0 ] && pass "horizontal compare run OK" || fail "horizontal compare run"
[ $vrc -eq 0 ] && pass "vertical compare run OK (all results identical on both pipelines)" || { fail "vertical compare run"; cat "$tmp/v.txt"; }
if diff -q "$tmp/h.csv" "$tmp/v.csv" > /dev/null; then pass "vertical CSV (cycles, CPI, stalls, flushes, forwards) identical to horizontal"
else fail "vertical results differ from horizontal"; diff "$tmp/h.csv" "$tmp/v.csv" | head; fi

out=$(printf 'cu v\nload %s\ncontinue\nregs\nucontrol vertical\nquit\n' "$tmp/bench_sum.hex" | ./sim)
echo "$out" | grep -q "VERTICAL control unit" && echo "$out" | grep -q "Status: HALTED (clean)" && echo "$out" | grep -q "r2 : 0x000013ba" \
    && pass "debugger: cu v + bench_sum -> 5050" || { fail "debugger run on vertical unit"; echo "$out" | tail -20; }
echo "$out" | grep -q "Vertical Control Store ROM" && pass "debugger: ucontrol vertical dump" || fail "ucontrol vertical dump"

echo; echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
