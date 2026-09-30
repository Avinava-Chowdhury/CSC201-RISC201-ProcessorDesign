#include "cpu.h"
#include <fstream>
#include <string>

//CPU register file, memories, reset
CPU::CPU() {
    instrMem.assign(4096, 0); 
    dataMem.assign(65536, 0); 
}

void CPU::reset() {
    for (int i = 0; i < 16; i++) {
        reg[i] = 0;
    }
    // sp (r14) starts at 0x10000[cite: 8] since stack grows downwards
    reg[risc201::STACK_POINTER_REG_INDEX] = risc201::STACK_STARTING_ADDRESS; 
    pc = 0;
    
    // Clear flags[cite: 8]
    flags.IsEqual = false;
    flags.IsGreaterThan = false;
}

// CPU hex loader[cite: 8]
bool CPU::loadHex(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    uint32_t memIndex = 0;
    
    // Parse "0x...." lines into instrMem[cite: 8]
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        
        // Adding a boundary safeguard here to prevent vector overflow
        if (memIndex >= 4096) break; 
        
        uint32_t instruction = std::stoul(line, nullptr, 16);
        instrMem[memIndex++] = instruction;
    }
    return true;
}

void CPU::stepInstruction() {
    // PHASE 1: FETCH
    // PC holds the byte address. Divide by 4 to get the word index for instrMem.
    uint32_t rawInstruction = instrMem[pc / risc201::BYTES_PER_WORD];

    // PHASE 2: DECODE 
    auto decodedOpt = risc201::convertFromBinary(rawInstruction); // isa's cfb acts as decoder unit adds 4 to pc
    if (!decodedOpt.has_value()) {
        //exception logic
        return;
    }
    risc201::DecodedInstruction instr = decodedOpt.value();

    // Advance PC by 4 bytes (1 word) for the next cycle. 
    // Branch instructions in Phase 4 might overwrite this.
    pc += risc201::BYTES_PER_WORD;

    // PHASE 3: READ OPERANDS
    int32_t operand1 = 0;
    int32_t operand2 = 0;

    // Rule: Single-argument operations (MOV, NOT, NEG, ABS) 
    // These read their input directly into operand1.
    if (instr.operation == risc201::OperationCode::MOV || 
        instr.operation == risc201::OperationCode::NOT ||
        instr.operation == risc201::OperationCode::NEG ||
        instr.operation == risc201::OperationCode::ABS) {
        
        operand1 = instr.usesConstantNumber ? instr.calculateFinalConstantValue() : reg[instr.sourceRegister1];
    } 
    // Rule: Three-argument operations (ADD, SUB, CMP, etc.)
    else {
        operand1 = reg[instr.sourceRegister1]; // Always reads the first register
        operand2 = instr.usesConstantNumber ? instr.calculateFinalConstantValue() : reg[instr.sourceRegister2]; // Second input is either a constant or the second register[cite: 3]
    }

    // PHASE 4: EXECUTE
    int32_t aluResult = 0; 

    switch (instr.operation) {
        
        // 1. ALU Operations 
        case risc201::OperationCode::ADD:
            aluResult = operand1 + operand2;
            break;
            
        case risc201::OperationCode::SUB:
            aluResult = operand1 - operand2;
            break;
            
        // 2. State Operations 
        case risc201::OperationCode::CMP:
            flags.IsEqual = (operand1 == operand2);
            flags.IsGreaterThan = (operand1 > operand2);
            break;

        // 3. Branching Operations 
        case risc201::OperationCode::B:
            // Unconditional branch: Add the jump distance to the PC
            pc += instr.branchJumpDistance; 
            break;
            
        case risc201::OperationCode::BEQ:
            // Branch if Equal: Only jump if the last CMP set IsEqual to true
            if (flags.IsEqual) {
                pc += instr.branchJumpDistance;
            }
            break;

        // 4. Memory Operations 
        case risc201::OperationCode::LD:
            // coming up
            break;
            
        case risc201::OperationCode::ST:
            // coming up
            break;

        default:
            break;
    }

    // PHASE 5: WRITEBACK
    // check if this operation writes to a register.
    if (risc201::modifiesDestinationRegister(instr.operation)) {
        reg[instr.destinationRegister] = aluResult;
    }

}