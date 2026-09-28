#include "assembler.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <input.s> <output_file> [--hex]\n";
        return 1;
    }

    std::string inputPath = argv[1];
    std::string outputPath = argv[2];
    bool asHex = (argc > 3 && std::string(argv[3]) == "--hex");

    Assembler assembler;

    std::cout << "Reading " << inputPath << "...\n";
    if (!assembler.loadSourceFile(inputPath)) return 1;

    std::cout << "Assembling program (Pass 1 & Pass 2)...\n";
    if (!assembler.assemble()) return 1;

    std::cout << "Writing output to " << outputPath << "...\n";
    if (!assembler.writeOutput(outputPath, asHex)) return 1;

    std::cout << "Done! Assembly successful.\n";
    return 0;
}
