// isa.h -- RISC201 instruction set definition
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace risc201 
{


// Processor Architecture Constants

constexpr int      TOTAL_REGISTERS = 16;
constexpr uint8_t  STACK_POINTER_REG_INDEX = 14; // Register 14 acts as the Stack Pointer (SP)
constexpr uint8_t  RETURN_ADDRESS_REG_INDEX = 15; // Register 15 holds the Return Address (RA)
constexpr uint32_t BYTES_PER_WORD = 4;           // Each instruction and data block is 4 bytes (32 bits)

// Memory constraints. We map out two distinct neighborhoods of memory:
constexpr uint32_t INSTRUCTION_MEMORY_SIZE = 0x4000;  // 16 KB reserved for storing the program's code
constexpr uint32_t DATA_MEMORY_SIZE = 0x10000;        // 64 KB reserved for program data and the stack

// The stack grows downwards like a root system. 
// When you push data, the stack pointer (SP) decreases.
// Example: If SP is at 0x10000 and you push a 4-byte word, SP drops to 0x0FFFC.
constexpr uint32_t STACK_STARTING_ADDRESS = 0x10000;  // Where the stack begins (empty)
constexpr uint32_t STACK_LOWEST_LIMIT = 0x0C000;      // The absolute floor the stack can reach without crashing (16 KB max)

const char* getRegisterName(uint8_t registerIndex);          // Converts 0 to "r0", 14 to "sp", etc.
int         parseRegisterName(const std::string& regName);   // Converts "sp" or "r7" back into the integer index.


// Operation Codes (Opcodes)
// Every action the CPU can take is assigned a unique 5-bit ID. 

enum class OperationCode : uint8_t 
{
    ADD = 0,  SUB = 1,  MUL = 2,  DIV = 3,  MOD = 4,
    CMP = 5,
    AND = 6,  OR  = 7,
    NOT = 8,  MOV = 9,
    LSL = 10, LSR = 11, ASR = 12,
    NOP = 13,
    LD  = 14, ST  = 15,
    BEQ = 16, BGT = 17, B = 18, CALL = 19, RET = 20
};


// Instruction Layouts
// Think of the 32-bit instruction as a train with specific cargo cars. 
// There are only two ways to arrange the cars:

enum class InstructionLayout : uint8_t 
{ 
    OPERATE, // Used for math, logic, and memory (uses specific registers and small constants)
    BRANCH   // Used for jumping around code (uses the opcode and one massive 27-bit jump distance)
};

/* 
   Visualizing the OPERATE layout:
   Imagine the instruction: add r1, r2, 5
   Bits 31-27: Opcode (ADD = 00000)
   Bit 26:     Immediate Flag (1, because we are using the number '5' instead of a register)
   Bits 25-22: Destination Register (r1 = 0001)
   Bits 21-18: Source Register 1 (r2 = 0010)
   Bits 17-0:  The raw number 5 (000000000000000101)
*/

// Assembly Syntax categories define how the human-readable text translates to the machine fields.
enum class AssemblySyntax : uint8_t {
    THREE_ARG_MATH, // e.g., add  r1, r2, r3  (Writes to r1, reads r2 and r3)
    COMPARE_ONLY,   // e.g., cmp  r1, 5       (Reads r1 and 5, but writes to no registers)
    SINGLE_ARG,     // e.g., mov  r1, r2      (Writes to r1, reads r2. Skips the first source register entirely)
    LOAD_MEMORY,    // e.g., ld   r1, 20[r2]  
    STORE_MEMORY,   // e.g., st   r1, 20[r2]  (Note: r1 is the data being saved, not the destination!)
    JUMP_TO_LABEL,  // e.g., beq  loop_start
    NO_ARGS         // e.g., nop, ret
};

// Modifiers change how a 16-bit constant is expanded to fill a full 32-bit space inside the CPU.
enum class ImmediateModifier : uint8_t { 
    SIGNED = 0,   // Keeps negative numbers negative (e.g., -5 stays -5)
    UNSIGNED = 1, // Treats the bits as a pure positive number (u suffix)
    UPPER = 2     // Shifts the 16 bits into the top half of the 32-bit space (h suffix)
};

inline const char* getModifierSuffix(ImmediateModifier mod) {
    switch (mod) {
    case ImmediateModifier::UNSIGNED: return "u";
    case ImmediateModifier::UPPER:    return "h";
    default:                          return "";
    }
}

// A dictionary entry explaining exactly how a specific operation behaves.
struct OperationDetails {
    const char*        nameInAssembly; 
    OperationCode      opCodeId;
    InstructionLayout  layoutFormat;
    AssemblySyntax     assemblyFormat;
    bool               allowsUorHSuffix; 
};

const OperationDetails* findByAssemblyText(const std::string& nameInAssembly); 
const OperationDetails* findByOpcodeId(uint8_t numericOpcodeId);                  
const OperationDetails* findByOpcodeId(OperationCode opCodeId);


// The Decoded Instruction
// This breaks apart the raw 32-bit binary into labeled, easy-to-use variables 
// so the processor pipeline can route the data to the ALU or memory.

struct DecodedInstruction {
    OperationCode     operation = OperationCode::NOP;
    uint8_t           destinationRegister = 0; // Where the final answer goes
    uint8_t           sourceRegister1 = 0;     // The first input
    uint8_t           sourceRegister2 = 0;     // The second input (ignored if using a constant number)
    
    bool              usesConstantNumber = false; // The "I" bit. True if using a raw number instead of sourceRegister2
    ImmediateModifier constantModifier = ImmediateModifier::SIGNED; 
    uint16_t          rawConstantValue = 0;       // The raw 16-bit number from the instruction
    
    int32_t           branchJumpDistance = 0;     // How far to jump for BRANCH layouts

    // Example proof: If rawConstantValue is 0xFFFF (-1 in 16-bit signed) and modifier is SIGNED, 
    // this function expands it to 0xFFFFFFFF (-1 in full 32-bit) before sending it to the ALU.
    int32_t calculateFinalConstantValue() const;
};

int32_t expandConstant(uint16_t rawValue, ImmediateModifier modifier);

const int BRANCH_OFFSET_BIT_COUNT = 27;
const int CONSTANT_VALUE_BIT_COUNT = 16;

inline bool canFitInSignedSpace(int64_t value, int availableBits) {
    return value >= -(int64_t(1) << (availableBits - 1)) && value < (int64_t(1) << (availableBits - 1));
}
inline bool canFitInUnsignedSpace(int64_t value, int availableBits) {
    return value >= 0 && value < (int64_t(1) << availableBits);
}

// Converts a smaller signed binary number into a full 32-bit signed binary number.
inline int32_t extendSignBit(uint32_t value, int originalBitCount) {
    value &= (originalBitCount == 32) ? 0xFFFFFFFFu : ((1u << originalBitCount) - 1);
    uint32_t signMask = 1u << (originalBitCount - 1);
return static_cast<int32_t>((value ^ signMask) - signMask);}

uint32_t convertToBinary(const DecodedInstruction& instructionStruct);
std::optional<DecodedInstruction> convertFromBinary(uint32_t rawBinaryWord);

// Processor Status Flags
// A hidden scratchpad that remembers the result of the last 'cmp' (compare) instruction.
// Example: If you run `cmp r1, r2` where r1=10 and r2=5, the ALU sets GreaterThan to true.

struct ProcessorFlags {
    bool IsEqual      = false; // 'E' flag: Set to true if inputs were exactly identical
    bool IsGreaterThan = false; // 'GT' flag: Set to true if the first input was strictly larger
};

inline ProcessorFlags evaluateComparison(int32_t inputA, int32_t inputB) {
    ProcessorFlags flags;
    flags.IsEqual      = (inputA == inputB);
    flags.IsGreaterThan = (inputA > inputB);
    return flags;
}


// Pipeline Hazard Classification Helpers
// These let the CPU check what an instruction is about to do before it does it,
// which is necessary for coordinating the clock cycles of a 4-stage or 6-stage pipeline.

bool isMemoryLoad(OperationCode op);             
bool isMemoryStore(OperationCode op);            
bool isBranchLayout(OperationCode op);           // Checks if it uses the massive 27-bit jump offset
bool isConditionalBranch(OperationCode op);      // Checks if it needs to read the ProcessorFlags (beq, bgt)
bool modifiesDestinationRegister(OperationCode op); 
bool readsFirstSourceRegister(OperationCode op); 
bool readsSecondSourceRegister(const DecodedInstruction& instruction); 

}  // namespace risc201
