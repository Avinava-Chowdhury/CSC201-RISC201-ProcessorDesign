#include "alu.h"
#include <cstdint>
#include <climits>
#include <stdexcept>

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
        case Opcode::NEG:
        case Opcode::ABS:
        case Opcode::MIN:
        case Opcode::MAX:
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
        case Opcode::ADD: {
            int64_t wide = (int64_t)a + (int64_t)b;
            int32_t truncated = (int32_t)wide;
            result.value = truncated;
            result.flags.overflow = (wide != (int64_t)truncated);
            result.flags.carry = ((uint64_t)(uint32_t)a + (uint32_t)b) > 0xFFFFFFFFULL;
            break;
        }

        case Opcode::SUB: {
            int64_t wide = (int64_t)a - (int64_t)b;
            int32_t truncated = (int32_t)wide;
            result.value = truncated;
            result.flags.overflow = (wide != (int64_t)truncated);
            result.flags.carry = (uint32_t)a >= (uint32_t)b;
            break;
        }

        case Opcode::MUL: {
            int64_t wide = (int64_t)a * (int64_t)b;
            int32_t truncated = (int32_t)wide;
            result.value = truncated;
            result.flags.overflow = (wide != (int64_t)truncated);
            result.flags.carry = result.flags.overflow;
            break;
        }

        case Opcode::DIV: {
            if (b == 0) {
                result.value = 0;
                result.flags.overflow = true;
                break;
            }
            if (a == INT32_MIN && b == -1) {
                result.value = a;
                result.flags.overflow = true;
                break;
            }
            result.value = a / b;
            break;
        }

        case Opcode::MOD: {
            if (b == 0) {
                result.value = 0;
                result.flags.overflow = true;
                break;
            }
            if (a == INT32_MIN && b == -1) {
                result.value = 0;
                result.flags.overflow = true;
                break;
            }
            result.value = a % b;
            break;
        }

        case Opcode::NEG: {
            if (a == INT32_MIN) {
                result.value = a;
                result.flags.overflow = true;
                break;
            }
            result.value = -a;
            break;
        }

        case Opcode::ABS: {
            if (a == INT32_MIN) {
                result.value = a;
                result.flags.overflow = true;
                break;
            }
            result.value = (a < 0) ? -a : a;
            break;
        }

        case Opcode::MIN: {
            result.value = (a < b) ? a : b;
            break;
        }

        case Opcode::MAX: {
            result.value = (a > b) ? a : b;
            break;
        }

        default:
            break;
    }

    result.flags.zero = (result.value == 0);
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
        case Opcode::AND: {
            int32_t out = 0;
            for (int i = 0; i < 32; i++) {
                int bitA = (a >> i) & 1;        // extract bit i of a
                int bitB = (b >> i) & 1;        // extract bit i of b
                int bitResult = bitA & bitB;    // AND gate for this bit position
                out |= (bitResult << i);        // place result bit back
            }
            result.value = out;
            break;
        }
      
        case Opcode::OR: {
            int32_t out = 0;
            for (int i = 0; i < 32; i++) {
                int bitA = (a >> i) & 1;
                int bitB = (b >> i) & 1;
                int bitResult = bitA | bitB;    // OR gate for this bit position
                out |= (bitResult << i);
            }
            result.value = out;
            break;
        }
      
        case Opcode::NOT: {
            // NOT is SINGLE_ARG (skips sourceRegister1 per isa.h), so the real input is 'b'
            int32_t out = 0;
            for (int i = 0; i < 32; i++) {
                int bitB = (b >> i) & 1;
                int bitResult = bitB ^ 1;       // inverter: flips the bit (1->0, 0->1)
                out |= (bitResult << i);
            }
            result.value = out;
            break;
        }
      
        case Opcode::MOV: {
            // MOV is SINGLE_ARG (skips sourceRegister1 per isa.h), so the real input is 'b'
            // Pure pass-through (MUX select), rebuilt bit-by-bit for consistency
            int32_t out = 0;
            for (int i = 0; i < 32; i++) {
                int bitB = (b >> i) & 1;
                out |= (bitB << i);
            }
            result.value = out;
            break;
        }
      
        case Opcode::CMP: {
            // COMPARE_ONLY: reuses subtractor logic (a - b), only the zero flag matters here.
            // Real E/GT flags are handled separately by evaluateComparison() in isa.h.
            int64_t wide = (int64_t)a - (int64_t)b;
            result.value = (int32_t)wide;
            break;
        }
      
        default:
            break;
    }

    result.flags.zero = (result.value == 0);
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
