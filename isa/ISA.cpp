// isa.cpp -- Implementation of the RISC201 instruction set.
//
// This section explains how the 32 bits of a single instruction are divided into different structural parts. 
// These sizes perfectly match the field widths declared in your isa.h file.
// The available components include a 5-bit opcode, a 4-bit destination register, a 4-bit source register, a 16-bit constant number, and a 27-bit branch offset.
//
// -----------------------------------------------------------------------
// OPERATE Instructions (Math, Logic, and Data Movement)
// -----------------------------------------------------------------------
// The layout for OPERATE instructions changes slightly depending on the Immediate Flag (I bit):
//
// Bits [31:27] : Opcode (5 bits) - This tells the processor exactly which operation to perform (like ADD or LSL).
// Bit  [26]    : Immediate Flag (1 bit) - If this switch is set to 1, the math uses a constant number. If 0, it uses a second register.
// Bits [25:22] : Destination Register (4 bits) - This points to the register where the final calculated answer will be saved.
// Bits [21:18] : Source Register 1 (4 bits) - This points to the first register providing data for the calculation.
//
// --- When the Immediate Flag is 0 (Using two registers) ---
// Bits [17:14] : Source Register 2 (4 bits) - This points to the second register providing data for the math.
// Bits [13:0]  : Unused (14 bits) - Since a constant number is not needed, these remaining bits are left completely blank (zero).
//
// --- When the Immediate Flag is 1 (Using one register and a constant number) ---
// Bits [17:16] : Constant Modifier (2 bits) - This tells the CPU how to interpret the constant number. 
//                A value of 0 treats it as a SIGNED number, 1 treats it as an UNSIGNED number, and 2 treats it as an UPPER half value.
// Bits [15:0]  : Raw Constant Value (16 bits) - This holds the actual 16-bit numerical value typed into the assembly code.
//
// -----------------------------------------------------------------------
// BRANCH Instructions (Jumps and Function Calls)
// -----------------------------------------------------------------------
// The layout for BRANCH instructions is much simpler because they only need an opcode and a destination:
//
// Bits [31:27] : Opcode (5 bits) - This identifies which specific jump instruction is being used.
// Bits [26:0]  : Branch Jump Distance (27 bits) - This is a large, sign-extended number representing how many steps forward or backward the program should jump.
#include "isa.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Everything below is defined INSIDE namespace risc201 so that these are
// definitions of the functions declared in isa.h (and not brand-new
// overloads at global scope, which is what caused the "ambiguous call" errors).
namespace risc201
{

// ---------------------------------------------------------------------
// Register naming
// ---------------------------------------------------------------------

namespace
{
    constexpr const char* kRegisterNames[TOTAL_REGISTERS] = {
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
        "r8", "r9", "r10", "r11", "r12", "r13", "sp", "ra"
    };
}

const char* getRegisterName(uint8_t registerIndex)
{
    if (registerIndex >= TOTAL_REGISTERS)
    {
        return "?";
    }
    return kRegisterNames[registerIndex];
}

int parseRegisterName(const std::string& originalName)
{
    
    std::string regName = originalName;
    for (char& c : regName)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (regName == "sp")
    {
        return STACK_POINTER_REG_INDEX;
    }
    if (regName == "ra")
    {
        return RETURN_ADDRESS_REG_INDEX;
    }

    if (regName.size() < 2 || regName[0] != 'r')
    {
        return -1;
    }

    for (size_t i = 1; i < regName.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(regName[i])))
        {
            return -1;
        }
    }

    // Guard against absurdly long digit strings (e.g. "r99999999999")
    // which would make std::stoi throw std::out_of_range.
    if (regName.size() > 3)
    {
        return -1;
    }

    int index = std::stoi(regName.substr(1));
    if (index < 0 || index >= TOTAL_REGISTERS)
    {
        return -1;
    }
    return index;
}

// ---------------------------------------------------------------------
// Operation lookup table
// ---------------------------------------------------------------------

