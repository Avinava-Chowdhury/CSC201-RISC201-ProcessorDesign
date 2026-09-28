#include "disassembler.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <input> <output.s> [--hex] [--addr]\n"
                  << "  --hex   input is a text file of hex words (assembler --hex output)\n"
                  << "  --addr  add '; address: word' comments to each line\n";
        return 1;
    }

    std::string inputPath = argv[1];
    std::string outputPath = argv[2];
    bool asHex = false, showAddr = false;
    for (int i = 3; i < argc; ++i) {
        std::string flag = argv[i];
        if (flag == "--hex") asHex = true;
        else if (flag == "--addr") showAddr = true;
        else { std::cerr << "Unknown option: " << flag << "\n"; return 1; }
    }

    Disassembler dis;
    std::cout << "Reading " << inputPath << "...\n";
    if (!dis.loadInput(inputPath, asHex)) return 1;

    std::cout << "Disassembling " << dis.instructionCount() << " instruction(s)...\n";
    if (!dis.disassemble()) return 1;

    std::cout << "Writing output to " << outputPath << "...\n";
    if (!dis.writeOutput(outputPath, showAddr)) return 1;

    std::cout << "Done! Disassembly successful.\n";
    return 0;
}