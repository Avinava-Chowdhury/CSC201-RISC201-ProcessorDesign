#include "pass1.h"
#include "pass2.h"
#include "tokenizer.h"
#include "isa.h"
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace std;
using namespace risc201;

namespace
{
    bool isLabelLine(const vector<string>& tokens)
    {
        return tokens.size() == 1 && tokens[0].back() == ':';
    }

    vector<string> readLines(const string& path)
    {
        vector<string> lines;
        ifstream file(path);
        string line;
        while (getline(file, line)) lines.push_back(line);
        return lines;
    }

    void printAddressListing(const vector<string>& lines)
    {
        uint32_t address = 0;
        cout << "\nPass 1 walk-through:\n";
        for (const string& line : lines)
        {
            vector<string> tokens = tokenizeLine(line);
            if (tokens.empty()) continue;
            cout << "  0x" << hex << setw(4) << setfill('0') << address << dec
                 << "  " << line;
            if (isLabelLine(tokens)) cout << "   <-- label, address not advanced";
            else address += BYTES_PER_WORD;
            cout << "\n";
        }
    }

    void printSymbolTable(const map<string, uint32_t>& symbols)
    {
        cout << "\nSymbol table:\n";
        for (const auto& [name, address] : symbols)
        {
            cout << "  " << left << setw(14) << setfill(' ') << name << right
                 << " -> 0x" << hex << setw(4) << setfill('0') << address << dec << "\n";
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        cout << "Usage: " << argv[0] << " <input.s>\n";
        return 1;
    }

    vector<string> rawLines = readLines(argv[1]);
    if (rawLines.empty())
    {
        cerr << "Error: could not read " << argv[1] << "\n";
        return 1;
    }

    try
    {
        vector<string> expanded = expandMacroLines(rawLines);
        printAddressListing(expanded);
        printSymbolTable(buildSymbolTable(expanded));
    }
    catch (const exception& e)
    {
        cerr << "Pass 1 Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}