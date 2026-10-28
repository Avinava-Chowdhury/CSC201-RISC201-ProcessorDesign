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
    // 'function' is only used by Opcode::FPU (0=fadd 1=fsub 2=fmul 3=fdiv); every other opcode ignores it,
    // so all existing 3-argument calls keep working unchanged.
    ALUResult execute(Opcode op, int32_t operand1, int32_t operand2, uint8_t function = 0);

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
    
    // Floating-point unit (IEEE-754 binary32, round-to-nearest-even, built from integer logic only).
    // Flags: zero = result is +/-0.0, overflow = IEEE overflow / invalid operation / divide-by-zero
    // happened (no trap is raised for it; the result is inf or NaN as IEEE-754 says), carry = unused.
    ALUResult computeFloat(uint8_t function, uint32_t a, uint32_t b);
    uint32_t  fpAdd(uint32_t a, uint32_t b, bool& exception);
    uint32_t  fpMul(uint32_t a, uint32_t b, bool& exception);
    uint32_t  fpDiv(uint32_t a, uint32_t b, bool& exception);
    uint32_t  fpRoundPack(bool sign, int exponent, uint64_t mantissa, bool& exception);
    uint64_t  add64(uint64_t a, uint64_t b);   // 64-bit add built from two 32-bit CLA adders
    uint64_t  sub64(uint64_t a, uint64_t b);   // 64-bit subtract (a + ~b + 1) from two CLA adders
};

} // namespace risc201
