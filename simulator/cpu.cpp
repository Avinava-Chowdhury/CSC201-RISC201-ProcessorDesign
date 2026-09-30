#include "cpu.h"
#include "sim_exception.h"
#include <fstream>
#include <string>
#include <cmath> // Added for std::abs

//CPU register file, memories, reset
CPU::CPU() {
    instrMem.assign(4096, 0); 
    dataMem.assign(65536, 0); 
}

void CPU::reset() {
    for (int i = 0; i < 16; i++) {
        reg[i] = 0;
    }
    // sp (r14) starts at 0x10000 since stack grows downwards
    reg[risc201::STACK_POINTER_REG_INDEX] = risc201::STACK_STARTING_ADDRESS; 
    pc = 0;
    
    // Clear flags
    flags.IsEqual = false;
    flags.IsGreaterThan = false;
}

// CPU hex loader
bool CPU::loadHex(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    uint32_t memIndex = 0;
    
    // Parse "0x...." lines into instrMem
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
        throw SimException("Hardware Crash: Invalid opcode detected at PC " + std::to_string(pc));
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
        operand2 = instr.usesConstantNumber ? instr.calculateFinalConstantValue() : reg[instr.sourceRegister2]; // Second input is either a constant or the second register
    }

    // PHASE 4: EXECUTE
    int32_t aluResult = 0; 

    switch (instr.operation) {
        
        // 1. Single-Argument ALU Operations (Previously Missing)
        case risc201::OperationCode::MOV:
            aluResult = operand1;
            break;
            
        case risc201::OperationCode::NOT:
            aluResult = ~operand1;
            break;
            
        case risc201::OperationCode::NEG:
            aluResult = -operand1;
            break;
            
        case risc201::OperationCode::ABS:
            aluResult = std::abs(operand1);
            break;

        // 2. Two-Argument ALU Operations
        case risc201::OperationCode::ADD:
            aluResult = operand1 + operand2;
            break;
            
        case risc201::OperationCode::SUB:
            aluResult = operand1 - operand2;
            break;
            
        case risc201::OperationCode::MUL:
            aluResult = operand1 * operand2;
            break;
            
        case risc201::OperationCode::DIV:
            if (operand2 == 0) throw SimException("Hardware Crash: Division by zero");
            aluResult = operand1 / operand2;
            break;
            
        case risc201::OperationCode::MOD:
            if (operand2 == 0) throw SimException("Hardware Crash: Modulo by zero");
            aluResult = operand1 % operand2;
            break;

        case risc201::OperationCode::AND:
            aluResult = operand1 & operand2;
            break;

        case risc201::OperationCode::OR:
            aluResult = operand1 | operand2;
            break;

        case risc201::OperationCode::LSL:
            aluResult = operand1 << (operand2 & 31);
            break;

        case risc201::OperationCode::LSR:
            // Cast to unsigned to force logical shift right instead of arithmetic
            aluResult = static_cast<int32_t>(static_cast<uint32_t>(operand1) >> (operand2 & 31));
            break;

        case risc201::OperationCode::ASR:
            aluResult = operand1 >> (operand2 & 31);
            break;

        case risc201::OperationCode::MIN:
            aluResult = (operand1 < operand2) ? operand1 : operand2;
            break;

        case risc201::OperationCode::MAX:
            aluResult = (operand1 > operand2) ? operand1 : operand2;
            break;

        case risc201::OperationCode::ROL:
            aluResult = (operand1 << (operand2 & 31)) | (static_cast<uint32_t>(operand1) >> (32 - (operand2 & 31)));
            break;

        case risc201::OperationCode::ROR:
            aluResult = (static_cast<uint32_t>(operand1) >> (operand2 & 31)) | (operand1 << (32 - (operand2 & 31)));
            break;
            
        // 3. State Operations 
        case risc201::OperationCode::CMP:
            flags.IsEqual = (operand1 == operand2);
            flags.IsGreaterThan = (operand1 > operand2);
            break;

        // 4. Branching Operations 
        case risc201::OperationCode::B:
            // Because Phase 2 already did 'pc += 4', we must subtract 4 before adding the distance.
            pc = (pc - 4) + instr.branchJumpDistance; 
            break;
            
        case risc201::OperationCode::BEQ:
            if (flags.IsEqual) {
                pc = (pc - 4) + instr.branchJumpDistance;
            }
            break;
            
        case risc201::OperationCode::BGT: // Previously Missing
            if (flags.IsGreaterThan) {
                pc = (pc - 4) + instr.branchJumpDistance;
            }
            break;

        case risc201::OperationCode::CALL:
            // 1. Save the return address. 
            // Phase 2 already advanced 'pc' by 4, so it perfectly points to the next instruction.
            // We write this address into the Return Address register (r15).
            reg[risc201::RETURN_ADDRESS_REG_INDEX] = pc;
            
            // 2. Jump to the function (using the same offset correction as above).
            pc = (pc - 4) + instr.branchJumpDistance;
            break;
            
        case risc201::OperationCode::RET:
            // Overwrite the PC with the address we previously saved in r15.
            pc = reg[risc201::RETURN_ADDRESS_REG_INDEX];
            break;

        // 5. Memory Operations 
        case risc201::OperationCode::LD: {
            // Calculate physical memory address
            uint32_t memAddr = reg[instr.sourceRegister1] + instr.calculateFinalConstantValue();
            
            // 65536 is max size. We read 4 bytes, so max address is 65532
            if (memAddr > 65532) {
                throw SimException("Hardware Crash: Out-of-bounds memory access at " + std::to_string(memAddr));
            }
            // Address must be perfectly divisible by 4
            if (memAddr % 4 != 0) {
                throw SimException("Hardware Crash: Misaligned memory access at " + std::to_string(memAddr));
            }
            
            // Read 4 consecutive bytes from dataMem and combine them into a 32-bit word (Little-Endian)
            uint32_t loadedWord = 0;
            loadedWord |= dataMem[memAddr];
            loadedWord |= (dataMem[memAddr + 1] << 8);
            loadedWord |= (dataMem[memAddr + 2] << 16);
            loadedWord |= (dataMem[memAddr + 3] << 24);
            
            aluResult = loadedWord; // Phase 5 will write this into the destination register
            break;
        }
            
        case risc201::OperationCode::ST: {
            // Calculate physical memory address
            uint32_t memAddr = reg[instr.sourceRegister1] + instr.calculateFinalConstantValue();
            
            // 65536 is max size. We read 4 bytes, so max address is 65532
            if (memAddr > 65532) {
                throw SimException("Hardware Crash: Out-of-bounds memory access at " + std::to_string(memAddr));
            }
            // Address must be perfectly divisible by 4
            if (memAddr % 4 != 0) {
                throw SimException("Hardware Crash: Misaligned memory access at " + std::to_string(memAddr));
            }
            
            // The data to save is sitting in the destination register
            uint32_t dataToStore = reg[instr.destinationRegister];
            
            // Break the 32-bit word into 4 bytes and save them to dataMem
            dataMem[memAddr] = dataToStore & 0xFF;
            dataMem[memAddr + 1] = (dataToStore >> 8) & 0xFF;
            dataMem[memAddr + 2] = (dataToStore >> 16) & 0xFF;
            dataMem[memAddr + 3] = (dataToStore >> 24) & 0xFF;
            break;
        }

        default:
            break;
    }

    // PHASE 5: WRITEBACK
    // check if this operation writes to a register.
    if (risc201::modifiesDestinationRegister(instr.operation)) {
        reg[instr.destinationRegister] = aluResult;
    }

    
    // Ensure the stack pointer hasn't violated its physical hardware boundaries.
    uint32_t currentSP = reg[risc201::STACK_POINTER_REG_INDEX];
    if (currentSP < risc201::STACK_LOWEST_LIMIT || currentSP > risc201::STACK_STARTING_ADDRESS) {
        throw SimException("Hardware Crash: Stack pointer out of bounds (Value: " + std::to_string(currentSP) + ")");
    }

}