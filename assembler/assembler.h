#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <string>
#include <vector>
#include <cstdint>
#include <map>

class Assembler {
public:
    Assembler() = default;

    // Read the .s assembly file line by line
    bool loadSourceFile(const std::string& path);

    // Run macro expansion, Pass 1, and Pass 2
    bool assemble();

    // Write machine code as binary or hexadecimal text
    bool writeOutput(const std::string& outPath, bool asHex = false);

    // Export the symbol table for the simulator[cite: 7]
    bool writeSymbolTable(const std::string& symPath);

private:
    std::vector<std::string> rawLines;
    std::vector<uint32_t> machineCode;
    std::map<std::string, uint32_t> symbolTable;
};

#endif // ASSEMBLER_H
