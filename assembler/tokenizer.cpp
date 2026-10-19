#include "tokenizer.h"
#include "isa.h"
#include <sstream>
#include <stdexcept>
#include <cctype>

using namespace risc201;

std::vector<std::string> tokenizeLine(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string clean = line;

    // 1. Strip out human comments (handle ;, #, and //)
    size_t commentHash = clean.find('#');
    if (commentHash != std::string::npos) clean = clean.substr(0, commentHash);
    
    size_t commentSemi = clean.find(';');
    if (commentSemi != std::string::npos) clean = clean.substr(0, commentSemi);
    
    size_t commentSlash = clean.find("//");
    if (commentSlash != std::string::npos) clean = clean.substr(0, commentSlash);

    // 2. Commas just get in the way, turn them into spaces
    for (char& c : clean) {
        if (c == ',') c = ' ';
    }

    // 3. Now split on whitespace
    std::istringstream stream(clean);
    std::string word;
    while (stream >> word) {
        tokens.push_back(word);
    }

    if (tokens.empty()) {
        return tokens;
    }

    // A lone label line, just one word ending in a colon
    if (tokens.size() == 1 && tokens[0].back() == ':') {
        return tokens;
    }

    // A label followed by an instruction on the same line,
    // e.g. "start: add r1, r2, r3" - leave the label token where it is,
    // just validate the instruction part that comes after it
    size_t instructionStart = 0;
    if (tokens[0].back() == ':') {
        instructionStart = 1;
        if (instructionStart == tokens.size()) {
            return tokens; // label only, nothing follows it
        }
    }

    // Mnemonics aren't case sensitive, normalize before lookup
    std::string& mnemonic = tokens[instructionStart];
    for (char& c : mnemonic) {
        c = (char)tolower((unsigned char)c);
    }

    // Make sure the instruction name is real
    const OperationDetails* details = findByAssemblyText(mnemonic);
    if (!details) {
        throw std::invalid_argument("unknown instruction: " + mnemonic);
    }
    
    // Format operands for Memory Load/Store
    if (details->assemblyFormat == AssemblySyntax::LOAD_MEMORY ||
        details->assemblyFormat == AssemblySyntax::STORE_MEMORY) {
        const size_t operandStart = instructionStart + 2;   // after mnemonic and register
        if (tokens.size() > operandStart + 1) {
            std::string memoryOperand;
            // Compress "4", "[sp]" into "4[sp]" in case of arbitrary spacing
            for (size_t i = operandStart; i < tokens.size(); i++) {
                memoryOperand += tokens[i];
            }
            tokens.resize(operandStart);
            tokens.push_back(memoryOperand);
        }
        return tokens;
    }

    if (details->assemblyFormat == AssemblySyntax::JUMP_TO_LABEL ||
        details->assemblyFormat == AssemblySyntax::NO_ARGS) {
        return tokens;
    }

    // Check every other token that looks like a register actually is one
    for (size_t i = instructionStart + 1; i < tokens.size(); i++) {
        bool looksLikeNumber = isdigit((unsigned char)tokens[i][0]) || tokens[i][0] == '-';
        if (!looksLikeNumber) {
            int reg = parseRegisterName(tokens[i]);
            if (reg < 0) {
                throw std::invalid_argument("bad register name: " + tokens[i]);
            }
        }
    }

    return tokens;
}
