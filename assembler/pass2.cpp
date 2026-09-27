#include "pass2.h"

#include <cctype>
#include <sstream>
#include <stdexcept>

using namespace risc201;

// =======================================================================
// Small private helpers
// =======================================================================
namespace
{
    std::string toLower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Same rule Khushi's tokenizer uses to decide "is this an operand a
    // number or a register": starts with a digit or a minus sign.
    bool looksLikeNumber(const std::string& tok)
    {
        return !tok.empty() && (std::isdigit(static_cast<unsigned char>(tok[0])) || tok[0] == '-');
    }

    int registerIndexOrThrow(const std::string& tok)
    {
        int idx = parseRegisterName(tok);
        if (idx < 0)
        {
            throw std::invalid_argument("bad register name: " + tok);
        }
        return idx;
    }

    // Parses a numeric literal such as "5", "-8", "0x1F", "5u", "0x1000h"
    // into a raw 16-bit field plus the modifier the suffix asked for.
    // Suffix rule (matches risc201::getModifierSuffix): a trailing 'u' means
    // UNSIGNED, a trailing 'h' means UPPER, no suffix means SIGNED. Neither
    // letter is a valid hex digit, so there is no ambiguity with a hex
    // literal like "0x1A".
    void parseImmediate(const std::string& tokenIn, uint16_t& rawValueOut, ImmediateModifier& modifierOut)
    {
        std::string tok = tokenIn;
        modifierOut = ImmediateModifier::SIGNED;

        if (!tok.empty())
        {
            char last = tok.back();
            if (last == 'u')
            {
                modifierOut = ImmediateModifier::UNSIGNED;
                tok.pop_back();
            }
            else if (last == 'h')
            {
                modifierOut = ImmediateModifier::UPPER;
                tok.pop_back();
            }
        }

        long parsed;
        try
        {
            // base 0 auto-detects a "0x" prefix; falls back to decimal.
            parsed = std::stol(tok, nullptr, 0);
        }
        catch (const std::exception&)
        {
            throw std::invalid_argument("not a valid number: " + tokenIn);
        }

        if (modifierOut == ImmediateModifier::SIGNED)
        {
            if (!canFitInSignedSpace(parsed, CONSTANT_VALUE_BIT_COUNT))
            {
                throw std::out_of_range("immediate " + tokenIn + " does not fit in a signed 16-bit field");
            }
        }
        else
        {
            if (!canFitInUnsignedSpace(parsed, CONSTANT_VALUE_BIT_COUNT))
            {
                throw std::out_of_range("immediate " + tokenIn + " does not fit in an unsigned 16-bit field");
            }
        }

        rawValueOut = static_cast<uint16_t>(parsed & 0xFFFF);
    }

    // Splits a memory operand like "20[r2]" into offset part "20" and
    // register part "r2". Also accepts "[r2]" (offset defaults to 0).
    bool splitMemoryOperand(const std::string& tok, std::string& offsetPart, std::string& regPart)
    {
        auto open = tok.find('[');
        auto close = tok.find(']');
        if (open == std::string::npos || close == std::string::npos || close < open)
        {
            return false;
        }
        offsetPart = tok.substr(0, open);
        regPart = tok.substr(open + 1, close - open - 1);
        if (offsetPart.empty())
        {
            offsetPart = "0";
        }
        return true;
    }

    void requireArgCount(const std::string& mnemonic, const std::vector<std::string>& tokens, size_t expected)
    {
        if (tokens.size() != expected)
        {
            throw std::invalid_argument(
                mnemonic + ": expected " + std::to_string(expected - 1) +
                " operand(s), got " + std::to_string(tokens.size() - 1));
        }
    }
}  // namespace

// =======================================================================
// Pass 2: buildInstruction / encode
// =======================================================================

