#include "pass1.h"
#include "tokenizer.h"
#include "isa.h"
#include <stdexcept>

using namespace risc201;

namespace
{
    // Removes leading/trailing spaces and tabs.
    std::string trim(const std::string& text)
    {
        const size_t first = text.find_first_not_of(" \t\r");
        if (first == std::string::npos) {
            return "";
        }
        const size_t last = text.find_last_not_of(" \t\r");
        return text.substr(first, last - first + 1);
    }
}

std::vector<std::string> splitLabelLines(const std::vector<std::string>& rawLines)
{
    std::vector<std::string> result;
    result.reserve(rawLines.size());

    for (const std::string& line : rawLines) {
        // Work on the code part only; a ':' inside a comment is not a label.
        std::string rest = trim(line.substr(0, line.find(';')));

        // No colon means no label, so keep the line exactly as written.
        if (rest.find(':') == std::string::npos) {
            result.push_back(line);
            continue;
        }

        // Peel labels off the front one at a time.
        size_t colon;
        while ((colon = rest.find(':')) != std::string::npos) {
            const std::string name = trim(rest.substr(0, colon));
            if (name.empty() || name.find_first_of(" \t") != std::string::npos) {
                throw std::invalid_argument("malformed label in line: " + trim(line));
            }
            result.push_back(name + ":");
            rest = trim(rest.substr(colon + 1));
        }

        // Whatever is left after the last label is the instruction (if any).
        if (!rest.empty()) {
            result.push_back(rest);
        }
    }

    return result;
}

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
