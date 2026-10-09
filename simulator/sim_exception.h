#pragma once
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

// SimException acts as our hardware fault trigger
class SimException : public std::runtime_error { // inherits from runtime error
public:
    explicit SimException(const std::string& message)
        : std::runtime_error(message), faultType(FaultType::GENERIC) {}

    SimException(FaultType type, const std::string& message)
        : std::runtime_error(message), faultType(type) {}

    FaultType type() const { return faultType; }

private:
    FaultType faultType;
};
