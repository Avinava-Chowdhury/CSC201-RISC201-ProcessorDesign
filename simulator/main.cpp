#include "cpu.h"
#include "sim_exception.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>

int main() {
    CPU cpu;
    cpu.reset();
    
    std::cout << "RISC201 Functional Simulator\n";
    std::cout << "Type 'help' for commands or 'quit' to exit.\n";

    std::string line;
    while (true) {
        std::cout << "\n(sim) > ";
        if (!std::getline(std::cin, line)) break;

        std::stringstream ss(line);
        std::string command;
        ss >> command;

        if (command.empty()) continue;

        if (command == "quit") {
            break;
        } 
        else if (command == "load") {
            std::string filename;
            if (ss >> filename) {
                if (cpu.loadHex(filename)) {
                    std::cout << "Successfully loaded " << filename << "\n";
                } else {
                    std::cout << "Error: Could not open file " << filename << "\n";
                }
            } else {
                std::cout << "Usage: load <file.hex>\n";
            }
        } 
        else if (command == "reset") {
            cpu.reset();
            std::cout << "CPU reset to initial state.\n";
        } 
        else if (command == "step") {
            int steps = 1;
            std::string arg;
            if (ss >> arg) {
                steps = std::stoi(arg); // Handle optional [n] argument
            }
            
            try {
                for (int i = 0; i < steps; i++) {
                    cpu.stepInstruction();
                }
                std::cout << "Stepped " << steps << " instruction(s). PC is now 0x" 
                          << std::hex << cpu.pc << std::dec << "\n";
            } catch (const SimException& e) {
                std::cout << "\n" << e.what() << "\n";
            }
        } 
        else if (command == "regs") {
            std::cout << "--- Processor State ---\n";
            std::cout << "PC: 0x" << std::hex << cpu.pc << std::dec << "\n";
            for (int i = 0; i < 16; i++) {
                // Print registers in a clean grid
                std::cout << "r" << std::left << std::setw(2) << i << ": 0x" 
                          << std::hex << std::setfill('0') << std::setw(8) << cpu.reg[i] 
                          << std::dec << std::setfill(' ') << "\t";
                if ((i + 1) % 4 == 0) std::cout << "\n";
            }
            std::cout << "Flags -> E: " << cpu.flags.IsEqual << " | GT: " << cpu.flags.IsGreaterThan << "\n";
        } 
        else if (command == "help") {
            std::cout << "Commands:\n"
                      << "  load <file.hex>  - Load program into memory\n"
                      << "  step [n]         - Run n instructions (default 1)\n"
                      << "  regs             - Show all 16 registers, PC, and flags\n"
                      << "  reset            - Reset CPU state\n"
                      << "  quit             - Exit simulator\n";
        } 
        else {
            std::cout << "Unknown command: " << command << "\n";
        }
    }

    return 0;
}