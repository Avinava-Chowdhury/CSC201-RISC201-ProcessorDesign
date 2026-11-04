
CXX = g++
# Optimize both prediction modes equally; never enable fast-math for the software FPU.
CXXFLAGS ?= -O2
FLAGS = -std=c++20 -Iisa -Ialu -Iassembler -Isimulator -Idebugger -Idisassembler

SIM = simulator/main.cpp \
      simulator/cpu.cpp \
      simulator/branch_predictor.cpp \
      simulator/horizontal_microcode.cpp \
      simulator/vertical_microcode.cpp \
      alu/ALU.cpp \
      isa/ISA.cpp

ASM = assembler/main.cpp \
      assembler/assembler.cpp \
      assembler/pass1.cpp \
      assembler/pass2.cpp \
      assembler/tokenizer.cpp \
      isa/ISA.cpp

DISASM = disassembler/main.cpp \
         disassembler/disassembler.cpp \
         isa/ISA.cpp

.PHONY: all clean test test-branch

all: sim asm_tool disassembler.exe

# Build the processor simulator
sim: Makefile $(SIM) $(wildcard simulator/*.h alu/*.h isa/*.h debugger/*.h)
	$(CXX) $(CXXFLAGS) $(FLAGS) $(SIM) -o sim

# Build the assembler
asm_tool: Makefile $(ASM) $(wildcard assembler/*.h isa/*.h)
	$(CXX) $(CXXFLAGS) $(FLAGS) $(ASM) -o asm_tool

# Build the disassembler
disassembler.exe: Makefile $(DISASM) $(wildcard disassembler/*.h isa/*.h)
	$(CXX) $(CXXFLAGS) $(FLAGS) $(DISASM) -o disassembler.exe

# Assemble a .s program and convert its binary output to text hex
programs/%.hex: programs/%.s asm_tool
	./asm_tool programs/$*.s programs/$*.bin
	python3 -c "import sys; \
data = open(sys.argv[1], 'rb').read(); \
words = [int.from_bytes(data[i:i+4], 'little') for i in range(0, len(data), 4)]; \
open(sys.argv[2], 'w').write('\n'.join(f'0x{w:08x}' for w in words) + '\n')" programs/$*.bin programs/$*.hex
	rm -f programs/$*.bin

# Remove generated executables and intermediate binaries
clean:
	rm -f sim sim.exe asm_tool asm_tool.exe disassembler.exe branch_tests branch_tests.exe programs/*.bin

# Run automated tests
test: all
	bash tests/stack_faults.sh
	bash tests/vertical_tests.sh
	bash tests/disassembler_roundtrip.sh
	bash tests/fpu_tests.sh

# Portable C++ predictor and CPU regression suite (no Python dependency).
test-branch: all
	$(CXX) $(CXXFLAGS) $(FLAGS) tests/branch_predictor_tests.cpp simulator/cpu.cpp simulator/branch_predictor.cpp simulator/horizontal_microcode.cpp simulator/vertical_microcode.cpp alu/ALU.cpp isa/ISA.cpp -o branch_tests
	./branch_tests
