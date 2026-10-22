#!/bin/bash
# Round-trip test: assemble -> disassemble -> assemble again.
# The two assembled results must be identical. (The disassembler invents label
# names and expands push/pop, so the TEXT can never match the original; the
# machine code is what has to match.)
# From the project root:  bash tests/disassembler_roundtrip.sh

cd "$(dirname "$0")/.." || exit 1

if [ ! -x ./asm_tool ] || [ ! -x ./disassembler.exe ]; then
    echo "Build first with: make"
    exit 1
fi

echo "Running Disassembler Round-Trip Test..."

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
passed=0
failed=0

for src in programs/*.s benchmarks/*.s; do
    name=$(basename "$src" .s)

    if ! ./asm_tool "$src" "$tmp/$name.1.hex" --hex > /dev/null; then
        echo "[FAIL] $name (assembler error on original)"
        failed=$((failed + 1)); continue
    fi
    if ! ./disassembler.exe "$tmp/$name.1.hex" "$tmp/$name.re.s" --hex > /dev/null; then
        echo "[FAIL] $name (disassembler error)"
        failed=$((failed + 1)); continue
    fi
    if ! ./asm_tool "$tmp/$name.re.s" "$tmp/$name.2.hex" --hex > /dev/null; then
        echo "[FAIL] $name (could not re-assemble disassembler output)"
        failed=$((failed + 1)); continue
    fi

    if cmp -s "$tmp/$name.1.hex" "$tmp/$name.2.hex"; then
        echo "[PASS] $name"
        passed=$((passed + 1))
    else
        echo "[FAIL] $name (machine code differs after round trip)"
        failed=$((failed + 1))
    fi
done

# A word that is not a valid instruction must survive the trip via .word
./disassembler.exe tests/invalid_opcode.hex "$tmp/inv.s" --hex > /dev/null &&
./asm_tool "$tmp/inv.s" "$tmp/inv.hex" --hex > /dev/null &&
[ "$(tr 'A-F' 'a-f' < tests/invalid_opcode.hex)" = "$(tr 'A-F' 'a-f' < "$tmp/inv.hex")" ]
if [ $? -eq 0 ]; then
    echo "[PASS] invalid_opcode (.word round trip)"
    passed=$((passed + 1))
else
    echo "[FAIL] invalid_opcode (.word round trip)"
    failed=$((failed + 1))
fi

echo
echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
