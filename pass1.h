#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct SymbolTableResult
{
    std::map<std::string, uint32_t> symbols;
    std::vector<std::string> errors;
};

SymbolTableResult buildSymbolTable(const std::vector<std::string>& lines);