#include "assembler.h"
#include "tokenizer.h"
#include "pass1.h"
#include "pass2.h"
#include "isa.h"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <exception>
#include <stdexcept>
#include <string>

using namespace std;
using namespace risc201;

bool Assembler::loadSourceFile(const string& path)
{
    ifstream file(path);

    if (!file.is_open()) {
        cerr << "Error: Could not open source file: " << path << "\n";
        return false;
    }

    rawLines.clear();

    string line;
    while (getline(file, line)) {
        rawLines.push_back(line);
    }

    file.close();

    return true;
}

bool Assembler::assemble()
{
    if (rawLines.empty()) {
        cerr << "Error: No source code loaded.\n";
        return false;
    }

    try {
        // ---------------------------------------------------------
        // Step 1: Expand macros
        // ---------------------------------------------------------
        vector<string> expandedLines = expandMacroLines(splitLabelLines(rawLines));

        // ---------------------------------------------------------
        // Step 2: Pass 1
        // ---------------------------------------------------------
        symbolTable = buildSymbolTable(expandedLines);

        // ---------------------------------------------------------
        // Step 3: Pass 2
        // ---------------------------------------------------------
        machineCode.clear();

        uint32_t addr = 0;

        for (const auto& line : expandedLines) {

            // Convert assembly text into tokens.
            vector<string> tokens;
            try {
                tokens = tokenizeLine(line);
            } catch (const exception& e) {
                throw runtime_error(string(e.what()) + "  [at: " + line + "]");
            }

            // Blank/comment-only line.
            if (tokens.empty()) {
                continue;
            }

            bool isLabel =
                tokens.size() == 1 &&
                !tokens[0].empty() &&
                tokens[0].back() == ':';

            if (isLabel) {
                continue;
            }

            // ".word N": raw data word, copied straight into the output.
            if (isWordDirective(tokens)) {
                try {
                    machineCode.push_back(parseWordDirective(tokens));
                } catch (const exception& e) {
                    throw runtime_error(string(e.what()) + "  [at: " + line + "]");
                }
                addr += BYTES_PER_WORD;
                continue;
            }
            
            // Convert tokens into a decoded instruction.
            DecodedInstruction instr;
            try {
                instr = buildInstruction(tokens, addr, symbolTable);
            } catch (const exception& e) {
                throw runtime_error(string(e.what()) + "  [at: " + line + "]");
            }

            // Convert the decoded instruction into the final 32-bit machine word.
            uint32_t word = encode(instr);

            machineCode.push_back(word);

            // Every instruction is 4 bytes.
            addr += BYTES_PER_WORD;
        }
            constexpr size_t MAX_PROGRAM_WORDS = INSTRUCTION_MEMORY_SIZE / BYTES_PER_WORD;
        if (machineCode.size() > MAX_PROGRAM_WORDS) {
            throw runtime_error("program is " + to_string(machineCode.size()) +
                                " words; instruction memory holds only " + to_string(MAX_PROGRAM_WORDS));
        }
    } catch (const exception& e) {

        cerr << "Assembly Error: " << e.what() << "\n";
        return false;
    }

    return true;
}

bool Assembler::writeOutput(const string& outPath, bool asHex)
{
    if (machineCode.empty()) {
        cerr << "Error: No machine code to write.\n";
        return false;
    }

    // ---------------------------------------------------------
    // Hexadecimal text output
    // ---------------------------------------------------------
    if (asHex) {

        ofstream outFile(outPath);

        if (!outFile.is_open()) {
            cerr << "Error: Could not open output file: "
                 << outPath << "\n";
            return false;
        }

        for (uint32_t word : machineCode) {
            outFile
                << "0x"
                << hex
                << setw(8)
                << setfill('0')
                << word
                << "\n";
        }

        outFile.close();
    }

    // ---------------------------------------------------------
    // Raw binary output
    // ---------------------------------------------------------
    else {

        ofstream outFile(outPath, ios::binary);

        if (!outFile.is_open()) {
            cerr << "Error: Could not open output file: "
                 << outPath << "\n";
            return false;
        }

        for (uint32_t word : machineCode) {
            outFile.write(
                reinterpret_cast<const char*>(&word),
                sizeof(word)
            );
        }

        outFile.close();
    }

    return true;
}

// ---------------------------------------------------------
// Symbol Table Export
// ---------------------------------------------------------
bool Assembler::writeSymbolTable(const string& symPath)
{
    ofstream outFile(symPath);

    if (!outFile.is_open()) {
        cerr << "Error: Could not open symbol file for writing: " << symPath << "\n";
        return false;
    }

    for (const auto& pair : symbolTable) {
        outFile << pair.first << ": 0x" << hex << setw(8) << setfill('0') << pair.second << "\n";
    }

    outFile.close();
    return true;
}
