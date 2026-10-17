#include "cpu.h"
#include "sim_exception.h"
#include "isa.h"

#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>

using namespace std;
using namespace risc201;

namespace
{
    bool parseAddress(const string& token, uint32_t& valueOut)
    {
        try
        {
            size_t consumed = 0;
            unsigned long value = stoul(token, &consumed, 0);
            if (consumed != token.size()) return false;
            valueOut = static_cast<uint32_t>(value);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    string formatOperand(const DecodedInstruction& instr)
    {
        if (!instr.usesConstantNumber) return getRegisterName(instr.sourceRegister2);
        ostringstream out;
        switch (instr.constantModifier)
        {
        case ImmediateModifier::UNSIGNED:
            out << "0x" << hex << instr.rawConstantValue << "u";
            break;
        case ImmediateModifier::UPPER:
            out << "0x" << hex << instr.rawConstantValue << "h";
            break;
        default:
            out << instr.calculateFinalConstantValue();
        }
        return out.str();
    }

    string formatInstruction(uint32_t word, uint32_t address)
    {
        optional<DecodedInstruction> decoded = convertFromBinary(word);
        if (!decoded) return ".word " + hex32(word) + "  (invalid instruction)";

        const OperationDetails* details = findByOpcodeId(decoded->operation);
        if (!details) return ".word " + hex32(word) + "  (unknown opcode)";

        const string mnemonic = details->nameInAssembly;
        const string dest = getRegisterName(decoded->destinationRegister);
        const string src1 = getRegisterName(decoded->sourceRegister1);

        switch (details->assemblyFormat)
        {
        case AssemblySyntax::NO_ARGS:
            return mnemonic;
        case AssemblySyntax::JUMP_TO_LABEL: {
            int64_t target = static_cast<int64_t>(address) + decoded->branchJumpDistance;
            return mnemonic + " " + hex32(static_cast<uint32_t>(target));
        }
        case AssemblySyntax::THREE_ARG_MATH:
            return mnemonic + " " + dest + ", " + src1 + ", " + formatOperand(*decoded);
        case AssemblySyntax::COMPARE_ONLY:
            return mnemonic + " " + src1 + ", " + formatOperand(*decoded);
        case AssemblySyntax::SINGLE_ARG:
            return mnemonic + " " + dest + ", " +
                   (decoded->usesConstantNumber ? formatOperand(*decoded) : src1);
        case AssemblySyntax::LOAD_MEMORY:
        case AssemblySyntax::STORE_MEMORY:
            return mnemonic + " " + dest + ", " + formatOperand(*decoded) + "[" + src1 + "]";
        }
        return mnemonic;
    }

    void printRegs(const CPU& cpu)
    {
        cout << "--- Processor State ---\n";
        cout << "PC: " << hex32(cpu.pc) << "\n";
        for (int i = 0; i < 16; i++)
        {
            cout << "r" << left << setw(2) << i << ": "
                 << hex32(static_cast<uint32_t>(cpu.reg[i])) << "\t";
            if ((i + 1) % 4 == 0) cout << "\n";
        }
        cout << "Flags -> E: " << cpu.flags.IsEqual << " | GT: " << cpu.flags.IsGreaterThan << "\n";
        if (cpu.faulted) cout << "Status: HALTED (fault)\n";
    }

    void printNextInstruction(const CPU& cpu)
    {
        if (cpu.faulted)
        {
            cout << "CPU is halted by a fault.\n";
            return;
        }
        uint32_t index = cpu.pc / BYTES_PER_WORD;
        if (index >= cpu.instrMem.size())
        {
            cout << hex32(cpu.pc) << " is outside instruction memory.\n";
            return;
        }
        cout << hex32(cpu.pc) << ":  " << formatInstruction(cpu.instrMem[index], cpu.pc) << "\n";
    }

    void printMemory(const CPU& cpu, uint32_t startAddr, int wordCount)
    {
        for (int i = 0; i < wordCount; i++)
        {
            uint32_t addr = startAddr + static_cast<uint32_t>(i) * BYTES_PER_WORD;
            try
            {
                uint32_t value = cpu.readWord(addr, cpu.pc);
                cout << "  " << hex32(addr) << ": " << hex32(value) << "\n";
            }
            catch (const SimException& e)
            {
                cout << "  " << hex32(addr) << ": <" << faultName(e.type()) << ">\n";
                break;
            }
        }
    }

    void printStack(const CPU& cpu, int maxWords)
    {
        uint32_t sp = static_cast<uint32_t>(cpu.reg[STACK_POINTER_REG_INDEX]);
        cout << "--- Stack (sp = " << hex32(sp) << ", base = " << hex32(STACK_STARTING_ADDRESS) << ") ---\n";
        if (sp >= STACK_STARTING_ADDRESS)
        {
            cout << "  (empty)\n";
            return;
        }
        uint32_t addr = STACK_STARTING_ADDRESS - BYTES_PER_WORD;
        int shown = 0;
        while (addr >= sp && shown < maxWords)
        {
            try
            {
                uint32_t value = cpu.readWord(addr, cpu.pc);
                cout << "  " << hex32(addr) << " | " << hex32(value) << " |"
                     << (addr == sp ? "  <- sp\n" : "\n");
            }
            catch (const SimException&)
            {
                cout << "  " << hex32(addr) << " | <unreadable> |\n";
            }
            if (addr < BYTES_PER_WORD) break;
            addr -= BYTES_PER_WORD;
            shown++;
        }
        if (shown >= maxWords) cout << "  ... (" << maxWords << " word limit, use 'stack <n>' to see more)\n";
    }

    void printHelp()
    {
        cout << "Commands:\n"
             << "  load <file.hex>     - Load program into memory\n"
             << "  reset               - Reset CPU state and clear breakpoints\n"
             << "  step [n]            - Execute n instructions (default 1)\n"
             << "  continue            - Run until a breakpoint or fault\n"
             << "  break <addr>        - Set a breakpoint at an address (hex or decimal)\n"
             << "  break list          - List all breakpoints\n"
             << "  delete <addr>       - Remove a breakpoint\n"
             << "  next                - Show the instruction about to execute\n"
             << "  regs                - Show all 16 registers, PC, and flags\n"
             << "  mem <addr> [n]      - Dump n data words starting at addr (default 8)\n"
             << "  stack [n]           - Show up to n words of the stack (default 16)\n"
             << "  help                - Show this message\n"
             << "  quit                - Exit the debugger\n";
    }

    constexpr int MAX_CONTINUE_STEPS = 200000;

    void doContinue(CPU& cpu, const set<uint32_t>& breakpoints)
    {
        if (cpu.faulted)
        {
            cout << "CPU is halted by a fault. Type 'reset' to start over.\n";
            return;
        }
        int stepsRun = 0;
        bool hitBreakpoint = false;
        bool first = true;

        while (stepsRun < MAX_CONTINUE_STEPS)
        {
            if (!first && breakpoints.count(cpu.pc))
            {
                hitBreakpoint = true;
                break;
            }
            first = false;

            try
            {
                cpu.stepInstruction();
            }
            catch (const SimException& e)
            {
                cout << "\n" << e.report() << "\n";
                cout << "CPU halted. Type 'reset' to start over.\n";
                return;
            }
            stepsRun++;
        }

        if (hitBreakpoint)
        {
            cout << "Breakpoint hit at " << hex32(cpu.pc) << " after " << stepsRun << " instruction(s).\n";
        }
        else
        {
            cout << "Stopped after the " << MAX_CONTINUE_STEPS
                 << "-instruction safety limit (no breakpoint or fault hit).\n";
        }
    }
}

int main()
{
    CPU cpu;
    cpu.reset();
    set<uint32_t> breakpoints;

    cout << "RISC201 Interactive Debugger\n";
    cout << "Type 'help' for commands or 'quit' to exit.\n";

    string line;
    while (true)
    {
        cout << "\n(debug) > ";
        if (!getline(cin, line)) break;

        istringstream ss(line);
        string command;
        ss >> command;
        if (command.empty()) continue;

        if (command == "quit")
        {
            break;
        }
        else if (command == "load")
        {
            string filename;
            if (ss >> filename)
            {
                if (cpu.loadHex(filename)) cout << "Successfully loaded " << filename << "\n";
                else cout << "Error: Could not open file " << filename << "\n";
            }
            else
            {
                cout << "Usage: load <file.hex>\n";
            }
        }
        else if (command == "reset")
        {
            cpu.reset();
            breakpoints.clear();
            cout << "CPU reset to initial state. Breakpoints cleared.\n";
        }
        else if (command == "step")
        {
            if (cpu.faulted)
            {
                cout << "CPU is halted by a fault. Type 'reset' to start over.\n";
                continue;
            }
            int steps = 1;
            string arg;
            if (ss >> arg)
            {
                try { steps = stoi(arg); }
                catch (...) { cout << "Usage: step [n]\n"; continue; }
            }

            try
            {
                for (int i = 0; i < steps; i++)
                {
                    if (!breakpoints.empty() && i > 0 && breakpoints.count(cpu.pc))
                    {
                        cout << "Breakpoint hit at " << hex32(cpu.pc) << " before completing "
                             << steps << " steps.\n";
                        steps = i;
                        break;
                    }
                    cpu.stepInstruction();
                }
                cout << "Stepped " << steps << " instruction(s). PC is now " << hex32(cpu.pc) << "\n";
                printStack(cpu, 16);
            }
            catch (const SimException& e)
            {
                cout << "\n" << e.report() << "\n";
                cout << "CPU halted. Type 'reset' to start over.\n";
            }
        }
        else if (command == "continue")
        {
            doContinue(cpu, breakpoints);
        }
        else if (command == "break")
        {
            string arg;
            if (ss >> arg)
            {
                if (arg == "list")
                {
                    if (breakpoints.empty())
                    {
                        cout << "No breakpoints set.\n";
                    }
                    else
                    {
                        cout << "Breakpoints:\n";
                        for (uint32_t addr : breakpoints) cout << "  " << hex32(addr) << "\n";
                    }
                }
                else
                {
                    uint32_t addr;
                    if (parseAddress(arg, addr))
                    {
                        breakpoints.insert(addr);
                        cout << "Breakpoint set at " << hex32(addr) << "\n";
                    }
                    else
                    {
                        cout << "Usage: break <addr> | break list\n";
                    }
                }
            }
            else
            {
                cout << "Usage: break <addr> | break list\n";
            }
        }
        else if (command == "delete")
        {
            string arg;
            uint32_t addr;
            if (ss >> arg && parseAddress(arg, addr))
            {
                if (breakpoints.erase(addr)) cout << "Removed breakpoint at " << hex32(addr) << "\n";
                else cout << "No breakpoint at " << hex32(addr) << "\n";
            }
            else
            {
                cout << "Usage: delete <addr>\n";
            }
        }
        else if (command == "next")
        {
            printNextInstruction(cpu);
        }
        else if (command == "regs")
        {
            printRegs(cpu);
        }
        else if (command == "mem")
        {
            string arg;
            uint32_t addr;
            if (ss >> arg && parseAddress(arg, addr))
            {
                int count = 8;
                string countArg;
                if (ss >> countArg)
                {
                    try { count = stoi(countArg); }
                    catch (...) { count = 8; }
                }
                printMemory(cpu, addr, count);
            }
            else
            {
                cout << "Usage: mem <addr> [count]\n";
            }
        }
        else if (command == "stack")
        {
            int count = 16;
            string arg;
            if (ss >> arg)
            {
                try { count = stoi(arg); }
                catch (...) { count = 16; }
            }
            printStack(cpu, count);
        }
        else if (command == "help")
        {
            printHelp();
        }
        else
        {
            cout << "Unknown command: " << command << " (type 'help')\n";
        }
    }

    return 0;
}
