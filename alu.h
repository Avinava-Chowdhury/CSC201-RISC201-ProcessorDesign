#pragma once
#include 
#include "isa.h"
using namespace std;
using namespace risc201;
class ALUFlag
{
   public:
       bool zero;      // true if the result is exactly 0
       bool carry;     // true if ADD/SUB produced a carry/borrow out of bit 31
       bool overflow;  // true if a *signed* ADD/SUB overflowed (result has the wrong sign)
};
class ALUResult 
{
   public:
       int32_t  value;
       ALUFlag flags;
};
//dispatcher function
ALUResult execute(Opcode op, int32_t operand1, int32_t operand2);
//Helper functions
ALUResult computeArithmetic(Opcode op, int32_t a, int32_t b); // Seema Ji
ALUResult computeLogical(Opcode op, int32_t a, int32_t b);    // Khushi Ji
ALUResult computeShift(Opcode op, int32_t a, int32_t b);      // Saanvi Ji
