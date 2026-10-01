#pragma once
#include <stdexcept>
#include <string>

// SimException acts as our hardware fault trigger.
class SimException : public std::runtime_error { // inherits from runtime error
public:
    explicit SimException(const std::string& message) 
        : std::runtime_error(message) {}
};