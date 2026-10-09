#include "alu.h"
#include <cstdint>
#include <climits>
#include <stdexcept>

namespace risc201 {

// The Public Interface
ALUResult ALU::execute(Opcode op, int32_t operand1, int32_t operand2) {
    switch (op) {
        // Arithmetic
        case Opcode::ADD: case Opcode::SUB: case Opcode::MUL: case Opcode::DIV:
        case Opcode::MOD: case Opcode::NEG: case Opcode::ABS: case Opcode::MIN:
        case Opcode::MAX:
            return computeArithmetic(op, operand1, operand2);

        // Logical
        case Opcode::AND: case Opcode::OR:  case Opcode::XOR: case Opcode::NOT:
        case Opcode::MOV: case Opcode::CMP:
            return computeLogical(op, operand1, operand2);
        
        // Shift
        case Opcode::LSL: case Opcode::LSR: case Opcode::ASR: case Opcode::ROL:
        case Opcode::ROR:
            return computeShift(op, operand1, operand2);
       
        // Bypassed Operations (Loads, Stores, Branches)
        case Opcode::NOP: case Opcode::LD:  case Opcode::ST:  case Opcode::BEQ:
        case Opcode::BGT: case Opcode::B:   case Opcode::CALL: case Opcode::RET:
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

// -------------------------------------------------------------
// Primitives
// -------------------------------------------------------------
ALU::AdderOutput ALU::carryLookaheadAdd(uint32_t a, uint32_t b, bool carryIn) {
    bool G[32], P[32];
    for (int i = 0; i < 32; i++) {
        bool ai = (a >> i) & 1;
        bool bi = (b >> i) & 1;
        G[i] = ai && bi;
        P[i] = ai ^ bi;
    }

    bool GG[8], GP[8];
    for (int j = 0; j < 8; j++) {
        int base = j * 4;
        bool g0 = G[base], g1 = G[base+1], g2 = G[base+2], g3 = G[base+3];
        bool p0 = P[base], p1 = P[base+1], p2 = P[base+2], p3 = P[base+3];
        GG[j] = g3 || (p3 && g2) || (p3 && p2 && g1) || (p3 && p2 && p1 && g0);
        GP[j] = p0 && p1 && p2 && p3;
    }

    bool groupCarry[9];
    groupCarry[0] = carryIn;
    for (int j = 0; j < 8; j++)
        groupCarry[j+1] = GG[j] || (GP[j] && groupCarry[j]);

    uint32_t sum = 0;
    for (int j = 0; j < 8; j++) {
        int base = j * 4;
        bool c = groupCarry[j];
        for (int k = 0; k < 4; k++) {
            int i = base + k;
            bool bitSum = P[i] ^ c;
            if (bitSum) sum |= (1u << i);
            c = G[i] || (P[i] && c);
        }
    }

    AdderOutput out;
    out.sum = sum;
    out.carryOut = groupCarry[8];
    return out;
}

uint32_t ALU::negateCLA(uint32_t x) {
    AdderOutput r = carryLookaheadAdd(~x, 0, true);
    return r.sum;
}

void ALU::restoringDivide(uint32_t dividend, uint32_t divisor, uint32_t& quotient, uint32_t& remainder) {
    quotient = 0;
    remainder = 0;
    for (int i = 31; i >= 0; i--) {
        remainder = (remainder << 1) | ((dividend >> i) & 1);
        AdderOutput r = carryLookaheadAdd(remainder, negateCLA(divisor), false);
        if ((r.sum >> 31) & 1) {
            quotient = quotient << 1;
        } else {
            remainder = r.sum;
            quotient = (quotient << 1) | 1;
        }
    }
}

// -------------------------------------------------------------
// Group Dispatchers
// -------------------------------------------------------------
ALUResult ALU::computeArithmetic(Opcode op, int32_t a, int32_t b) {
    ALUResult result;
    result.value = 0;
    result.flags.zero = false;
    result.flags.carry = false;
    result.flags.overflow = false;

    switch (op) {
        case Opcode::ADD: {
            AdderOutput r = carryLookaheadAdd((uint32_t)a, (uint32_t)b, false);
            result.value = (int32_t)r.sum;
            result.flags.carry = r.carryOut;
            bool signA = (a >> 31) & 1, signB = (b >> 31) & 1, signR = (r.sum >> 31) & 1;
            result.flags.overflow = (signA == signB) && (signR != signA);
            break;
        }

        case Opcode::SUB: {
            uint32_t bInverted = ~(uint32_t)b;
            AdderOutput r = carryLookaheadAdd((uint32_t)a, bInverted, true);
            result.value = (int32_t)r.sum;
            result.flags.carry = r.carryOut;
            bool signA = (a >> 31) & 1, signB = (b >> 31) & 1, signR = (r.sum >> 31) & 1;
            result.flags.overflow = (signA != signB) && (signR != signA);
            break;
        }

        case Opcode::MUL: {
            bool Aext = false;
            uint32_t A = 0;
            uint32_t Q = (uint32_t)b;
            uint32_t M = (uint32_t)a;
            bool Mext = (M >> 31) & 1;

            AdderOutput negLow = carryLookaheadAdd(~M, 0, true);
            uint32_t negM = negLow.sum;
            bool negMext = (!Mext) ^ negLow.carryOut;

            bool Qm1 = false;

            for (int i = 0; i < 32; i++) {
                bool Q0 = Q & 1;

                if (Q0 && !Qm1) {
                    AdderOutput r = carryLookaheadAdd(A, negM, false);
                    Aext = Aext ^ negMext ^ r.carryOut;
                    A = r.sum;
                } else if (!Q0 && Qm1) {
                    AdderOutput r = carryLookaheadAdd(A, M, false);
                    Aext = Aext ^ Mext ^ r.carryOut;
                    A = r.sum;
                }

                Qm1 = Q0;
                uint32_t Alsb = A & 1;
                Q = (Q >> 1) | (Alsb << 31);
                A = (A >> 1) | (Aext ? (1u << 31) : 0);
            }

            uint64_t highWord = A;
            uint64_t product = (highWord << 32) | Q;
            int64_t signedProduct = static_cast<int64_t>(product);
            bool overflow = signedProduct < INT32_MIN || signedProduct > INT32_MAX;

            result.value = (int32_t)Q;
            result.flags.overflow = overflow;
            result.flags.carry = overflow;
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
            bool signA = (a >> 31) & 1, signB = (b >> 31) & 1;
            uint32_t absA = signA ? negateCLA((uint32_t)a) : (uint32_t)a;
            uint32_t absB = signB ? negateCLA((uint32_t)b) : (uint32_t)b;
            uint32_t quotient, remainder;
            restoringDivide(absA, absB, quotient, remainder);
            result.value = (signA != signB) ? (int32_t)negateCLA(quotient) : (int32_t)quotient;
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
            bool signA = (a >> 31) & 1, signB = (b >> 31) & 1;
            uint32_t absA = signA ? negateCLA((uint32_t)a) : (uint32_t)a;
            uint32_t absB = signB ? negateCLA((uint32_t)b) : (uint32_t)b;
            uint32_t quotient, remainder;
            restoringDivide(absA, absB, quotient, remainder);
            result.value = signA ? (int32_t)negateCLA(remainder) : (int32_t)remainder;
            break;
        }

        case Opcode::NEG: {
            if (a == INT32_MIN) {
                result.value = a;
                result.flags.overflow = true;
                break;
            }
            result.value = (int32_t)negateCLA((uint32_t)a);
            break;
        }

        case Opcode::ABS: {
            if (a == INT32_MIN) {
                result.value = a;
                result.flags.overflow = true;
                break;
            }
            bool signA = (a >> 31) & 1;
            result.value = signA ? (int32_t)negateCLA((uint32_t)a) : a;
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

ALUResult ALU::computeLogical(Opcode op, int32_t a, int32_t b) {
    ALUResult result;
    result.value = 0;
    result.flags.zero = false;
    result.flags.carry = false;      // Not used in logic ops
    result.flags.overflow = false;   // Not used in logic ops
    switch (op) {
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
       
       case Opcode::XOR: {
            int32_t out = 0;
            for (int i = 0; i < 32; i++) {
                int bitA = (a >> i) & 1;
                int bitB = (b >> i) & 1;
                int bitResult = bitA ^ bitB;    // XOR gate: 1 only when the two bits differ
                out |= (bitResult << i);
            }
            result.value = out;
            break;
        }
       
        case Opcode::NOT: {
            // NOT is SINGLE_ARG, reads sourceRegister1, so the real input is 'a'
            int32_t out = 0;
            for (int i = 0; i < 32; i++) {
                int bitA = (a >> i) & 1;
                int bitResult = bitA ^ 1;        // inverter: flips the bit (1->0, 0->1)
                out |= (bitResult << i);
            }
            result.value = out;
            break;
        }
      
        case Opcode::MOV: {
            // MOV is SINGLE_ARG, reads sourceRegister1, so the real input is 'a'
            // Pure pass-through (MUX select), rebuilt bit-by-bit for consistency
            int32_t out = 0;
            for (int i = 0; i < 32; i++) {
                int bitA = (a >> i) & 1;
                out |= (bitA << i);
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

ALUResult ALU::computeShift(Opcode op, int32_t a, int32_t b) {
    ALUResult result;
    result.value = 0;
    result.flags.zero = false;
    result.flags.carry = false;      // Not needed for simple shifts
    result.flags.overflow = false;   // Not needed for shifts

    uint32_t ua = (uint32_t)a;
    uint32_t amount = (uint32_t)b & 31;   // only 0-31 makes sense for a 32-bit shift

    switch (op) {
        case Opcode::LSL: {
            uint32_t out = 0;
            for (int i = 0; i < 32; i++) {
                int destPos = i + amount;
                if (destPos < 32) {
                    int bit = (ua >> i) & 1;
                    out |= ((uint32_t)bit << destPos);
                }
            }
            result.value = (int32_t)out;
            break;
        }

       case Opcode::LSR:{
    uint32_t out = 0;

    for (int i = 0; i < 32; i++) {
        int destPos = i - amount;

        if (destPos >= 0) {
            int bit = (ua >> i) & 1;
            out |= ((uint32_t)bit << destPos);
        }
    }

    result.value = (int32_t)out;
    break;
       }

        case Opcode::ASR: {
    uint32_t out = 0;
    int signBit = (ua >> 31) & 1;

    for (int i = 0; i < 32; i++) {
        int destPos = i - amount;

        if (destPos >= 0) {
            int bit = (ua >> i) & 1;
            out |= ((uint32_t)bit << destPos);
        }
    }

    if (signBit) {
        for (int i = 31; i >= 32 - amount; i--) {
            out |= (1u << i);
        }
    }

    result.value = (int32_t)out;
    break;
}
        case Opcode::ROL: {
            uint32_t out = 0;
            for (int i = 0; i < 32; i++) {
                int destPos = i + amount;
                if (destPos >= 32) destPos -= 32;   // wrap around
                int bit = (ua >> i) & 1;
                out |= ((uint32_t)bit << destPos);
            }
            result.value = (int32_t)out;
            break;
        }
case Opcode::ROR: {
    uint32_t out = 0;

    for (int i = 0; i < 32; i++) {
        int destPos = i - amount;

        if (destPos < 0)
            destPos += 32;

        int bit = (ua >> i) & 1;

        out |= ((uint32_t)bit << destPos);
    }

    result.value = (int32_t)out;
    break;
}
         default:
            break;
    }

    result.flags.zero = (result.value == 0);
    return result;
}

} // namespace risc201
