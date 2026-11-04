#pragma once
#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
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

// Two implementations per family. Restoring defaults are the optimized versions.
enum class AddAlgorithm { LOOKAHEAD, RIPPLE };
enum class MulAlgorithm { BOOTH, ITERATIVE };
enum class DivAlgorithm { RESTORING, NON_RESTORING };
enum class FpDivAlgorithm { SIMPLE, NEWTON };
struct ALUConfig {
    AddAlgorithm add = AddAlgorithm::LOOKAHEAD;
    MulAlgorithm mul = MulAlgorithm::BOOTH;
    DivAlgorithm div = DivAlgorithm::RESTORING;
    MulAlgorithm fpMul = MulAlgorithm::ITERATIVE;
    FpDivAlgorithm fpDiv = FpDivAlgorithm::SIMPLE;
};
struct ALUTiming { uint64_t calls = 0, totalNs = 0, lastNs = 0; };

// 3. The formal ALU Class (Assignment Requirement)
class ALU 
{
public:
    // The public interface the EX stage will call
    // 'function' is only used by Opcode::FPU (0=fadd 1=fsub 2=fmul 3=fdiv); every other opcode ignores it,
    // so all existing 3-argument calls keep working unchanged.
    ALUResult execute(Opcode op, int32_t operand1, int32_t operand2, uint8_t function = 0);

    ALUConfig config;
    bool timingEnabled = true;
    bool selectAlgorithm(const std::string& family, const std::string& name);
    void printAlgorithms(std::ostream& out) const;
    void printTiming(std::ostream& out) const;
    void clearTiming() { timings.clear(); }
    bool compareAlgorithms(std::ostream& out, const std::string& operation,
                           int32_t a, int32_t b, unsigned repeats = 1000);

private:
    std::map<std::string, ALUTiming> timings;
    ALUResult executeCore(Opcode op, int32_t a, int32_t b, uint8_t function);
    std::string algorithmLabel(Opcode op, uint8_t function) const;
    // Internal helper structures (used by the CLA implementation)
    struct AdderOutput 
    {
        uint32_t sum;
        bool carryOut;
    };

    // Low-level mathematical primitives
    AdderOutput carryLookaheadAdd(uint32_t a, uint32_t b, bool carryIn);
    AdderOutput addSelected(uint32_t a, uint32_t b, bool carryIn);
    AdderOutput rippleAdd(uint32_t a, uint32_t b, bool carryIn);
    uint64_t boothProduct(int32_t a, int32_t b);
    uint64_t iterativeProduct(int32_t a, int32_t b);
    uint64_t multiplyUnsigned32(uint32_t a, uint32_t b, MulAlgorithm algorithm);
    void nonRestoringDivide(uint32_t a, uint32_t b, uint32_t& q, uint32_t& r);
    uint32_t reciprocalQ31(uint32_t mantissa);
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
    uint64_t  add64(uint64_t a, uint64_t b);   // 64-bit add built from two selected 32-bit adders
    uint64_t  sub64(uint64_t a, uint64_t b);   // 64-bit subtract from two selected 32-bit adders
};

} // namespace risc201

// Interface support is separate from the arithmetic implementations.
#include "alu_tools.h"
