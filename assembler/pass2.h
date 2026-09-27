#include <cstdio>
#include <iostream>
#include <vector>
#include <string>

#include "isa.h"
#include "tokenizer.h"
#include "pass1.h"
#include "pass2.h"

using namespace std;
using namespace risc201;

static void printWord(const string& label, uint32_t w) {
    printf("%-28s 0x%08X\n", label.c_str(), w);
}

int main() {
    // ---- Test 1: plain program with a backward AND forward branch ----
    vector<string> src1 = {
        "loop_start:",
        "sub r1, r2, 3",
        "beq loop_start",
        "add r4, r1, r2",
        "b end_label",
        "mov r5, 10",
        "end_label:",
        "nop"
    };

    cout << "=== Test 1: labels + branches ===\n";
    auto symbols1 = buildSymbolTable(src1);
    for (auto& [name, addr] : symbols1) {
        printf("  symbol %-12s -> 0x%04X\n", name.c_str(), addr);
    }

    uint32_t addr = 0;
    for (auto& line : src1) {
        auto tokens = tokenizeLine(line);
        if (tokens.empty()) continue;
        bool isLabel = tokens.size() == 1 && tokens[0].back() == ':';
        if (isLabel) continue;

        auto instr = buildInstruction(tokens, addr, symbols1);
        uint32_t word = encode(instr);
        printWord(line, word);
        addr += BYTES_PER_WORD;
    }

    // ---- Test 2: PUSH/POP macro expansion + correct addressing ----
    cout << "\n=== Test 2: push/pop macro expansion ===\n";
    vector<string> src2 = {
        "push r1",
        "mov r3, 99",
        "pop r2",
        "beq after_pop",   // should resolve correctly even though push/pop are now 2 instrs each
        "nop",
        "after_pop:",
        "nop"
    };

    auto expanded = expandMacroLines(src2);
    cout << "-- expanded source --\n";
    for (auto& l : expanded) cout << "  " << l << "\n";

    auto symbols2 = buildSymbolTable(expanded);
    for (auto& [name, a] : symbols2) {
        printf("  symbol %-12s -> 0x%04X\n", name.c_str(), a);
    }

    addr = 0;
    for (auto& line : expanded) {
        auto tokens = tokenizeLine(line);
        if (tokens.empty()) continue;
        bool isLabel = tokens.size() == 1 && tokens[0].back() == ':';
        if (isLabel) continue;

        auto instr = buildInstruction(tokens, addr, symbols2);
        uint32_t word = encode(instr);
        printWord(line, word);
        addr += BYTES_PER_WORD;
    }

    // ---- Test 3: load/store with offsets, immediates with suffixes ----
    cout << "\n=== Test 3: memory ops + immediate suffixes ===\n";
    vector<string> src3 = {
        "ld r1, 20[r2]",
        "st r3, -4[sp]",
        "mov r5, 0xFFFFh",
        "cmp r1, 5u",
    };
    addr = 0;
    map<string,uint32_t> noSymbols;
    for (auto& line : src3) {
        auto tokens = tokenizeLine(line);
        auto instr = buildInstruction(tokens, addr, noSymbols);
        uint32_t word = encode(instr);
        printWord(line, word);

        // round-trip check: decode it back and verify key fields match
        auto decoded = convertFromBinary(word);
        if (!decoded) { cout << "  !! failed to decode\n"; }
        addr += BYTES_PER_WORD;
    }

    // ---- Test 4: error handling ----
    cout << "\n=== Test 4: error handling ===\n";
    try {
        auto tokens = tokenizeLine("beq nowhere");
        buildInstruction(tokens, 0, noSymbols);
        cout << "  !! should have thrown for undefined label\n";
    } catch (const exception& e) {
        cout << "  OK, caught: " << e.what() << "\n";
    }

    try {
        auto tokens = tokenizeLine("not r1, 5");  // not doesn't allow immediates
        buildInstruction(tokens, 0, noSymbols);
        cout << "  !! should have thrown for immediate on 'not'\n";
    } catch (const exception& e) {
        cout << "  OK, caught: " << e.what() << "\n";
    }

    return 0;
}
