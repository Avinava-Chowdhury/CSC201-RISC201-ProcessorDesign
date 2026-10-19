#!/bin/bash
echo "Running Disassembler Round-Trip Test..."

# 1. Assemble the source file to binary, then convert to hex format
./asm_tool programs/test.s test.bin
python3 -c "import sys; data = open(sys.argv[1], 'rb').read(); words = [int.from_bytes(data[i:i+4], 'little') for i in range(0, len(data), 4)]; open(sys.argv[2], 'w').write('\n'.join(f'0x{w:08x}' for w in words) + '\n')" test.bin test.hex

# 2. Disassemble the hex back to a new assembly text file
./disassembler.exe test.hex reconstructed.s

# 3. Verify the reconstructed file matches the original, ignoring whitespace/comments
if diff -w programs/test.s reconstructed.s > /dev/null; then
    echo "PASS: Round-trip test successful. Translation is 100% accurate."
    rm test.bin test.hex reconstructed.s
    exit 0
else
    echo "FAIL: The reconstructed assembly does not match the original."
    diff -w programs/test.s reconstructed.s
    exit 1
fi
