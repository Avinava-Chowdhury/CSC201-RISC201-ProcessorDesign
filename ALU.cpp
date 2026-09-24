#include "alu.h"
#include 
using namespace std;
using namespace risc201;

// Yaha kuch nahi karna hai, code likhne ki jagah niche hai.

ALUResult execute(Opcode op, int32_t operand1, int32_t operand2) 
{
   switch (op) 
   {
        // Seema's operations
        case Opcode::ADD:
        case Opcode::SUB:
        case Opcode::MUL:
        case Opcode::DIV:
        case Opcode::MOD:
            return computeArithmetic(op, operand1, operand2);

        // Khushi's operations
        case Opcode::AND:
        case Opcode::OR:
        case Opcode::NOT:
        case Opcode::MOV:
        case Opcode::CMP:
            return computeLogical(op, operand1, operand2);

        // Saanvi's operations
        case Opcode::LSL:
        case Opcode::LSR:
        case Opcode::ASR:
            return computeShift(op, operand1, operand2);

        // Iss part ko mein (Avinava) baadme dekh lunga, kisiko kuch karne ki zarurat nahi hai.
        // Non-ALU instructions (Loads, Stores, Branches)
        case Opcode::NOP:
        case Opcode::LD:
        case Opcode::ST:
        case Opcode::BEQ:
        case Opcode::BGT:
        case Opcode::B:
        case Opcode::CALL:
        case Opcode::RET:
        {
            // These don't use the ALU for math in the same way. Return a safe empty object.
            ALUResult empty;
            empty.value = 0;
            empty.flags.zero = false;
            empty.flags.carry = false;
            empty.flags.overflow = false;
            return empty;
        }
        default:
            throw std::invalid_argument("ALU execute: Unknown opcode");
    }
}


// Seema Ji, Jo jo arithmetic.cpp me kar rakha hai, yaha kar dena, aur us file ko delete kar dena

ALUResult computeArithmetic(Opcode op, int32_t a, int32_t b) 
{
    ALUResult result;
    result.value = 0;
    result.flags.zero = false;
    result.flags.carry = false;
    result.flags.overflow = false;
    switch (op) 
    {
        case Opcode::ADD:
            // TODO (Seema): Implement ADD.
            // Example: result.value = a + b;
            // Also calculate carry/overflow flags if required.
            break;
      
        case Opcode::SUB:
            // TODO (Seema): Implement SUB.
            break;
      
        case Opcode::MUL:
            // TODO (Seema): Implement MUL.
            break;
      
        case Opcode::DIV:
            // TODO (Seema): Implement DIV.
            break;
      
        case Opcode::MOD:
            // TODO (Seema): Implement MOD.
            break;
      
        default:
            break;
    }
    
    // TODO (Seema): Set result.flags.zero = true if result.value is exactly 0.
    return result;
}

// KHUSHI Ji: Logical Operations

ALUResult computeLogical(Opcode op, int32_t a, int32_t b) 
{
    ALUResult result;
    result.value = 0;
    result.flags.zero = false;
    result.flags.carry = false;      // Not used in logic ops
    result.flags.overflow = false;   // Not used in logic ops
    switch (op) 
    {
        case Opcode::AND:
            // TODO (Khushi): Implement bitwise AND.
            break;
      
        case Opcode::OR:
            // TODO (Khushi): Implement bitwise OR.
            break;
      
        case Opcode::NOT:
            // TODO (Khushi): Implement bitwise NOT on operand 'a'.
            break;
      
        case Opcode::MOV:
            // TODO (Khushi): Implement MOV (usually result.value = b or a, depending on setup).
            break;
      
        case Opcode::CMP:
            // TODO (Khushi): CMP does not output a number to a register, 
            // but you should subtract 'b' from 'a' just to check if the result is zero.
            // (Note: The actual GT and E flags are handled in isa.h, so just handle 'zero' here).
            break;
      
        default:
            break;
    }

    // TODO (Khushi): Set result.flags.zero = true if result.value is exactly 0.
    return result;
}


// SAANVI ji: Shift Operations

ALUResult computeShift(Opcode op, int32_t a, int32_t b) 
{
    ALUResult result;
    result.value = 0;
    result.flags.zero = false;
    result.flags.carry = false;      // Not needed for simple shifts
    result.flags.overflow = false;   // Not needed for shifts

    switch (op) 
    {
        case Opcode::LSL:
            // TODO (Saanvi): Implement Logical Shift Left using <<
            break;
      
        case Opcode::LSR:
            // TODO (Saanvi): Implement Logical Shift Right.
            // Hint: cast 'a' to an unsigned integer (uint32_t) before shifting (>>) 
            // so it doesn't drag the sign bit with it.
            break;
      
        case Opcode::ASR:
            // TODO (Saanvi): Implement Arithmetic Shift Right.
            // Hint: a standard shift (>>) on a signed int32_t in C++ automatically preserves the sign.
            break;
      
        default:
            break;
    }

    // TODO (Saanvi): Set result.flags.zero = true if result.value is exactly 0.
    return result;
}
