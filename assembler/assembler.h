#ifndef ASSEMBLER_H
#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <string>
#include <vector>
#include <cstdint>
#include <map>

class Assembler {
public:
    Assembler() = default;

    // 1. Reads the .s assembly file line by line into memory
    bool loadSourceFile(const std::string& path);

    // 2. Runs Pass 1 (Symbol Table) and Pass 2 (Encoding)
    bool assemble();

    // 3. Writes the output to disk as raw binary or hex text
    bool writeOutput(const std::string& outPath, bool asHex = false);

private:
    std::vector<std::string> rawLines;
    std::vector<uint32_t> machineCode;
    std::map<std::string, uint32_t> symbolTable;
};

#endif // ASSEMBLER_H
