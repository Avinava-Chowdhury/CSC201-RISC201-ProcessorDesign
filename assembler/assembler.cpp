#include "assembler.h"
#include "tokenizer.h"
#include "pass1.h"
#include "pass2.h"
#include "isa.h"

#include <fstream>
#include <iostream>
#include <iomanip>

using namespace std;
using namespace risc201;

bool Assembler::loadSourceFile(const string& path) {
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

bool Assembler::assemble() {
    if (rawLines.empty()) {
        cerr << "Error: No source code loaded.\n";
        return false;
    }

    try {
        // Step 1: Expand macros (e.g. PUSH / POP into real instruction strings)
        vector<string> expandedLines = expandMacroLines(rawLines);

        // Step 2: Pass 1 - Build Symbol Table (Label -> Address mapping)
        symbolTable = buildSymbolTable(expandedLines);

        // Step 3: Pass 2 - Tokenize lines and encode to machine code
        machineCode.clear();
        uint32_t addr = 0;

        for (const auto& line : expandedLines) {
            auto tokens = tokenizeLine(line);
            if (tokens.empty()) continue;

            // Skip label-only definitions (e.g. "loop_start:")
            bool isLabel = tokens.size() == 1 && tokens[0].back() == ':';
            if (isLabel) continue;

            // Build instruction and convert to binary word
            auto instr = buildInstruction(tokens, addr, symbolTable);
            uint32_t word = encode(instr);

            machineCode.push_back(word);
            addr += BYTES_PER_WORD;
        }

    } catch (const exception& e) {
        cerr << "Assembly Error: " << e.what() << "\n";
        return false;
    }

    return true;
}

bool Assembler::writeOutput(const string& outPath, bool asHex) {
    if (machineCode.empty()) {
        cerr << "Error: No machine code to write.\n";
        return false;
    }

    if (asHex) {
        ofstream outFile(outPath);
        if (!outFile.is_open()) return false;

        for (uint32_t word : machineCode) {
            outFile << "0x" << hex << setw(8) << setfill('0') << word << "\n";
        }
        outFile.close();
    } else {
        ofstream outFile(outPath, ios::binary);
        if (!outFile.is_open()) return false;

        for (uint32_t word : machineCode) {
            outFile.write(reinterpret_cast<const char*>(&word), sizeof(word));
        }
        outFile.close();
    }

    return true;
}
