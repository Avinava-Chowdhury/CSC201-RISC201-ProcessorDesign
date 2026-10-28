#include "alu.h"
#include <cstdint>
#include <climits>
#include <stdexcept>

namespace risc201 {

// The Public Interface
ALUResult ALU::execute(Opcode op, int32_t operand1, int32_t operand2, uint8_t function) {
    switch (op) {
        // Floating point (fadd / fsub / fmul / fdiv, chosen by 'function')
        case Opcode::FPU:
            return computeFloat(function, (uint32_t)operand1, (uint32_t)operand2);
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

        case Opcode::LSR: {
            uint32_t out = 0;
            for (int i = 0; i < 32; i++) {
                int srcPos = i + amount;
                if (srcPos < 32) {
                    int bit = (ua >> srcPos) & 1;
                    out |= ((uint32_t)bit << i);
                }
            }
            result.value = (int32_t)out;
            break;
        }

        case Opcode::ASR: {
            uint32_t out = 0;
            int signBit = (ua >> 31) & 1;
            for (int i = 0; i < 32; i++) {
                int srcPos = i + amount;
                int bit = (srcPos < 32) ? (int)((ua >> srcPos) & 1) : signBit;
                out |= ((uint32_t)bit << i);
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
                int srcPos = i + amount;
                if (srcPos >= 32) srcPos -= 32;     // wrap around
                int bit = (ua >> srcPos) & 1;
                out |= ((uint32_t)bit << i);
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
// =============================================================
// Floating-point unit  (IEEE-754 binary32, round-to-nearest-even)
// =============================================================
// Everything below uses integer logic only (no 'float' type), the same way the rest of this ALU
// is built from bit operations and the carry-lookahead adder.
//
//   bit 31      sign
//   bits 30-23  biased exponent (bias 127)
//   bits 22-0   fraction (24th "hidden" bit is 1 for normal numbers)
//
// Supported: normal numbers, subnormals, +/-0, +/-infinity, NaN (always returned as the quiet
// NaN 0x7FC00000). Rounding mode is round-to-nearest, ties-to-even.

namespace {

constexpr uint32_t FP_SIGN_MASK = 0x80000000u;
constexpr uint32_t FP_EXP_MASK  = 0x7F800000u;
constexpr uint32_t FP_FRAC_MASK = 0x007FFFFFu;
constexpr uint32_t FP_HIDDEN    = 0x00800000u;
constexpr uint32_t FP_INF       = 0x7F800000u;
constexpr uint32_t FP_QNAN      = 0x7FC00000u;

enum class FpClass { ZERO, NORMAL, INF, NAN_VALUE };

// An unpacked operand: value = (-1)^sign * mantissa * 2^(exponent - 23), with bit 23 of 'mantissa'
// always set for non-zero finite numbers (subnormals are normalized here).
struct FpUnpacked {
    bool     sign;
    int      exponent;
    uint32_t mantissa;
    FpClass  kind;
};

FpUnpacked fpUnpack(uint32_t bits) {
    FpUnpacked u;
    u.sign = (bits & FP_SIGN_MASK) != 0;
    uint32_t expField = (bits & FP_EXP_MASK) >> 23;
    uint32_t frac     = bits & FP_FRAC_MASK;
    u.exponent = 0;
    u.mantissa = 0;

    if (expField == 0xFF) {
        u.kind = (frac != 0) ? FpClass::NAN_VALUE : FpClass::INF;
    } else if (expField == 0) {
        if (frac == 0) {
            u.kind = FpClass::ZERO;
        } else {                                  // subnormal: shift left until the hidden bit is set
            u.kind = FpClass::NORMAL;
            u.mantissa = frac;
            u.exponent = -126;
            while (!(u.mantissa & FP_HIDDEN)) {
                u.mantissa <<= 1;
                u.exponent--;
            }
        }
    } else {
        u.kind = FpClass::NORMAL;
        u.mantissa = frac | FP_HIDDEN;
        u.exponent = (int)expField - 127;
    }
    return u;
}

int msbIndex(uint64_t v) {                         // v must be non-zero
    int i = 63;
    while (!((v >> i) & 1)) i--;
    return i;
}

} // namespace

// 64-bit add/subtract using the 32-bit carry-lookahead adder twice (low half, then high half).
uint64_t ALU::add64(uint64_t a, uint64_t b) {
    AdderOutput lo = carryLookaheadAdd((uint32_t)a, (uint32_t)b, false);
    AdderOutput hi = carryLookaheadAdd((uint32_t)(a >> 32), (uint32_t)(b >> 32), lo.carryOut);
    return ((uint64_t)hi.sum << 32) | lo.sum;
}

uint64_t ALU::sub64(uint64_t a, uint64_t b) {
    uint64_t nb = ~b;
    AdderOutput lo = carryLookaheadAdd((uint32_t)a, (uint32_t)nb, true);
    AdderOutput hi = carryLookaheadAdd((uint32_t)(a >> 32), (uint32_t)(nb >> 32), lo.carryOut);
    return ((uint64_t)hi.sum << 32) | lo.sum;
}

// Rounds the exact value  mantissa * 2^exponent  to binary32 (round-to-nearest-even) and packs it.
// The caller guarantees the low bit of 'mantissa' is a sticky/jam bit whenever bits were discarded
// earlier, and that the mantissa has well over 24 significant bits.
// Sets 'exception' when the result overflows to infinity.
uint32_t ALU::fpRoundPack(bool sign, int exponent, uint64_t mantissa, bool& exception) {
    uint32_t signBits = sign ? FP_SIGN_MASK : 0;
    if (mantissa == 0) return signBits;

    int p = msbIndex(mantissa);                    // position of the leading 1
    int leadExp = exponent + p;                    // unbiased exponent of the leading 1
    int lsbExp = leadExp - 23;                     // weight of the last kept bit (normal numbers)
    if (lsbExp < -149) lsbExp = -149;              // subnormal range: fixed weight 2^-149

    int shift = lsbExp - exponent;                 // how many low bits are dropped
    uint64_t kept;
    if (shift <= 0) {
        kept = mantissa << (-shift);               // exact, nothing dropped
    } else if (shift > 64) {
        kept = 0;                                  // everything is below half an ulp
    } else {
        kept = (shift == 64) ? 0 : (mantissa >> shift);
        bool guard = (shift - 1) < 64 && ((mantissa >> (shift - 1)) & 1);
        uint64_t belowMask = (shift - 1 >= 64) ? ~0ull : ((1ull << (shift - 1)) - 1);
        bool sticky = (mantissa & belowMask) != 0;
        if (guard && (sticky || (kept & 1))) kept++;
    }
    if (shift > 64) {
        // value < 2^-150 -> rounds to zero (exact half-way cannot occur here)
        return signBits;
    }

    // bits = ((lsbExp + 149) << 23) + kept  handles the rounding carry, the subnormal->normal
    // boundary and the normal exponent in one step (see the derivation in the project notes).
    int64_t bits = ((int64_t)(lsbExp + 149) << 23) + (int64_t)kept;
    if (bits >= (int64_t)FP_INF) {
        exception = true;                          // overflow -> infinity
        return signBits | FP_INF;
    }
    return signBits | (uint32_t)bits;
}

uint32_t ALU::fpAdd(uint32_t a, uint32_t b, bool& exception) {
    FpUnpacked x = fpUnpack(a), y = fpUnpack(b);

    if (x.kind == FpClass::NAN_VALUE || y.kind == FpClass::NAN_VALUE) return FP_QNAN;
    if (x.kind == FpClass::INF || y.kind == FpClass::INF) {
        if (x.kind == FpClass::INF && y.kind == FpClass::INF && x.sign != y.sign) {
            exception = true;                      // inf - inf is invalid
            return FP_QNAN;
        }
        return (x.kind == FpClass::INF) ? a : b;
    }
    if (x.kind == FpClass::ZERO && y.kind == FpClass::ZERO) {
        return (x.sign && y.sign) ? FP_SIGN_MASK : 0u;   // -0 + -0 = -0, otherwise +0
    }
    if (x.kind == FpClass::ZERO) return b;
    if (y.kind == FpClass::ZERO) return a;

    // Make x the operand with the larger exponent.
    if (y.exponent > x.exponent || (y.exponent == x.exponent && y.mantissa > x.mantissa)) {
        FpUnpacked t = x; x = y; y = t;
    }

    // 29 guard bits below the 24-bit mantissas leave room for the alignment shift.
    uint64_t mx = (uint64_t)x.mantissa << 29;
    uint64_t my = (uint64_t)y.mantissa << 29;
    int d = x.exponent - y.exponent;
    if (d > 0) {
        bool lost;
        if (d >= 64) { lost = (my != 0); my = 0; }
        else         { lost = (my & ((1ull << d) - 1)) != 0; my >>= d; }
        if (lost) my |= 1;                         // sticky "jam" into the lowest bit
    }

    uint64_t sum;
    bool sign = x.sign;
    if (x.sign == y.sign) {
        sum = add64(mx, my);
    } else {
        sum = sub64(mx, my);                       // x is the larger magnitude, so this is >= 0
        if (sum == 0) return 0u;                   // exact cancellation -> +0 (round to nearest)
    }
    return fpRoundPack(sign, x.exponent - 23 - 29, sum, exception);
}

uint32_t ALU::fpMul(uint32_t a, uint32_t b, bool& exception) {
    FpUnpacked x = fpUnpack(a), y = fpUnpack(b);
    bool sign = x.sign != y.sign;
    uint32_t signBits = sign ? FP_SIGN_MASK : 0;

    if (x.kind == FpClass::NAN_VALUE || y.kind == FpClass::NAN_VALUE) return FP_QNAN;
    if ((x.kind == FpClass::INF && y.kind == FpClass::ZERO) ||
        (x.kind == FpClass::ZERO && y.kind == FpClass::INF)) {
        exception = true;                          // 0 * inf is invalid
        return FP_QNAN;
    }
    if (x.kind == FpClass::INF || y.kind == FpClass::INF) return signBits | FP_INF;
    if (x.kind == FpClass::ZERO || y.kind == FpClass::ZERO) return signBits;

    // 24-bit x 24-bit shift-and-add multiplier -> exact 48-bit product
    uint64_t product = 0;
    for (int i = 0; i < 24; i++) {
        if ((y.mantissa >> i) & 1) product = add64(product, (uint64_t)x.mantissa << i);
    }
    return fpRoundPack(sign, x.exponent + y.exponent - 46, product, exception);
}

uint32_t ALU::fpDiv(uint32_t a, uint32_t b, bool& exception) {
    FpUnpacked x = fpUnpack(a), y = fpUnpack(b);
    bool sign = x.sign != y.sign;
    uint32_t signBits = sign ? FP_SIGN_MASK : 0;

    if (x.kind == FpClass::NAN_VALUE || y.kind == FpClass::NAN_VALUE) return FP_QNAN;
    if (x.kind == FpClass::INF && y.kind == FpClass::INF) { exception = true; return FP_QNAN; }
    if (x.kind == FpClass::ZERO && y.kind == FpClass::ZERO) { exception = true; return FP_QNAN; }
    if (x.kind == FpClass::INF)  return signBits | FP_INF;
    if (y.kind == FpClass::INF)  return signBits;                 // finite / inf = 0
    if (y.kind == FpClass::ZERO) { exception = true; return signBits | FP_INF; }   // x / 0 = inf
    if (x.kind == FpClass::ZERO) return signBits;

    // Restoring division of (mantissaX << 26) by mantissaY: gives a 26-27 bit quotient plus remainder.
    uint64_t dividend = (uint64_t)x.mantissa << 26;
    uint64_t divisor  = y.mantissa;
    uint64_t quotient = 0, remainder = 0;
    for (int i = 63; i >= 0; i--) {
        remainder = (remainder << 1) | ((dividend >> i) & 1);
        if (remainder >= divisor) {
            remainder = sub64(remainder, divisor);
            quotient = (quotient << 1) | 1;
        } else {
            quotient <<= 1;
        }
    }
    // Shift the quotient up one bit and put the sticky flag in the new lowest bit, so that a
    // non-zero remainder can never be mistaken for an exact result.
    uint64_t jammed = (quotient << 1) | (remainder != 0 ? 1 : 0);
    return fpRoundPack(sign, x.exponent - y.exponent - 26 - 1, jammed, exception);
}

ALUResult ALU::computeFloat(uint8_t function, uint32_t a, uint32_t b) {
    ALUResult result;
    result.value = 0;
    result.flags.zero = false;
    result.flags.carry = false;
    result.flags.overflow = false;

    bool exception = false;
    uint32_t out = 0;
    switch (function) {
        case 0: out = fpAdd(a, b, exception); break;                       // fadd
        case 1: out = fpAdd(a, b ^ FP_SIGN_MASK, exception); break;        // fsub = a + (-b)
        case 2: out = fpMul(a, b, exception); break;                       // fmul
        case 3: out = fpDiv(a, b, exception); break;                       // fdiv
        default: throw std::invalid_argument("ALU computeFloat: unknown FPU function");
    }

    result.value = (int32_t)out;
    result.flags.overflow = exception;
    result.flags.zero = ((out & ~FP_SIGN_MASK) == 0);                      // +0.0 and -0.0 are both zero
    return result;
}
} // namespace risc201
