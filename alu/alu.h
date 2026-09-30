#pragma once
#include <cstdint>
#include "../isa/isa.h"

using Opcode = risc201::OperationCode;

namespace risc201 
{

// 1. The Flags Struct
class ALUFlag 
{
public:
    bool zero;      
    bool carry;     
    bool overflow;  
};

// 2. The Result Struct
class ALUResult 
{
public:
    int32_t  value;
    ALUFlag flags;
};

// 3. The formal ALU Class (Assignment Requirement)
class ALU 
{
public:
    // The public interface the EX stage will call
    ALUResult execute(Opcode op, int32_t operand1, int32_t operand2);

private:
    // Internal helper structures (used by the CLA implementation)
    struct AdderOutput 
    {
        uint32_t sum;
        bool carryOut;
    };

    // Low-level mathematical primitives
    AdderOutput carryLookaheadAdd(uint32_t a, uint32_t b, bool carryIn);
    uint32_t negateCLA(uint32_t x);
    void restoringDivide(uint32_t dividend, uint32_t divisor, uint32_t& quotient, uint32_t& remainder);

    // Operation group dispatchers (Written by the team)
    ALUResult computeArithmetic(Opcode op, int32_t a, int32_t b);
    ALUResult computeLogical(Opcode op, int32_t a, int32_t b);    
    ALUResult computeShift(Opcode op, int32_t a, int32_t b);      
};

} // namespace risc201