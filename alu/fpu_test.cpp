// fpu_test.cpp -- checks the ALU's floating-point unit against the host CPU's IEEE-754 hardware.
// Build/run:  g++ -std=c++20 -Ialu -Iisa alu/ALU.cpp alu/fpu_test.cpp -o fpu_test && ./fpu_test
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>
#include "alu.h"

using namespace risc201;

static uint32_t bitsOf(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
static float    floatOf(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
static bool isNaNBits(uint32_t u) { return (u & 0x7F800000u) == 0x7F800000u && (u & 0x007FFFFFu); }

static int failures = 0;
static long checks = 0;

static void check(ALU& alu, uint8_t fn, uint32_t a, uint32_t b) {
    volatile float fa = floatOf(a), fb = floatOf(b), r;
    switch (fn) {
        case 0: r = fa + fb; break;
        case 1: r = fa - fb; break;
        case 2: r = fa * fb; break;
        default: r = fa / fb; break;
    }
    uint32_t expect = bitsOf(r);
    ALUResult got = alu.execute(Opcode::FPU, (int32_t)a, (int32_t)b, fn);
    uint32_t gotBits = (uint32_t)got.value;
    checks++;
    bool ok = isNaNBits(expect) ? isNaNBits(gotBits) : (expect == gotBits);
    bool zeroOk = got.flags.zero == ((gotBits & 0x7FFFFFFFu) == 0);
    if (!ok || !zeroOk) {
        if (failures++ < 15)
            std::cout << "[FAIL] fn=" << (int)fn << " a=0x" << std::hex << a << " b=0x" << b
                      << " expected 0x" << expect << " got 0x" << gotBits << std::dec << "\n";
    }
}

int main() {
    ALU alu;
    std::vector<uint32_t> special = {
        0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x40000000, 0x3F000000, 0x7F7FFFFF, 0xFF7FFFFF,
        0x00000001, 0x80000001, 0x007FFFFF, 0x00800000, 0x7F800000, 0xFF800000, 0x7FC00000, 0x7F800001,
        0x3FC00000, 0x40490FDB, 0x33800000, 0x4B000000, 0x4B800000, 0x34000000, 0x7F000000, 0x00FFFFFF };
    for (uint32_t a : special)
        for (uint32_t b : special)
            for (uint8_t fn = 0; fn < 4; fn++) check(alu, fn, a, b);

    std::mt19937 rng(12345);
    for (int i = 0; i < 400000; i++) {
        uint32_t a = rng(), b = rng();
        int mode = i % 4;
        if (mode == 1) b = (a & 0x807FFFFFu) | (((a >> 23) + (rng() % 5) - 2) & 0xFF) << 23;   // close exponents
        if (mode == 2) { a &= 0x807FFFFFu | ((rng() % 3) << 23); }                              // tiny / subnormal
        if (mode == 3) { a = (a & 0x807FFFFFu) | ((100 + rng() % 56) << 23); b = (b & 0x807FFFFFu) | ((100 + rng() % 56) << 23); }
        for (uint8_t fn = 0; fn < 4; fn++) check(alu, fn, a, b);
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    std::cout << (failures == 0 ? "FPU: ALL TESTS PASSED\n" : "FPU: FAILED\n");
    return failures == 0 ? 0 : 1;
}