DecodedInstruction buildInstruction(
    const std::vector<std::string>& tokens,
    uint32_t currentAddress,
    const std::map<std::string, uint32_t>& symbols)
{
    if (tokens.empty())
    {
        throw std::invalid_argument("buildInstruction: empty token line");
    }

    const std::string& mnemonic = tokens[0];
    const OperationDetails* details = findByAssemblyText(mnemonic);
    if (details == nullptr)
    {
        // Khushi's tokenizer should already have caught this, but Pass 2
        // never trusts an earlier stage blindly.
        throw std::invalid_argument("unknown instruction: " + mnemonic);
    }

    DecodedInstruction instr;
    instr.operation = details->opCodeId;

    switch (details->assemblyFormat)
    {
    case AssemblySyntax::NO_ARGS:
    {
        // nop, ret -- nothing else to fill in.
        requireArgCount(mnemonic, tokens, 1);
        break;
    }

    case AssemblySyntax::JUMP_TO_LABEL:
    {
        // beq loop_start / bgt loop_start / b loop_start / call loop_start
        requireArgCount(mnemonic, tokens, 2);
        const std::string& label = tokens[1];
        auto it = symbols.find(label);
        if (it == symbols.end())
        {
            throw std::invalid_argument("undefined label: " + label);
        }
        int64_t distance = static_cast<int64_t>(it->second) - static_cast<int64_t>(currentAddress);
        if (!canFitInSignedSpace(distance, BRANCH_OFFSET_BIT_COUNT))
        {
            throw std::out_of_range("branch distance to '" + label + "' does not fit in 27 bits");
        }
        instr.branchJumpDistance = static_cast<int32_t>(distance);
        break;
    }

    case AssemblySyntax::THREE_ARG_MATH:
    {
        // add r1, r2, r3    OR    add r1, r2, 5[u|h]
        requireArgCount(mnemonic, tokens, 4);
        instr.destinationRegister = registerIndexOrThrow(tokens[1]);
        instr.sourceRegister1 = registerIndexOrThrow(tokens[2]);

        if (looksLikeNumber(tokens[3]))
        {
            if (!details->allowsUorHSuffix)
            {
                throw std::invalid_argument(mnemonic + " does not accept an immediate operand");
            }
            instr.usesConstantNumber = true;
            parseImmediate(tokens[3], instr.rawConstantValue, instr.constantModifier);
        }
        else
        {
            instr.sourceRegister2 = registerIndexOrThrow(tokens[3]);
        }
        break;
    }

    case AssemblySyntax::COMPARE_ONLY:
    {
        // cmp r1, r2    OR    cmp r1, 5[u|h]
        requireArgCount(mnemonic, tokens, 3);
        instr.sourceRegister1 = registerIndexOrThrow(tokens[1]);

        if (looksLikeNumber(tokens[2]))
        {
            if (!details->allowsUorHSuffix)
            {
                throw std::invalid_argument(mnemonic + " does not accept an immediate operand");
            }
            instr.usesConstantNumber = true;
            parseImmediate(tokens[2], instr.rawConstantValue, instr.constantModifier);
        }
        else
        {
            instr.sourceRegister2 = registerIndexOrThrow(tokens[2]);
        }
        break;
    }

    case AssemblySyntax::SINGLE_ARG:
    {
        // mov r1, r2   OR   mov r1, 5[u|h]   (only when allowsUorHSuffix)
        // not r1, r2 / neg r1, r2 / abs r1, r2   (register operand only)
        requireArgCount(mnemonic, tokens, 3);
        instr.destinationRegister = registerIndexOrThrow(tokens[1]);

        if (looksLikeNumber(tokens[2]))
        {
            if (!details->allowsUorHSuffix)
            {
                throw std::invalid_argument(mnemonic + " does not accept an immediate operand");
            }
            instr.usesConstantNumber = true;
            parseImmediate(tokens[2], instr.rawConstantValue, instr.constantModifier);
        }
        else
        {
            instr.sourceRegister1 = registerIndexOrThrow(tokens[2]);
        }
        break;
    }

    case AssemblySyntax::LOAD_MEMORY:
    {
        // ld r1, 20[r2]  -- r1 is the destination, r2 is the base address register.
        requireArgCount(mnemonic, tokens, 3);
        instr.destinationRegister = registerIndexOrThrow(tokens[1]);

        std::string offsetPart, regPart;
        if (!splitMemoryOperand(tokens[2], offsetPart, regPart))
        {
            throw std::invalid_argument(mnemonic + ": expected 'offset[reg]', got '" + tokens[2] + "'");
        }
        instr.sourceRegister1 = registerIndexOrThrow(regPart);
        instr.usesConstantNumber = true;  // LD/ST always carry an immediate offset.
        parseImmediate(offsetPart, instr.rawConstantValue, instr.constantModifier);
        break;
    }

    case AssemblySyntax::STORE_MEMORY:
    {
        // st r1, 20[r2] -- r1 is the DATA being written, stashed in the
        // destinationRegister field (per the OPERATE layout note in isa.h);
        // r2 is the base address register.
        requireArgCount(mnemonic, tokens, 3);
        instr.destinationRegister = registerIndexOrThrow(tokens[1]);

        std::string offsetPart, regPart;
        if (!splitMemoryOperand(tokens[2], offsetPart, regPart))
        {
            throw std::invalid_argument(mnemonic + ": expected 'offset[reg]', got '" + tokens[2] + "'");
        }
        instr.sourceRegister1 = registerIndexOrThrow(regPart);
        instr.usesConstantNumber = true;
        parseImmediate(offsetPart, instr.rawConstantValue, instr.constantModifier);
        break;
    }
    }

    return instr;
}

