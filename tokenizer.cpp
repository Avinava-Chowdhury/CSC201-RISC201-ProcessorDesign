#include "tokenizer.h"
#include "isa.h"
#include <sstream>
#include <stdexcept>
#include <cctype>

using namespace risc201;

std::vector<std::string> tokenizeLine(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string clean = line.substr(0, line.find(';'));

    // commas just get in the way, turn them into spaces
    for (char& c : clean) {
        if (c == ',') c = ' ';
    }

    // now split on whitespace
    std::istringstream stream(clean);
    std::string word;
    while (stream >> word) {
        tokens.push_back(word);
    }

    if (tokens.empty()) {
        return tokens;
    }

    // a label line is just one word ending in a colon, leave it as is
    // so whoever calls this can check for the colon and handle it separately
    if (tokens.size() == 1 && tokens[0].back() == ':') {
        return tokens;
    }

    // catch typos early - make sure the instruction name is real
    const OperationDetails* details = findByAssemblyText(tokens[0]);
    if (!details) {
        throw std::invalid_argument("unknown instruction: " + tokens[0]);
    }
   if (details->assemblyFormat == AssemblySyntax::JUMP_TO_LABEL ||
        details->assemblyFormat == AssemblySyntax::NO_ARGS) {
        return tokens;
    }
    // check every other token that looks like a register actually is one
    for (size_t i = 1; i < tokens.size(); i++) {
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