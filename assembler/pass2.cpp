// pass2.cpp -- Pass 2 of the RISC201 assembler.
//
//   buildInstruction()   tokens + symbol table  ->  DecodedInstruction
//   encode()             DecodedInstruction     ->  32-bit machine word
//   expandMacroLines()   PUSH / POP / LI on raw text lines
#include "pass2.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>

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

    // Is an operand a number (as opposed to a register)? It starts with a
    // digit or a sign. Same rule the tokenizer uses (plus '+').
    bool looksLikeNumber(const std::string& tok)
    {
        if (tok.empty())
        {
            return false;
        }
        const unsigned char c = static_cast<unsigned char>(tok[0]);
        return std::isdigit(c) || c == '-' || c == '+';
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

    bool isMacroName(const std::string& lowercaseName)
    {
        return lowercaseName == "push" || lowercaseName == "pop" || lowercaseName == "li";
    }

    // ---------------------------------------------------------------
    // Number parsing
    // ---------------------------------------------------------------

    int digitValue(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // Strictly parses an integer literal:
    //     [+|-] decimal      e.g.  10   -8   +5   010 (this is TEN, not octal)
    //     [+|-] 0x hex       e.g.  0x1F   -0x10   0XFF
    // The WHOLE text must be a number -- "5abc" is an error, not 5.
    // `original` is only used to make the error messages readable.
    int64_t parseInteger(const std::string& text, const std::string& original)
    {
        size_t pos = 0;
        bool negative = false;
        if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
        {
            negative = (text[pos] == '-');
            ++pos;
        }

        int base = 10;
        if (pos + 1 < text.size() && text[pos] == '0' && (text[pos + 1] == 'x' || text[pos + 1] == 'X'))
        {
            base = 16;
            pos += 2;
        }

        if (pos >= text.size())
        {
            throw std::invalid_argument("not a valid number: " + original);
        }

        // Anything beyond 2^40 is out of range for every field we have, so we
        // stop there instead of risking arithmetic overflow on huge digit strings.
        constexpr uint64_t HARD_LIMIT = (1ull << 40);
        uint64_t magnitude = 0;
        for (; pos < text.size(); ++pos)
        {
            const int digit = digitValue(text[pos]);
            if (digit < 0 || digit >= base)
            {
                throw std::invalid_argument("not a valid number: " + original);
            }
            magnitude = magnitude * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
            if (magnitude > HARD_LIMIT)
            {
                throw std::out_of_range("number is far too large: " + original);
            }
        }

        const int64_t value = static_cast<int64_t>(magnitude);
        return negative ? -value : value;
    }

    // Parses an immediate such as "5", "-8", "0x1F", "5u", "0x1000h", "5U"
    // into a raw 16-bit field plus the modifier the suffix asked for.
    // A trailing u/U means UNSIGNED, a trailing h/H means UPPER, no suffix means
    // SIGNED. Neither letter is a hex digit, so there is no clash with "0x1A".
    void parseImmediate(const std::string& tokenIn, uint16_t& rawValueOut, ImmediateModifier& modifierOut)
    {
        std::string tok = tokenIn;
        modifierOut = ImmediateModifier::SIGNED;

        if (!tok.empty())
        {
            const char last = static_cast<char>(std::tolower(static_cast<unsigned char>(tok.back())));
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

        const int64_t parsed = parseInteger(tok, tokenIn);

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
    // Returns false if the operand is not shaped like  offset[reg]  --
    // including when there is any extra text after the closing ']'.
    bool splitMemoryOperand(const std::string& tok, std::string& offsetPart, std::string& regPart)
    {
        const auto open = tok.find('[');
        const auto close = tok.find(']');
        if (open == std::string::npos || close == std::string::npos || close < open)
        {
            return false;
        }
        if (close != tok.size() - 1)
        {
            return false;   // e.g. "20[r2]junk"
        }
        offsetPart = tok.substr(0, open);
        regPart = tok.substr(open + 1, close - open - 1);
        if (offsetPart.empty())
        {
            offsetPart = "0";
        }
        return true;
    }

    void parseMemoryOffset(const std::string& mnemonic, const std::string& offsetPart,
                           uint16_t& rawValueOut, ImmediateModifier& modifierOut)
    {
        parseImmediate(offsetPart, rawValueOut, modifierOut);
        if (modifierOut != ImmediateModifier::SIGNED)
        {
            throw std::invalid_argument(
                mnemonic + ": memory offset must be a plain signed number (no u/h suffix): " + offsetPart);
        }
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
    std::string hex(uint32_t value)
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "0x%X", static_cast<unsigned>(value));
        return buffer;
    }

    // li rX, <32-bit constant>
    //
    // Loads any 32-bit number into a register. The size depends only on the
    // literal, so it is known before Pass 1 runs:
    //   fits a signed 16-bit value      ->  mov rX, v                  (1 instruction)
    //   0x8000 .. 0xFFFF                ->  mov rX, 0xVVVVu            (1 instruction)
    //   low 16 bits are zero            ->  mov rX, 0xHHHHh            (1 instruction)
    //   anything else                   ->  mov rX, 0xHHHHh            (2 instructions)
    //                                       or  rX, rX, 0xLLLLu
    // where HHHH / LLLL are the upper / lower 16 bits of the constant.
    std::vector<std::vector<std::string>> expandLi(const std::vector<std::string>& tokens)
    {
        if (tokens.size() != 3)
        {
            throw std::invalid_argument(
                "li: expected 2 operand(s) (a register and a number), got " + std::to_string(tokens.size() - 1));
        }
        const std::string& reg = tokens[1];
        const std::string& number = tokens[2];

        if (parseRegisterName(reg) < 0)
        {
            throw std::invalid_argument("li: bad register name: " + reg);
        }
        if (!looksLikeNumber(number))
        {
            throw std::invalid_argument("li: second operand must be a number, got '" + number + "'");
        }
        const char lastChar = static_cast<char>(std::tolower(static_cast<unsigned char>(number.back())));
        if (lastChar == 'u' || lastChar == 'h')
        {
            throw std::invalid_argument("li: takes a plain number (no u/h suffix): " + number);
        }

        const int64_t value = parseInteger(number, number);
        if (value < INT32_MIN || value > static_cast<int64_t>(UINT32_MAX))
        {
            throw std::out_of_range("li: " + number + " does not fit in 32 bits");
        }

        const uint32_t bits = static_cast<uint32_t>(value);   // two's-complement view of the constant
        const uint32_t upper16 = bits >> 16;
        const uint32_t lower16 = bits & 0xFFFFu;

        if (value >= -32768 && value <= 32767)
        {
            return { { "mov", reg, std::to_string(value) } };
        }
        if (value >= 0 && value <= 0xFFFF)
        {
            return { { "mov", reg, hex(lower16) + "u" } };
        }
        if (lower16 == 0)
        {
            return { { "mov", reg, hex(upper16) + "h" } };
        }
        return {
            { "mov", reg, hex(upper16) + "h" },
            { "or",  reg, reg, hex(lower16) + "u" }
        };
    }

    // Expands ONE already-split line. Non-macro lines come back unchanged
    // (as a single-line list).
    std::vector<std::vector<std::string>> expandOneLine(const std::vector<std::string>& tokens)
    {
        if (tokens.empty())
        {
            return { tokens };
        }

        const std::string mnemonic = toLower(tokens[0]);

        if (mnemonic == "push")
        {
            if (tokens.size() != 2)
            {
                throw std::invalid_argument("push: expected exactly one register operand");
            }
            if (toLower(tokens[1]) == "sp" || toLower(tokens[1]) == "r14")
            {
                throw std::invalid_argument("push: sp cannot be the operand (the macro itself changes sp)");
            }
            return {
                { "sub", "sp", "sp", "4" },       // move the stack pointer down by 4 bytes
                { "st", tokens[1], "0[sp]" }      // store the register at the new top of stack
            };
        }

        if (mnemonic == "pop")
        {
            if (tokens.size() != 2)
            {
                throw std::invalid_argument("pop: expected exactly one register operand");
            }
            if (toLower(tokens[1]) == "sp" || toLower(tokens[1]) == "r14")
            {
                throw std::invalid_argument("pop: sp cannot be the operand (the macro itself changes sp)");
            }
            return {
                { "ld", tokens[1], "0[sp]" },     // load the value at the top of stack
                { "add", "sp", "sp", "4" }        // move the stack pointer back up
            };
        }

        if (mnemonic == "li")
        {
            return expandLi(tokens);
        }

        return { tokens };
    }

    // Comment-stripped, comma-free word list for one raw source line.
    std::vector<std::string> splitWords(const std::string& line)
    {
        std::string clean = line;

        // Safely strip all comment types before executing macro logic
        size_t commentHash = clean.find('#');
        if (commentHash != std::string::npos) clean = clean.substr(0, commentHash);
        
        size_t commentSemi = clean.find(';');
        if (commentSemi != std::string::npos) clean = clean.substr(0, commentSemi);
        
        size_t commentSlash = clean.find("//");
        if (commentSlash != std::string::npos) clean = clean.substr(0, commentSlash);

        for (char& c : clean)
        {
            if (c == ',') c = ' ';
        }
        std::istringstream stream(clean);
        std::vector<std::string> words;
        std::string word;
        while (stream >> word)
        {
            words.push_back(word);
        }
        return words;
    }

    // {"st","r1","0[sp]"} -> "st r1, 0[sp]"
    std::string joinWords(const std::vector<std::string>& words)
    {
        std::string line = words[0];
        for (size_t i = 1; i < words.size(); ++i)
        {
            line += (i == 1) ? " " : ", ";
            line += words[i];
        }
        return line;
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
    const std::string lowered = toLower(mnemonic);

    if (isMacroName(lowered))
    {
        throw std::invalid_argument(
            mnemonic + " is a macro, not a real instruction -- run expandMacroLines() first");
    }

    const OperationDetails* details = findByAssemblyText(lowered);
    if (details == nullptr)
    {
        // The tokenizer should already have caught this, but Pass 2
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
        instr.destinationRegister = static_cast<uint8_t>(registerIndexOrThrow(tokens[1]));
        instr.sourceRegister1 = static_cast<uint8_t>(registerIndexOrThrow(tokens[2]));

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
            instr.sourceRegister2 = static_cast<uint8_t>(registerIndexOrThrow(tokens[3]));
        }
        break;
    }

    case AssemblySyntax::COMPARE_ONLY:
    {
        // cmp r1, r2    OR    cmp r1, 5[u|h]
        requireArgCount(mnemonic, tokens, 3);
        instr.sourceRegister1 = static_cast<uint8_t>(registerIndexOrThrow(tokens[1]));

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
            instr.sourceRegister2 = static_cast<uint8_t>(registerIndexOrThrow(tokens[2]));
        }
        break;
    }

    case AssemblySyntax::SINGLE_ARG:
    {
        // mov r1, r2   OR   mov r1, 5[u|h]   (only when allowsUorHSuffix)
        // not r1, r2 / neg r1, r2 / abs r1, r2   (register operand only)
        requireArgCount(mnemonic, tokens, 3);
        instr.destinationRegister = static_cast<uint8_t>(registerIndexOrThrow(tokens[1]));

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
            instr.sourceRegister1 = static_cast<uint8_t>(registerIndexOrThrow(tokens[2]));
        }
        break;
    }

    case AssemblySyntax::LOAD_MEMORY:
    {
        // ld r1, 20[r2]  -- r1 is the destination, r2 is the base address register.
        requireArgCount(mnemonic, tokens, 3);
        instr.destinationRegister = static_cast<uint8_t>(registerIndexOrThrow(tokens[1]));

        std::string offsetPart, regPart;
        if (!splitMemoryOperand(tokens[2], offsetPart, regPart))
        {
            throw std::invalid_argument(mnemonic + ": expected 'offset[reg]', got '" + tokens[2] + "'");
        }
        instr.sourceRegister1 = static_cast<uint8_t>(registerIndexOrThrow(regPart));
        instr.usesConstantNumber = true;  // LD/ST always carry an immediate offset.
        parseMemoryOffset(mnemonic, offsetPart, instr.rawConstantValue, instr.constantModifier);
        break;
    }

    case AssemblySyntax::STORE_MEMORY:
    {
        // st r1, 20[r2] -- r1 is the DATA being written, stashed in the
        // destinationRegister field (per the OPERATE layout note in isa.h);
        // r2 is the base address register.
        requireArgCount(mnemonic, tokens, 3);
        instr.destinationRegister = static_cast<uint8_t>(registerIndexOrThrow(tokens[1]));

        std::string offsetPart, regPart;
        if (!splitMemoryOperand(tokens[2], offsetPart, regPart))
        {
            throw std::invalid_argument(mnemonic + ": expected 'offset[reg]', got '" + tokens[2] + "'");
        }
        instr.sourceRegister1 = static_cast<uint8_t>(registerIndexOrThrow(regPart));
        instr.usesConstantNumber = true;
        parseMemoryOffset(mnemonic, offsetPart, instr.rawConstantValue, instr.constantModifier);
        break;
    }
    }

    return instr;
}

