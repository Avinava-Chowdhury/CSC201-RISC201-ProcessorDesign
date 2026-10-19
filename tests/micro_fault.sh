#!/bin/bash
echo "Testing Micro-level Fault (INVALID_MICROPC)..."

echo "0x0C480003" > temp_fault.hex

echo -e "setreg upc 255\nstep\nquit" | ./sim temp_fault.hex > fault_output.txt

if grep -q "INVALID_MICROPC" fault_output.txt; then
    echo "PASS: Micro-level fault successfully triggered and caught."
    rm temp_fault.hex fault_output.txt
    exit 0
else
    echo "FAIL: Fault not detected."
    cat fault_output.txt
    exit 1
fi
