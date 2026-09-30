#include <iostream>
#include <cstdint>
#include <climits>
#include <iomanip>
#include "alu.h"

using namespace std;
using namespace risc201;

void assertTest(const string& testName, ALUResult res, int32_t expVal, bool expZ, bool expC, bool expO) {
    bool passed = (res.value == expVal) && 
                  (res.flags.zero == expZ) && 
                  (res.flags.carry == expC) && 
                  (res.flags.overflow == expO);

    if (passed) {
        cout << "[PASS] " << left << setw(35) << testName << " | Val: " << res.value << "\n";
    } else {
        cout << "[FAIL] " << testName << "\n";
        cout << "       Expected -> Val: " << expVal << ", Z: " << expZ << ", C: " << expC << ", O: " << expO << "\n";
        cout << "       Got      -> Val: " << res.value << ", Z: " << res.flags.zero << ", C: " << res.flags.carry << ", O: " << res.flags.overflow << "\n";
    }
}

void testArithmetic() {
    cout << "\n--- Arithmetic Operations ---\n";
    
    assertTest("ADD: 15 + 25", execute(Opcode::ADD, 15, 25), 40, false, false, false);
    assertTest("SUB: 10 - 20", execute(Opcode::SUB, 10, 20), -10, false, false, false); 
    
    assertTest("ADD Overflow: INT_MAX + 1", execute(Opcode::ADD, INT32_MAX, 1), INT32_MIN, false, false, true);
    assertTest("SUB Overflow: INT_MIN - 1", execute(Opcode::SUB, INT32_MIN, 1), INT32_MAX, false, true, true);
    
    assertTest("MUL: 5 * -4", execute(Opcode::MUL, 5, -4), -20, false, false, false);
    assertTest("DIV by Zero", execute(Opcode::DIV, 10, 0), 0, true, false, true); 
    assertTest("DIV Overflow: INT_MIN / -1", execute(Opcode::DIV, INT32_MIN, -1), INT32_MIN, false, false, true);
}

void testLogical() {
    cout << "\n--- Logical Operations ---\n";
    
    assertTest("AND: 0b1100 & 0b1010", execute(Opcode::AND, 12, 10), 8, false, false, false);
    assertTest("OR:  0b1100 | 0b1010", execute(Opcode::OR, 12, 10), 14, false, false, false);
    assertTest("XOR: 0b1100 ^ 0b1010", execute(Opcode::XOR, 12, 10), 6, false, false, false);
    assertTest("XOR: x ^ x = 0", execute(Opcode::XOR, 1234, 1234), 0, true, false, false);
    assertTest("XOR: x ^ 0 = x", execute(Opcode::XOR, 42, 0), 42, false, false, false);
    assertTest("XOR: x ^ -1 = ~x", execute(Opcode::XOR, 5, -1), -6, false, false, false);
    assertTest("XOR: sign bit ^ sign bit", execute(Opcode::XOR, INT32_MIN, INT32_MIN), 0, true, false, false);
    assertTest("XOR: INT_MIN ^ 1", execute(Opcode::XOR, INT32_MIN, 1), INT32_MIN + 1, false, false, false);
    assertTest("NOT: ~0", execute(Opcode::NOT, 0, 0), -1, false, false, false); 
    
    assertTest("MOV: Pass-through 42", execute(Opcode::MOV, 42, 0), 42, false, false, false);
    assertTest("CMP: Equal values", execute(Opcode::CMP, 100, 100), 0, true, false, false);
}

void testShift() {
    cout << "\n--- Shift Operations ---\n";
    
    assertTest("LSL: 1 << 3", execute(Opcode::LSL, 1, 3), 8, false, false, false);
    assertTest("LSR: 16 >> 2", execute(Opcode::LSR, 16, 2), 4, false, false, false);
    
    assertTest("ASR: -8 >> 1", execute(Opcode::ASR, -8, 1), -4, false, false, false);
    assertTest("ROL: 0x80000000 ROL 1", execute(Opcode::ROL, 0x80000000, 1), 1, false, false, false);
}

void testBypass() {
    cout << "\n--- Non-ALU Instructions ---\n";
    assertTest("LD: Should bypass ALU", execute(Opcode::LD, 10, 20), 0, false, false, false);
    assertTest("BEQ: Should bypass ALU", execute(Opcode::BEQ, 5, 5), 0, false, false, false);
}

int main() {
    cout << " RISC201 ALU Mid-Evaluation Test Suite\n";
    
    testArithmetic();
    testLogical();
    testShift();
    testBypass();

    cout << "\nAll test blocks executed successfully.\n";
    return 0;
}
