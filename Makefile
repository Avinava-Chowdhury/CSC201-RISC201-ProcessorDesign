CXX=g++
FLAGS=-std=c++17 -Iisa -Ialu -Iassembler -Isimulator

SIM=simulator/main.cpp simulator/cpu.cpp alu/ALU.cpp isa/ISA.cpp

sim:
	$(CXX) $(FLAGS) $(SIM) -o sim