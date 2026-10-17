#pragma once

#include "cpu.h"
#include "sim_exception.h"
#include "isa.h"

#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>

namespace debugger
{
    inline bool parseAddress(const std::string& token, uint32_t& valueOut)
    {
        try
        {
            size_t consumed = 0;
            unsigned long value = std::stoul(token, &consumed, 0);
            if (consumed != token.size()) return false;
            valueOut = static_cast<uint32_t>(value);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    inline std::string formatOperand(const risc201::DecodedInstruction& instr)
    {
        if (!instr.usesConstantNumber) return risc201::getRegisterName(instr.sourceRegister2);
        std::ostringstream out;
        switch (instr.constantModifier)
        {
        case risc201::ImmediateModifier::UNSIGNED:
            out << "0x" << std::hex << instr.rawConstantValue << "u";
            break;
        case risc201::ImmediateModifier::UPPER:
            out << "0x" << std::hex << instr.rawConstantValue << "h";
            break;
        default:
            out << instr.calculateFinalConstantValue();
        }
        return out.str();
    }

    inline std::string formatInstruction(uint32_t word, uint32_t address)
    {
        using namespace risc201;

        std::optional<DecodedInstruction> decoded = convertFromBinary(word);
        if (!decoded) return ".word " + hex32(word) + "  (invalid instruction)";

        const OperationDetails* details = findByOpcodeId(decoded->operation);
        if (!details) return ".word " + hex32(word) + "  (unknown opcode)";

        const std::string mnemonic = details->nameInAssembly;
        const std::string dest = getRegisterName(decoded->destinationRegister);
        const std::string src1 = getRegisterName(decoded->sourceRegister1);

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

    inline void printRegs(const CPU& cpu)
    {
        using namespace std;
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

    inline void printNextInstruction(const CPU& cpu)
    {
        using namespace std;
        if (cpu.faulted)
        {
            cout << "CPU is halted by a fault.\n";
            return;
        }
        uint32_t index = cpu.pc / risc201::BYTES_PER_WORD;
        if (index >= cpu.instrMem.size())
        {
            cout << hex32(cpu.pc) << " is outside instruction memory.\n";
            return;
        }
        cout << hex32(cpu.pc) << ":  " << formatInstruction(cpu.instrMem[index], cpu.pc) << "\n";
    }

    inline void printMemory(const CPU& cpu, uint32_t startAddr, int wordCount)
    {
        using namespace std;
        for (int i = 0; i < wordCount; i++)
        {
            uint32_t addr = startAddr + static_cast<uint32_t>(i) * risc201::BYTES_PER_WORD;
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

    inline void printStack(const CPU& cpu, int maxWords)
    {
        using namespace std;
        using namespace risc201;

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

    inline void printHelp()
    {
        using namespace std;
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

    inline void doContinue(CPU& cpu, const std::set<uint32_t>& breakpoints)
    {
        using namespace std;
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