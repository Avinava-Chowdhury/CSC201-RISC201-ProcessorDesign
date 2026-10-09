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
    STACK_CORRUPTION
};

inline const char* faultName(FaultType type) {
    switch (type) {
        case FaultType::INVALID_OPCODE:   return "INVALID_OPCODE";
        case FaultType::OUT_OF_BOUNDS:    return "OUT_OF_BOUNDS";
        case FaultType::MISALIGNED:       return "MISALIGNED";
        case FaultType::STACK_OVERFLOW:   return "STACK_OVERFLOW";
        case FaultType::STACK_UNDERFLOW:  return "STACK_UNDERFLOW";
        case FaultType::STACK_CORRUPTION: return "STACK_CORRUPTION";
        default:                          return "GENERIC";
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
public:
    explicit SimException(const std::string& message)
        : std::runtime_error(message), faultType(FaultType::GENERIC) {}

    SimException(FaultType type, const std::string& message, uint32_t faultPc = 0)
        : std::runtime_error(message), faultType(type), pc(faultPc) {}

    // attach the bad address, used like: throw SimException(...).withAddress(a);
    SimException& withAddress(uint32_t address) {
        badAddress = address;
        hasAddress = true;
        return *this;
    }

    FaultType type() const { return faultType; }

    // multi-line description for the CLI
    std::string report() const {
        std::ostringstream out;
        out << "*** HARDWARE FAULT: " << faultName(faultType) << " ***\n"
            << "  reason  : " << what() << "\n"
            << "  at PC   : " << hex32(pc);
        if (hasAddress) {
            out << "\n  address : " << hex32(badAddress);
        }
        return out.str();
    }

private:
    FaultType faultType;
    uint32_t pc = 0;
    uint32_t badAddress = 0;
    bool hasAddress = false;
};