uint32_t encode(const DecodedInstruction& instr)
{
    return convertToBinary(instr);
}

// =======================================================================
// Macro expansion (PUSH / POP)
// =======================================================================

std::vector<std::string> expandMacroLines(const std::vector<std::string>& rawLines)
{
    std::vector<std::string> expanded;
    expanded.reserve(rawLines.size());

    for (const std::string& line : rawLines)
    {
        // Peek at the line's first word ourselves -- we deliberately do NOT
        // call Khushi's tokenizeLine() here, since it would throw on
        // "push"/"pop" before we ever get a chance to expand them.
        std::string beforeComment = line.substr(0, line.find(';'));
        std::istringstream peek(beforeComment);

        std::string first;
        peek >> first;
        std::string lowered = toLower(first);

        if (lowered != "push" && lowered != "pop")
        {
            expanded.push_back(line);  // labels, comments, blank lines, real instructions -- untouched
            continue;
        }

        std::string regToken;
        peek >> regToken;
        if (!regToken.empty() && regToken.back() == ',')
        {
            regToken.pop_back();
        }
        if (regToken.empty())
        {
            throw std::invalid_argument(lowered + ": expected exactly one register operand");
        }

        if (lowered == "push")
        {
            expanded.push_back("sub sp, sp, 4");
            expanded.push_back("st " + regToken + ", 0[sp]");
        }
        else  // pop
        {
            expanded.push_back("ld " + regToken + ", 0[sp]");
            expanded.push_back("add sp, sp, 4");
        }
    }

    return expanded;
}

std::vector<std::vector<std::string>> expandMacros(
    const std::vector<std::vector<std::string>>& tokenLines)
{
    std::vector<std::vector<std::string>> expanded;
    expanded.reserve(tokenLines.size());

    for (const auto& tokens : tokenLines)
    {
        if (tokens.empty())
        {
            expanded.push_back(tokens);
            continue;
        }

        std::string mnemonic = toLower(tokens[0]);

        if (mnemonic == "push")
        {
            if (tokens.size() != 2)
            {
                throw std::invalid_argument("push: expected exactly one register operand");
            }
            expanded.push_back({"sub", "sp", "sp", "4"});
            expanded.push_back({"st", tokens[1], "0[sp]"});
        }
        else if (mnemonic == "pop")
        {
            if (tokens.size() != 2)
            {
                throw std::invalid_argument("pop: expected exactly one register operand");
            }
            expanded.push_back({"ld", tokens[1], "0[sp]"});
            expanded.push_back({"add", "sp", "sp", "4"});
        }
        else
        {
            expanded.push_back(tokens);
        }
    }

    return expanded;
}
