// pass2.h -- Pass 2 of the RISC201 assembler (instruction building,
// encoding, and PUSH/POP macro expansion).
//
// This header only DECLARES the functions. The implementations live in
// pass2.cpp. Anything that wants to use them just does:  #include "pass2.h"
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "isa.h"

// Builds one instruction from a line's tokens, using the symbol table to
// resolve any label. `currentAddress` is the byte address of THIS instruction
// (branch distance = target address - currentAddress).
// Throws std::invalid_argument / std::out_of_range on any bad input
// (unknown mnemonic, bad register, undefined label, immediate too large...).
risc201::DecodedInstruction buildInstruction(
    const std::vector<std::string>& tokens,
    uint32_t currentAddress,
    const std::map<std::string, uint32_t>& symbols);

// Packs a DecodedInstruction into the final 32-bit machine word.
uint32_t encode(const risc201::DecodedInstruction& instr);

// Expands PUSH / POP into real instructions, working on RAW TEXT LINES.
// This must run BEFORE tokenizeLine() and buildSymbolTable(), because the
// tokenizer rejects "push"/"pop" as unknown instructions, and because label
// addresses have to account for each macro becoming two instructions.
//     push r1  ->  sub sp, sp, 4
//                  st  r1, 0[sp]
//     pop  r1  ->  ld  r1, 0[sp]
//                  add sp, sp, 4
std::vector<std::string> expandMacroLines(const std::vector<std::string>& rawLines);

// Same expansion, but working on already-tokenized lines. Only usable if the
// tokens were produced without the tokenizer's mnemonic check rejecting
// push/pop first.
std::vector<std::vector<std::string>> expandMacros(
    const std::vector<std::vector<std::string>>& tokenLines);
