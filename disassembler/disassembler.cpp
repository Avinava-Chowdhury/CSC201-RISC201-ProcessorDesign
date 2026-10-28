// disassembler.cpp -- RISC201 disassembler.
//
// Pass 1: decode every word and collect the addresses that some branch /
//         call jumps to (each one will get a label).
// Pass 2: print the instructions, inserting "L_xxxx:" before each target.
//
// The output can be fed straight back into the assembler.
#include "disassembler.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

using namespace risc201;

namespace
{//converts to hexadecimal
    std::string hex32(uint32_t v)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(v));
        return buf;
    }

    // "L_0010" for address 0x10 branch taregt address ko label mai convrt krta
    std::string labelFor(uint32_t address)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "L_%04X", static_cast<unsigned>(address));
        return buf;
    }

    // Second operand: a register or an immediate.
    std::string regOrImmediate(const DecodedInstruction& d, uint8_t reg)
    {
        return d.usesConstantNumber ? formatImmediate(d.rawConstantValue, d.constantModifier)
                                    : std::string(getRegisterName(reg));
    }

    bool validModifier(const DecodedInstruction& d)
    {
        return !d.usesConstantNumber ||
               static_cast<uint8_t>(d.constantModifier) <= static_cast<uint8_t>(ImmediateModifier::UPPER);
    }

    // Absolute target of a branch at `address`, or -1 if it falls outside 32 bits.
    int64_t branchTarget(const DecodedInstruction& d, uint32_t address)
    {
        return static_cast<int64_t>(address) + static_cast<int64_t>(d.branchJumpDistance);
    }
}  // namespace

// ---------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------
//Immediate value ko uske modifier ke according assembly syntax mein convert karta hai.
std::string formatImmediate(uint16_t rawValue, ImmediateModifier modifier)
{
    char buf[24];
    switch (modifier)
    {
    case ImmediateModifier::UNSIGNED:
        std::snprintf(buf, sizeof(buf), "0x%Xu", static_cast<unsigned>(rawValue));
        break;
    case ImmediateModifier::UPPER:
        std::snprintf(buf, sizeof(buf), "0x%Xh", static_cast<unsigned>(rawValue));
        break;
    default:  // SIGNED: show the 16-bit value as a signed decimal
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(extendSignBit(rawValue, CONSTANT_VALUE_BIT_COUNT)));
        break;
    }
    return buf;
}
//Ye one 32-bit machine-code word ko assembly instruction mein convert karta hai.
std::string disassembleWord(uint32_t word, uint32_t address, const std::string& targetLabel)
{
    const std::optional<DecodedInstruction> decoded = convertFromBinary(word);
    if (!decoded || !validModifier(*decoded))
    {
        return ".word " + hex32(word) + "   ; not a valid instruction";
    }

    const DecodedInstruction& d = *decoded;
    // FPU words must be register-form with bits [11:0] clear; anything else is not a real instruction
    if (d.operation == OperationCode::FPU && (d.usesConstantNumber || (word & 0x0FFFu) != 0))
    {
        return ".word " + hex32(word) + "   ; not a valid instruction";
    }
    const OperationDetails* info = findByOpcodeAndFunction(d.operation, d.fpuFunction);   // never null: decode succeeded
    const std::string name = info->nameInAssembly;

    switch (info->assemblyFormat)
    {
    case AssemblySyntax::NO_ARGS:                       // nop, ret
        return name;

    case AssemblySyntax::JUMP_TO_LABEL:                 // beq/bgt/b/call
    {
        if (!targetLabel.empty())
        {
            return name + " " + targetLabel;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), " 0x%llX   ; target outside the program",
                      static_cast<unsigned long long>(branchTarget(d, address)));
        return name + buf;
    }

    case AssemblySyntax::THREE_ARG_MATH:                // add rd, rs1, rs2|imm
        return name + " " + getRegisterName(d.destinationRegister) + ", " +
               getRegisterName(d.sourceRegister1) + ", " + regOrImmediate(d, d.sourceRegister2);

    case AssemblySyntax::COMPARE_ONLY:                  // cmp rs1, rs2|imm
        return name + " " + getRegisterName(d.sourceRegister1) + ", " +
               regOrImmediate(d, d.sourceRegister2);

    case AssemblySyntax::SINGLE_ARG:                    // mov/not/neg/abs rd, rs1|imm
        return name + " " + getRegisterName(d.destinationRegister) + ", " +
               regOrImmediate(d, d.sourceRegister1);

    case AssemblySyntax::LOAD_MEMORY:                   // ld rd, off[rs1]
    case AssemblySyntax::STORE_MEMORY:                  // st rd, off[rs1]   (rd = data to store)
        return name + " " + getRegisterName(d.destinationRegister) + ", " +
               formatImmediate(d.rawConstantValue, d.constantModifier) +
               "[" + getRegisterName(d.sourceRegister1) + "]";
    }
    return ".word " + hex32(word);
}

