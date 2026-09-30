#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "../isa/isa.h"

class CPU {
public:
    
    std::vector<uint32_t> instrMem; // 4096 words (16 KB)
    std::vector<uint8_t> dataMem;   // 65536 bytes (64 KB)
    
    // Processor state
    int32_t reg[16]; 
    uint32_t pc; 
    risc201::ProcessorFlags flags;

    CPU(); // Constructor to initialize sizes

    void reset();
    bool loadHex(const std::string& path);
    void stepInstruction();
};