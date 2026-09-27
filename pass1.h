#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

std::map<std::string, uint32_t> buildSymbolTable(const std::vector<std::string>& rawLines);