namespace
{
    // A plain array (instead of std::array) so the compiler works out the
    // element count itself -- adding an instruction never needs a size edit.
    constexpr OperationDetails kOperationTable[] = {
        { "add",  OperationCode::ADD,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "sub",  OperationCode::SUB,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "mul",  OperationCode::MUL,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "div",  OperationCode::DIV,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "mod",  OperationCode::MOD,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "cmp",  OperationCode::CMP,  InstructionLayout::OPERATE, AssemblySyntax::COMPARE_ONLY,   true  },
        { "and",  OperationCode::AND,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "or",   OperationCode::OR,   InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "xor",  OperationCode::XOR,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "not",  OperationCode::NOT,  InstructionLayout::OPERATE, AssemblySyntax::SINGLE_ARG,     false },
        { "mov",  OperationCode::MOV,  InstructionLayout::OPERATE, AssemblySyntax::SINGLE_ARG,     true  },
        { "lsl",  OperationCode::LSL,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "lsr",  OperationCode::LSR,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "asr",  OperationCode::ASR,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "nop",  OperationCode::NOP,  InstructionLayout::OPERATE, AssemblySyntax::NO_ARGS,        false },
        { "ld",   OperationCode::LD,   InstructionLayout::OPERATE, AssemblySyntax::LOAD_MEMORY,    false },
        { "st",   OperationCode::ST,   InstructionLayout::OPERATE, AssemblySyntax::STORE_MEMORY,   false },
        { "beq",  OperationCode::BEQ,  InstructionLayout::BRANCH,  AssemblySyntax::JUMP_TO_LABEL,  false },
        { "bgt",  OperationCode::BGT,  InstructionLayout::BRANCH,  AssemblySyntax::JUMP_TO_LABEL,  false },
        { "b",    OperationCode::B,    InstructionLayout::BRANCH,  AssemblySyntax::JUMP_TO_LABEL,  false },
        { "call", OperationCode::CALL, InstructionLayout::BRANCH,  AssemblySyntax::JUMP_TO_LABEL,  false },
        { "ret",  OperationCode::RET,  InstructionLayout::OPERATE, AssemblySyntax::NO_ARGS,        false },
        { "neg",  OperationCode::NEG,  InstructionLayout::OPERATE, AssemblySyntax::SINGLE_ARG,     false },
        { "abs",  OperationCode::ABS,  InstructionLayout::OPERATE, AssemblySyntax::SINGLE_ARG,     false },
        { "min",  OperationCode::MIN,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "max",  OperationCode::MAX,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "rol",  OperationCode::ROL,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
        { "ror",  OperationCode::ROR,  InstructionLayout::OPERATE, AssemblySyntax::THREE_ARG_MATH, true  },
    };
    
}  // namespace

const OperationDetails* findByAssemblyText(const std::string& nameInAssembly)
{
    for (const auto& entry : kOperationTable)
    {
        if (nameInAssembly == entry.nameInAssembly)
        {
            return &entry;
        }
    }
    return nullptr;
}

const OperationDetails* findByOpcodeId(uint8_t numericOpcodeId)
{
    for (const auto& entry : kOperationTable)
    {
        if (static_cast<uint8_t>(entry.opCodeId) == numericOpcodeId)
        {
            return &entry;
        }
    }
    return nullptr;
}

const OperationDetails* findByOpcodeId(OperationCode opCodeId)
{
    return findByOpcodeId(static_cast<uint8_t>(opCodeId));
}

// ---------------------------------------------------------------------
// Constant / immediate expansion
// ---------------------------------------------------------------------

int32_t expandConstant(uint16_t rawValue, ImmediateModifier modifier)
{
    switch (modifier)
    {
    case ImmediateModifier::SIGNED:
        // Sign-extend the 16-bit raw value into a full 32-bit signed value.
        return extendSignBit(static_cast<uint32_t>(rawValue), CONSTANT_VALUE_BIT_COUNT);

    case ImmediateModifier::UNSIGNED:
        // Zero-extend: the bits are already the correct positive magnitude.
        return static_cast<int32_t>(static_cast<uint32_t>(rawValue));

    case ImmediateModifier::UPPER:
        // Shift the 16 bits into the top half of the word (used to build
        // 32-bit constants together with a lower/OR instruction).
        return static_cast<int32_t>(static_cast<uint32_t>(rawValue) << 16);
    }
    return 0;
}

int32_t DecodedInstruction::calculateFinalConstantValue() const
{
    return expandConstant(rawConstantValue, constantModifier);
}

// ---------------------------------------------------------------------
// Binary <-> DecodedInstruction conversion
// ---------------------------------------------------------------------

namespace
{
    constexpr uint32_t OPCODE_BIT_COUNT = 5;
    constexpr uint32_t REGISTER_BIT_COUNT = 4;

