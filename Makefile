CXX=g++
FLAGS=-std=c++20 -Iisa -Ialu -Iassembler -Isimulator -Idebugger

SIM=simulator/main.cpp simulator/cpu.cpp simulator/horizontal_microcode.cpp alu/ALU.cpp isa/ISA.cpp
ASM=assembler/main.cpp assembler/assembler.cpp assembler/pass1.cpp assembler/pass2.cpp assembler/tokenizer.cpp isa/ISA.cpp

all: sim asm_tool

sim: $(SIM) $(wildcard simulator/*.h alu/*.h isa/*.h debugger/*.h)
	$(CXX) $(FLAGS) $(SIM) -o sim

asm_tool: $(ASM) $(wildcard assembler/*.h isa/*.h)
	$(CXX) $(FLAGS) $(ASM) -o asm_tool

# Automatically assemble to binary, then convert to clean text .hex format
programs/%.hex: programs/%.s asm_tool
	./asm_tool programs/$*.s programs/$*.bin
	python3 -c "import sys; \
data = open(sys.argv[1], 'rb').read(); \
words = [int.from_bytes(data[i:i+4], 'little') for i in range(0, len(data), 4)]; \
open(sys.argv[2], 'w').write('\n'.join(f'0x{w:08x}' for w in words) + '\n')" programs/$*.bin programs/$*.hex
	rm -f programs/$*.bin

clean:
	rm -f sim asm_tool programs/*.bin

test: sim asm_tool
	bash tests/stack_faults.sh
	bash tests/micro_tests.sh
