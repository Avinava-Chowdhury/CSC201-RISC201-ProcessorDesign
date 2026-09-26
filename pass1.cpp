#include "pass1.h"
#include "tokenizer.h"
#include "isa.h"
#include <stdexcept>

namespace
{
    bool isLabelLine(const std::vector<std::string>& tokens)
    {
        return tokens.size() == 1 && tokens[0].back() == ':';
    }

    std::string stripColon(const std::string& labelToken)
    {
        return labelToken.substr(0, labelToken.size() - 1);
    }
}

SymbolTableResult buildSymbolTable(const std::vector<std::string>& lines)
{
    SymbolTableResult result;
    uint32_t currentAddress = 0;

    for (size_t lineNumber = 0; lineNumber < lines.size(); lineNumber++)
    {
        std::vector<std::string> tokens;
        try
        {
            tokens = tokenizeLine(lines[lineNumber]);
        }
        catch (const std::invalid_argument& e)
        {
            result.errors.push_back(
                "line " + std::to_string(lineNumber + 1) + ": " + e.what());
            currentAddress += risc201::BYTES_PER_WORD;
            continue;
        }

        if (tokens.empty())
        {
            continue;
        }

        if (isLabelLine(tokens))
        {
            std::string labelName = stripColon(tokens[0]);
            if (result.symbols.count(labelName))
            {
                result.errors.push_back(
                    "line " + std::to_string(lineNumber + 1) +
                    ": duplicate label '" + labelName + "'");
                continue;
            }
            result.symbols[labelName] = currentAddress;
            continue;
        }

        currentAddress += risc201::BYTES_PER_WORD;
    }

    return result;
}