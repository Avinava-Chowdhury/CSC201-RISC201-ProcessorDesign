#pragma once
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

// The different ways the simulated hardware can fault.
enum class FaultType {
    GENERIC,
    INVALID_OPCODE,
    OUT_OF_BOUNDS,
    MISALIGNED,
    STACK_OVERFLOW,
    STACK_UNDERFLOW,
    STACK_CORRUPTION,
    // --- New Faults ---
    DIV_BY_ZERO,
    ARITHMETIC_OVERFLOW,
    PC_OUT_OF_BOUNDS,
    MISALIGNED_PC,
    ILLEGAL_INSTRUCTION,
    INVALID_MICROPC,
    ILLEGAL_MICROINSTRUCTION
};

inline const char* faultName(FaultType type) {
    switch (type) {
        case FaultType::GENERIC: return "GENERIC";
        case FaultType::INVALID_OPCODE: return "INVALID_OPCODE";
        case FaultType::OUT_OF_BOUNDS: return "OUT_OF_BOUNDS";
        case FaultType::MISALIGNED: return "MISALIGNED";
        case FaultType::STACK_OVERFLOW: return "STACK_OVERFLOW";
        case FaultType::STACK_UNDERFLOW: return "STACK_UNDERFLOW";
        case FaultType::STACK_CORRUPTION: return "STACK_CORRUPTION";
        case FaultType::DIV_BY_ZERO: return "DIV_BY_ZERO";
        case FaultType::ARITHMETIC_OVERFLOW: return "ARITHMETIC_OVERFLOW";
        case FaultType::PC_OUT_OF_BOUNDS: return "PC_OUT_OF_BOUNDS";
        case FaultType::MISALIGNED_PC: return "MISALIGNED_PC";
        case FaultType::ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
        case FaultType::INVALID_MICROPC: return "INVALID_MICROPC";
        case FaultType::ILLEGAL_MICROINSTRUCTION: return "ILLEGAL_MICROINSTRUCTION";
        default: return "UNKNOWN_FAULT";
    }
}

// 32-bit value as 0x0000abcd, used in fault messages
inline std::string hex32(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

// SimException acts as our hardware fault trigger.
class SimException : public std::runtime_error { // inherits from runtime error
private:
    FaultType faultType;
    uint32_t pc = 0;
    uint32_t badAddress = 0;
    bool hasAddress = false;

    // New context storage fields
    const char* stage_ = "UNKNOWN";
    uint64_t cycle_ = 0;
    uint8_t upc_ = 0;
    const char* mode_ = "UNKNOWN";

    // ADDITION: Safe string and conditional context fields from 1st program
    std::string stageName;
    std::string modeName;
    uint64_t cycle = 0;
    uint8_t upc = 0;
    bool hasContext = false;

public:
    explicit SimException(const std::string& message)
        : std::runtime_error(message), faultType(FaultType::GENERIC), pc(0) {}

    SimException(FaultType type, const std::string& message, uint32_t faultPc = 0)
        : std::runtime_error(message), faultType(type), pc(faultPc) {}

    // attach the bad address, used like: throw SimException(...).withAddress(a);
    SimException& withAddress(uint32_t address) {
        badAddress = address;
        hasAddress = true;
        return *this;
    }

    FaultType type() const { return faultType; }

    // Store execution context details
    void setContext(const char* stage, uint64_t cycle, uint8_t upc, const char* mode) {
        stage_ = stage ? stage : "UNKNOWN";
        cycle_ = cycle;
        upc_ = upc;
        mode_ = mode ? mode : "UNKNOWN";

        // ADDITION: Populate 1st program's context fields safely
        stageName = stage ? stage : "";
        this->cycle = cycle;
        this->upc = upc;
        modeName = mode ? mode : "";
        hasContext = true;
    }

    // multi-line description for the CLI
    std::string report() const {
        std::ostringstream out;
        out << "*** HARDWARE FAULT: " << faultName(faultType) << " ***\n"
            << "    reason  : " << what() << "\n"
            << "    at PC   : " << std::hex << "0x" << pc << std::dec;

        // ADDITION: Multi-line hex32 context block from 1st program
        if (hasContext) {
            out << "\n  stage   : " << stageName << " (" << modeName << ")"
                << "\n  cycle   : " << cycle
                << "\n  microPC : " << hex32(upc);
        }
            
        if (hasAddress) {
            out << "\n    address : " << std::hex << "0x" << badAddress << std::dec;
        }
        
        // Print the newly added context fields
        out << "\n    context : Cycle " << cycle_ 
            << ", Stage " << stage_ 
            << ", Mode " << mode_ 
            << ", MicroPC " << static_cast<int>(upc_);
            
        return out.str();
    }
};
