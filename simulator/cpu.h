#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "alu.h"
#include "isa.h"

// ---------------------------------------------------------
// HORIZONTAL MICROINSTRUCTION (The Control Word)
// ---------------------------------------------------------
enum class BranchControl : uint8_t { 
    NONE, 
    COND_EQ,  // BEQ uses IsEqual flag
    COND_GT,  // BGT uses IsGreaterThan flag
    UNCOND,   // B
    CALL,     // Save PC to r15, then jump
    RET       // Jump to r15
};

struct MicroInstruction {
    bool regWrite;      // Write result back to the destination register?
    bool memRead;       // Read data from memory into a register?
    bool memWrite;      // Write data from a register into memory?
    bool updateFlags;   // Update ProcessorFlags (IsEqual, IsGreaterThan)?
    
    BranchControl branchType;
    risc201::OperationCode aluOp; 
};

// ---------------------------------------------------------
// PIPELINE LATCHES
// ---------------------------------------------------------
struct IF_ID_Latch {
    bool active = false; 
    uint32_t pc = 0;
    uint32_t instruction = 0;
};

struct ID_EX_Latch {
    bool active = false; 
    uint32_t pc = 0;
    
    risc201::DecodedInstruction decoded; // Carries rd, rs1, rs2, constants
    int32_t val1 = 0;                    // Read from register file (rs1)
    int32_t val2 = 0;                    // Read from register file (rs2) OR immediate
    
    MicroInstruction ctrl;               // The control word traveling with the instruction
};

struct EX_MEMWB_Latch {
    bool active = false; 
    uint32_t pc = 0;            
    uint8_t dest_reg = 0;
    int32_t alu_result = 0;
    int32_t store_data = 0;              // Raw value of rs1 to store (for ST)
    
    MicroInstruction ctrl;               // Surviving control signals (regWrite, memRead, memWrite)
};

// ---------------------------------------------------------
// PIPELINED CPU CLASS
// ---------------------------------------------------------
class CPU {
public:
    std::vector<uint32_t> instrMem; // 4096 words (16 KB)
    std::vector<uint8_t>  dataMem;  // 65536 bytes (64 KB)
    
    // Processor state
    int32_t reg[16]; 
    uint32_t pc; 
    risc201::ProcessorFlags flags;

    // Pipeline Registers
    IF_ID_Latch if_id;
    ID_EX_Latch id_ex;
    EX_MEMWB_Latch ex_memwb;
    
    // Microprogrammed Control Store
    static constexpr size_t CONTROL_STORE_SIZE = 64; 
    MicroInstruction controlStore[CONTROL_STORE_SIZE];
    
    risc201::ALU alu; // Isolated ALU class

    CPU(); 

    void reset();
    bool loadHex(const std::string& path);
    void loadControlStore(); // Maps opcodes to microinstructions at startup
    
    void tick(); 
    void stepInstruction();  // Wrapper that calls tick() to stay compatible with main.cpp
    
    // Pipeline stages (executed MEM_WB -> EX -> ID -> IF to prevent overwriting)
    void do_MEM_WB();
    void do_EX();
    void do_ID();
    void do_IF();
};