    constexpr uint32_t OPCODE_SHIFT = 27;
    constexpr uint32_t IMMEDIATE_FLAG_SHIFT = 26;
    constexpr uint32_t DEST_REG_SHIFT = 22;
    constexpr uint32_t SRC1_REG_SHIFT = 18;
    constexpr uint32_t SRC2_REG_SHIFT = 14;
    constexpr uint32_t MODIFIER_SHIFT = 16;

    constexpr uint32_t OPCODE_MASK = (1u << OPCODE_BIT_COUNT) - 1;
    constexpr uint32_t REGISTER_MASK = (1u << REGISTER_BIT_COUNT) - 1;
    constexpr uint32_t MODIFIER_MASK = 0x3u;
    constexpr uint32_t CONSTANT_MASK = (1u << CONSTANT_VALUE_BIT_COUNT) - 1;
    constexpr uint32_t BRANCH_OFFSET_MASK = (1u << BRANCH_OFFSET_BIT_COUNT) - 1;
}  // namespace

uint32_t convertToBinary(const DecodedInstruction& instructionStruct)
{
    const OperationDetails* details = findByOpcodeId(instructionStruct.operation);
    uint32_t word = (static_cast<uint32_t>(instructionStruct.operation) & OPCODE_MASK) << OPCODE_SHIFT;

    const bool isBranch = details != nullptr && details->layoutFormat == InstructionLayout::BRANCH;

    if (isBranch)
    {
        uint32_t offsetBits = static_cast<uint32_t>(instructionStruct.branchJumpDistance) & BRANCH_OFFSET_MASK;
        word |= offsetBits;
        return word;
    }

    // OPERATE layout.
    word |= (static_cast<uint32_t>(instructionStruct.destinationRegister) & REGISTER_MASK) << DEST_REG_SHIFT;
    word |= (static_cast<uint32_t>(instructionStruct.sourceRegister1) & REGISTER_MASK) << SRC1_REG_SHIFT;

    if (instructionStruct.usesConstantNumber)
    {
        word |= (1u << IMMEDIATE_FLAG_SHIFT);
        word |= (static_cast<uint32_t>(instructionStruct.constantModifier) & MODIFIER_MASK) << MODIFIER_SHIFT;
        word |= (static_cast<uint32_t>(instructionStruct.rawConstantValue) & CONSTANT_MASK);
    }
    else
    {
        word |= (static_cast<uint32_t>(instructionStruct.sourceRegister2) & REGISTER_MASK) << SRC2_REG_SHIFT;
    }

    return word;
}

std::optional<DecodedInstruction> convertFromBinary(uint32_t rawBinaryWord)
{
    const uint8_t opcodeId = static_cast<uint8_t>((rawBinaryWord >> OPCODE_SHIFT) & OPCODE_MASK);
    const OperationDetails* details = findByOpcodeId(opcodeId);
    if (details == nullptr)
    {
        return std::nullopt;  // Unknown opcode -- not a valid instruction.
    }

    DecodedInstruction instruction;
    instruction.operation = details->opCodeId;

    if (details->layoutFormat == InstructionLayout::BRANCH)
    {
        instruction.branchJumpDistance = extendSignBit(rawBinaryWord & BRANCH_OFFSET_MASK, BRANCH_OFFSET_BIT_COUNT);
        return instruction;
    }

    // OPERATE layout.
    instruction.destinationRegister = static_cast<uint8_t>((rawBinaryWord >> DEST_REG_SHIFT) & REGISTER_MASK);
    instruction.sourceRegister1 = static_cast<uint8_t>((rawBinaryWord >> SRC1_REG_SHIFT) & REGISTER_MASK);
    instruction.usesConstantNumber = ((rawBinaryWord >> IMMEDIATE_FLAG_SHIFT) & 0x1u) != 0;

    if (instruction.usesConstantNumber)
    {
        const uint32_t modifierBits = (rawBinaryWord >> MODIFIER_SHIFT) & MODIFIER_MASK;
        instruction.constantModifier = static_cast<ImmediateModifier>(modifierBits);
        instruction.rawConstantValue = static_cast<uint16_t>(rawBinaryWord & CONSTANT_MASK);
    }
    else
    {
        instruction.sourceRegister2 = static_cast<uint8_t>((rawBinaryWord >> SRC2_REG_SHIFT) & REGISTER_MASK);
    }

    return instruction;
}

// ---------------------------------------------------------------------
// Pipeline hazard classification helpers
// ---------------------------------------------------------------------

bool isMemoryLoad(OperationCode op)
{
    return op == OperationCode::LD;
}

bool isMemoryStore(OperationCode op)
{
    return op == OperationCode::ST;
}

bool isBranchLayout(OperationCode op)
{
    const OperationDetails* details = findByOpcodeId(op);
    return details != nullptr && details->layoutFormat == InstructionLayout::BRANCH;
}

bool isConditionalBranch(OperationCode op)
{
    return op == OperationCode::BEQ || op == OperationCode::BGT;
}

bool modifiesDestinationRegister(OperationCode op)
{
    switch (op)
    {
    case OperationCode::ADD: case OperationCode::SUB: case OperationCode::MUL:
    case OperationCode::DIV: case OperationCode::MOD:
    case OperationCode::AND: case OperationCode::OR:  case OperationCode::NOT:
    case OperationCode::XOR:
    case OperationCode::MOV:
    case OperationCode::LSL: case OperationCode::LSR: case OperationCode::ASR:
    case OperationCode::ROL: case OperationCode::ROR:
    case OperationCode::LD:
    case OperationCode::NEG: case OperationCode::ABS:
    case OperationCode::MIN: case OperationCode::MAX:
        return true;
    default:
        // CMP only sets flags; NOP/ST/branches/RET/CALL do not write the
        // destinationRegister field (CALL/RET touch the RA register
        // implicitly, not via the decoded destination field).
        return false;
    }
}

bool readsFirstSourceRegister(OperationCode op)
{
    const OperationDetails* details = findByOpcodeId(op);
    if (details == nullptr)
    {
        return false;
    }

    switch (details->assemblyFormat)
    {
    case AssemblySyntax::NO_ARGS:
        return false;  // nop, ret take no register operands
    case AssemblySyntax::JUMP_TO_LABEL:
        return false;  // branches only consume the jump offset
    default:
        return true;   // THREE_ARG_MATH, COMPARE_ONLY, SINGLE_ARG, LOAD_MEMORY, STORE_MEMORY all read sourceRegister1
    }
}

bool readsSecondSourceRegister(const DecodedInstruction& instruction)
{
    // A second source register is only meaningful for the OPERATE layout,
    // and only when the instruction is not using an immediate constant.
    if (instruction.usesConstantNumber)
    {
        return false;
    }

    const OperationDetails* details = findByOpcodeId(instruction.operation);
    if (details == nullptr)
    {
        return false;
    }

    switch (details->assemblyFormat)
    {
    case AssemblySyntax::THREE_ARG_MATH:
    case AssemblySyntax::COMPARE_ONLY:
        return true;   // these forms have a genuine second operand slot
    default:
        return false;  // SINGLE_ARG, LOAD_MEMORY, STORE_MEMORY, NO_ARGS, JUMP_TO_LABEL don't use sourceRegister2
    }
}

}  // namespace risc201
