// disassembler.h -- RISC201 disassembler (machine code -> assembly text)
//
// This is the exact reverse of the assembler:
//     assembler   :  .s text  -> tokens -> DecodedInstruction -> 32-bit word
//     disassembler:  32-bit word -> DecodedInstruction -> text
// It reuses convertFromBinary() and the operation table from isa.h/ISA.cpp,
// so the two tools can never disagree about the bit layout.
#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <cstdint>
#include <string>
#include <vector>

#include "isa.h"

// Turns ONE machine word into ONE line of assembly.
//   address     : byte address of this word (needed to turn a branch
//                 distance into an absolute target).
//   targetLabel : if non-empty, used as the text of the branch target
//                 (e.g. "L_0010"); otherwise a numeric target is printed.
// Words that are not valid instructions come back as a ".word 0x...." line.
std::string disassembleWord(uint32_t word, uint32_t address,
                            const std::string& targetLabel = "");

// Formats a 16-bit immediate the way the assembler expects to read it back:
//     SIGNED   -> decimal, may be negative     (-5)
//     UNSIGNED -> hex with a 'u' suffix        (0xFFFFu)
//     UPPER    -> hex with an 'h' suffix       (0x1234h)
std::string formatImmediate(uint16_t rawValue, risc201::ImmediateModifier modifier);

class Disassembler {
public:
    Disassembler() = default;

    // Reads machine code. asHex=false : raw binary, 4 bytes per word
    // (the format Assembler::writeOutput writes by default).
    // asHex=true : text file, one word per line such as 0x0400C005.
    bool loadInput(const std::string& path, bool asHex = false);

    // Runs the two passes described in disassembler.cpp.
    bool disassemble();

    // Writes the assembly text. Set showAddresses to add a
    // "; 0x0000: 0x04008005" comment after each instruction.
    bool writeOutput(const std::string& outPath, bool showAddresses = false) const;

    // Number of instructions loaded.
    size_t instructionCount() const { return words.size(); }

private:
    // One line of output: either a label ("L_0010:") or an instruction.
    struct OutLine {
        std::string text;
        bool        isInstruction = false;
        uint32_t    address = 0;   // only meaningful for instructions
        uint32_t    word = 0;      // only meaningful for instructions
    };

    std::vector<uint32_t> words;     // the loaded program
    std::vector<OutLine>  outLines;  // the finished assembly listing
};

#endif // DISASSEMBLER_H