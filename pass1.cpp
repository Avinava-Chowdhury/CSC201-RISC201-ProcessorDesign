#include "pass1.h"
#include "tokenizer.h"
#include "isa.h"
#include <stdexcept>

using namespace risc201;

std::map<std::string, uint32_t> buildSymbolTable(const std::vector<std::string>& rawLines)
{
    std::map<std::string, uint32_t> symbols;
    uint32_t currentAddress = 0;

    for (const std::string& line : rawLines) {
        std::vector<std::string> tokens = tokenizeLine(line);

        if (tokens.empty()) {
            continue;
        }

        bool isLabelLine = (tokens.size() == 1 && tokens[0].back() == ':');

        if (isLabelLine) {
            std::string labelName = tokens[0].substr(0, tokens[0].size() - 1);

            if (symbols.count(labelName)) {
                throw std::invalid_argument("duplicate label: " + labelName);
            }

            symbols[labelName] = currentAddress;
        } else {
            currentAddress += BYTES_PER_WORD;
        }
    }

    return symbols;
}