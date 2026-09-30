#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

std::vector<std::string> splitLabelLines(const std::vector<std::string>& rawLines);

std::map<std::string, uint32_t> buildSymbolTable(const std::vector<std::string>& rawLines);