uint32_t encode(const DecodedInstruction& instr)
{
    return convertToBinary(instr);
}


bool isWordDirective(const std::vector<std::string>& tokens)
{
    return !tokens.empty() && toLower(tokens[0]) == ".word";
}

uint32_t parseWordDirective(const std::vector<std::string>& tokens)
{
    if (tokens.size() != 2)
        throw std::invalid_argument(".word: expected exactly one number");
    if (!looksLikeNumber(tokens[1]))
        throw std::invalid_argument(".word: operand must be a number, got '" + tokens[1] + "'");
    const int64_t value = parseInteger(tokens[1], tokens[1]);
    if (value < INT32_MIN || value > static_cast<int64_t>(UINT32_MAX))
        throw std::out_of_range(".word: " + tokens[1] + " does not fit in 32 bits");
    return static_cast<uint32_t>(value);
}

// =======================================================================
// Macro expansion (PUSH / POP / LI)
// =======================================================================

std::vector<std::string> expandMacroLines(const std::vector<std::string>& rawLines)
{
    std::vector<std::string> expanded;
    expanded.reserve(rawLines.size());

    for (const std::string& line : rawLines)
    {
        std::vector<std::string> words = splitWords(line);

        // Labels, comments, blank lines and real instructions pass through
        // untouched. (We deliberately do NOT call tokenizeLine() here: it
        // would throw on "push"/"pop"/"li" before we could expand them.)
        if (words.empty() || !isMacroName(toLower(words[0])))
        {
            expanded.push_back(line);
            continue;
        }

        for (const std::vector<std::string>& piece : expandOneLine(words))
        {
            expanded.push_back(joinWords(piece));
        }
    }

    return expanded;
}
