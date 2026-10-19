#!/bin/bash
# Checks the benchmark programs and the pipeline diagram / comparison tools.
# From the project root:  make test   (or: bash tests/benchmark_tests.sh)
#
#  1. every benchmark computes the right answer on BOTH the 4-stage and the 6-stage pipeline
#  2. the pipeline diagram agrees with the simulator: the number of instructions that reach
#     the last stage in the recorded timeline equals the 'Retired' counter
#  3. 'sim --compare' runs all benchmarks and reports identical results for both pipelines

cd "$(dirname "$0")/.." || exit 1
if [ ! -x ./sim ] || [ ! -x ./asm_tool ]; then echo "Build first with: make"; exit 1; fi

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
passed=0; failed=0
pass() { echo "[PASS] $1"; passed=$((passed + 1)); }
fail() { echo "[FAIL] $1"; failed=$((failed + 1)); }

# check <program> <register line that must appear, e.g. "r2 : 0x000013ba">
check() {
    prog=$1; want=$2
    ./asm_tool "benchmarks/$prog.s" "$tmp/$prog.hex" --hex > /dev/null || { fail "$prog (assembler error)"; return; }
    for mode in 4 6; do
        out=$(printf 'mode %s\nload %s\ncontinue\nregs\nstats\ntimeline csv %s\nquit\n' "$mode" "$tmp/$prog.hex" "$tmp/$prog.$mode.csv" | ./sim)
        if ! echo "$out" | grep -q "Status: HALTED (clean)"; then fail "$prog on ${mode}-stage (did not halt cleanly)"; continue; fi
        if ! echo "$out" | grep -q -- "$want"; then fail "$prog on ${mode}-stage (expected $want)"; continue; fi

        retired=$(echo "$out" | awk '/^Retired/ {print $3; exit}')
        csv="$tmp/$prog.$mode.csv"
        reached=$(tail -n +2 "$csv" | awk -F, '{ if ($NF != "" && $NF !~ /^x/) n++ } END {print n+0}')
        if [ "$retired" != "$reached" ]; then fail "$prog on ${mode}-stage (diagram shows $reached retirements, simulator says $retired)"; continue; fi
        pass "$prog on ${mode}-stage"
    done
}

check bench_sum       "r2 : 0x000013ba"      # 5050
check bench_fib       "r1 : 0x00001a6d"      # 6765
check bench_factorial "r2 : 0x00009d80"      # 40320
check bench_loaduse   "r5 : 0x00000120"      # 288
check bench_muldiv    "r2 : 0x00000c86"      # 3206
check bench_gcd       "r1 : 0x00000015"      # 21
check bench_branchy   "r2 : 0x00000014"      # 20
check bench_stack     "r2 : 0x0000004e"      # 78
check bench_rawchain  "r1 : 0x04000003"      # 67108867

# the comparison tool must run every benchmark, finish with HALT on both pipelines and agree
for f in benchmarks/*.s; do ./asm_tool "$f" "$tmp/$(basename "${f%.s}").hex" --hex > /dev/null; done
if ./sim --compare "$tmp"/bench_*.hex --csv "$tmp/cmp.csv" > "$tmp/cmp.txt"; then pass "sim --compare (all results identical)"; else fail "sim --compare"; cat "$tmp/cmp.txt"; fi

echo; echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
