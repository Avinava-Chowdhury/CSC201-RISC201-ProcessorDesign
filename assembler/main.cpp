#include "assembler.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <input.s> <output_file> [--hex] [--sym <symbols.txt>]\n";
        return 1;
    }

    std::string inputPath = argv[1];
    std::string outputPath = argv[2];
    bool asHex = false;
std::string symPath;
for (int i = 3; i < argc; ++i) {
    std::string flag = argv[i];
    if (flag == "--hex") {
        asHex = true;
    } else if (flag == "--sym" && i + 1 < argc) {
        symPath = argv[++i];
    } else {
        std::cerr << "Unknown or incomplete option: " << flag << "\n";
        return 1;
    }
}

    Assembler assembler;

    std::cout << "Reading " << inputPath << "...\n";
    if (!assembler.loadSourceFile(inputPath)) return 1;

    std::cout << "Assembling program (Pass 1 & Pass 2)...\n";
    if (!assembler.assemble()) return 1;

    std::cout << "Writing output to " << outputPath << "...\n";
   if (!symPath.empty() && !assembler.writeSymbolTable(symPath)) return 1;

    std::cout << "Done! Assembly successful.\n";
    return 0;
}
