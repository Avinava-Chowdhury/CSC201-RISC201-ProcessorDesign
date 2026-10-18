#pragma once

#include <cstdint>
#include <string>
#include "isa.h"

// Forward declaration of PipelineMode (which is defined in cpu.h)
enum class PipelineMode;

// Moved from cpu.h
enum class BranchControl : uint8_t {  // type of control flow
    NONE, 
    COND_EQ,  
    COND_GT,  
    UNCOND,   
    CALL,    
    RET       
};

enum class MicroStage : uint8_t { IF, ID, RR, EX, MEM, WB, MEMWB };
enum class Seq : uint8_t { NEXT, END };       // sequencer field

struct MicroWord {
    // HORIZONTAL: one bit/field per control line
    bool valid = false;
    MicroStage stage = MicroStage::ID;
    Seq seq = Seq::NEXT;
    
    // register-read controls (ID in 4-stage, RR in 6-stage)
    bool readRs1=false, readRs2=false, readRd=false, readRa=false, unaryOp=false;
    
    // EX controls
    bool aluEnable=false, updateFlags=false, trapDivZero=false, trapOverflow=false;
    bool linkPC=false, destIsRA=false, stopFetch=false;
    risc201::OperationCode aluOp = risc201::OperationCode::ADD;
    BranchControl branch = BranchControl::NONE;
    
    // MEM / WB controls
    bool memRead=false, memWrite=false, regWrite=false, halt=false;
};

constexpr size_t CONTROL_STORE_SIZE = 256;    // µPC is uint8_t

// Utility function declarations
void buildControlStore(PipelineMode mode, MicroWord rom[], uint8_t dispatch[32]);
std::string validateMicroWord(const MicroWord& word, MicroStage stage);
std::string describe(const MicroWord& word);