// ---------------------------------------------------------------------
// Disassembler class
// ---------------------------------------------------------------------

bool Disassembler::loadInput(const std::string& path, bool asHex)
{
    words.clear();

    if (asHex)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            std::cerr << "Error: Could not open input file: " << path << "\n";
            return false;
        }
        std::string line;
        int lineNumber = 0;
        while (std::getline(file, line))
        {
            ++lineNumber;
            line = line.substr(0, line.find(';'));
            std::istringstream stream(line);
            std::string token;
            if (!(stream >> token)) continue;                       // blank line
            try
            {
                size_t used = 0;
                const unsigned long value = std::stoul(token, &used, 16);   // accepts "0x..." too
                if (used != token.size() || value > 0xFFFFFFFFul) throw std::invalid_argument("bad");
                words.push_back(static_cast<uint32_t>(value));
            }
            catch (const std::exception&)
            {
                std::cerr << "Error: line " << lineNumber << ": '" << token << "' is not a 32-bit hex word\n";
                return false;
            }
        }
    }
    else
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            std::cerr << "Error: Could not open input file: " << path << "\n";
            return false;
        }
        uint32_t word;
        // Same byte order the assembler used when it wrote the file.
        while (file.read(reinterpret_cast<char*>(&word), sizeof(word)))
        {
            words.push_back(word);
        }
        if (file.gcount() != 0)
        {
            std::cerr << "Warning: file size is not a multiple of 4 bytes; ignoring "
                      << file.gcount() << " trailing byte(s)\n";
        }
    }
    return true;
}

bool Disassembler::disassemble()
{
    if (words.empty())
    {
        std::cerr << "Error: No machine code loaded.\n";
        return false;
    }

    const uint32_t programEnd = static_cast<uint32_t>(words.size()) * BYTES_PER_WORD;

    // ---- Pass 1: find every branch target ----------------------------
    std::set<uint32_t> targets;
    for (size_t i = 0; i < words.size(); ++i)
    {
        const auto decoded = convertFromBinary(words[i]);
        if (!decoded || !isBranchLayout(decoded->operation)) continue;

        const int64_t target = branchTarget(*decoded, static_cast<uint32_t>(i) * BYTES_PER_WORD);
        // Only label targets we can actually place: word-aligned and
        // inside the program (or exactly one past the end).
        if (target >= 0 && target <= programEnd && target % BYTES_PER_WORD == 0)
        {
            targets.insert(static_cast<uint32_t>(target));
        }
    }

    // ---- Pass 2: emit labels and instructions ------------------------
    outLines.clear();
    for (size_t i = 0; i < words.size(); ++i)
    {
        const uint32_t address = static_cast<uint32_t>(i) * BYTES_PER_WORD;

        if (targets.count(address))
        {
            OutLine label;
            label.text = labelFor(address) + ":";
            outLines.push_back(label);
        }

        std::string targetLabel;
        const auto decoded = convertFromBinary(words[i]);
        if (decoded && isBranchLayout(decoded->operation))
        {
            const int64_t target = branchTarget(*decoded, address);
            if (target >= 0 && target <= programEnd && target % BYTES_PER_WORD == 0)
            {
                targetLabel = labelFor(static_cast<uint32_t>(target));
            }
        }

        OutLine line;
        line.text = disassembleWord(words[i], address, targetLabel);
        line.isInstruction = true;
        line.address = address;
        line.word = words[i];
        outLines.push_back(line);
    }

    // A branch to "one past the last instruction" needs its label at the end.
    if (targets.count(programEnd))
    {
        OutLine label;
        label.text = labelFor(programEnd) + ":";
        outLines.push_back(label);
    }
    return true;
}

bool Disassembler::writeOutput(const std::string& outPath, bool showAddresses) const
{
    if (outLines.empty())
    {
        std::cerr << "Error: Nothing to write (run disassemble() first).\n";
        return false;
    }

    std::ofstream out(outPath);
    if (!out.is_open())
    {
        std::cerr << "Error: Could not open output file: " << outPath << "\n";
        return false;
    }

    for (const OutLine& line : outLines)
    {
        if (!line.isInstruction)
        {
            out << line.text << "\n";
            continue;
        }
        out << "    " << line.text;
        if (showAddresses)
        {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "   ; 0x%04X: 0x%08X",
                          static_cast<unsigned>(line.address), static_cast<unsigned>(line.word));
            out << buf;
        }
        out << "\n";
    }
    return true;
}
