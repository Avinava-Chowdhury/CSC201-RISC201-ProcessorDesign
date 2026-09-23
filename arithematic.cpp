#include "alu.h"
#include <climits>

using namespace risc201;

ALUResult computeArithmetic(Opcode op, int32_t a, int32_t b) {
    ALUResult result;

    switch (op) {
        case OperationCode::ADD: {
            int64_t wide = (int64_t)a + (int64_t)b;
            int32_t truncated = (int32_t)wide;
            result.value = truncated;
            result.flags.overflow = (wide != (int64_t)truncated);
            result.flags.carry = ((uint64_t)(uint32_t)a + (uint32_t)b) > 0xFFFFFFFFULL;
            result.flags.zero = (truncated == 0);
            break;
        }

        case OperationCode::SUB: {
            int64_t wide = (int64_t)a - (int64_t)b;
            int32_t truncated = (int32_t)wide;
            result.value = truncated;
            result.flags.overflow = (wide != (int64_t)truncated);
            result.flags.carry = (uint32_t)a >= (uint32_t)b;
            result.flags.zero = (truncated == 0);
            break;
        }

        case OperationCode::MUL: {
            int64_t wide = (int64_t)a * (int64_t)b;
            int32_t truncated = (int32_t)wide;
            result.value = truncated;
            result.flags.overflow = (wide != (int64_t)truncated);
            result.flags.carry = result.flags.overflow;
            result.flags.zero = (truncated == 0);
            break;
        }

        case OperationCode::DIV: {
            if (b == 0) {
                result.value = 0;
                result.flags.overflow = true;
                result.flags.carry = false;
                result.flags.zero = true;
                break;
            }
            if (a == INT32_MIN && b == -1) {
                result.value = a;
                result.flags.overflow = true;
                result.flags.carry = false;
                result.flags.zero = false;
                break;
            }
            result.value = a / b;
            result.flags.overflow = false;
            result.flags.carry = false;
            result.flags.zero = (result.value == 0);
            break;
        }

        case OperationCode::MOD: {
            if (b == 0) {
                result.value = 0;
                result.flags.overflow = true;
                result.flags.carry = false;
                result.flags.zero = true;
                break;
            }
            if (a == INT32_MIN && b == -1) {
                result.value = 0;
                result.flags.overflow = true;
                result.flags.carry = false;
                result.flags.zero = true;
                break;
            }
            result.value = a % b;
            result.flags.overflow = false;
            result.flags.carry = false;
            result.flags.zero = (result.value == 0);
            break;
        }

        default:
            result.value = 0;
            result.flags.overflow = false;
            result.flags.carry = false;
            result.flags.zero = true;
            break;
    }

    return result;
}