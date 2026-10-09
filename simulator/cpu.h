#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "alu.h"
#include "isa.h"
#include "sim_exception.h"

// HORIZONTAL MICROINSTRUCTION (The Control Word)
enum class BranchControl : uint8_t {  // type of control flow
    NONE, 
    COND_EQ,  
    COND_GT,  
    UNCOND,   
    CALL,    
    RET       
};

struct MicroInstruction {
    bool regWrite;      
    bool memRead;       
    bool memWrite;      
    bool updateFlags;   
    
    BranchControl branchType;
    risc201::OperationCode aluOp; 
};

// PIPELINE LATCHES to store intermediate values at each stage
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

// PIPELINED CPU CLASS
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

    // Checked data memory access (throws SimException on a bad address)
    void checkWordAccess(uint32_t addr, uint32_t faultPc) const;
    uint32_t readWord(uint32_t addr, uint32_t faultPc) const;
    void writeWord(uint32_t addr, uint32_t value, uint32_t faultPc);
    
    // Stack safety checks (throw SimException when something is wrong)
    void checkStackPointer(uint32_t newSP, uint32_t faultPc) const;

    // Pipeline stages 
    void do_MEM_WB();
    void do_EX();
    void do_ID();
    void do_IF();
};
