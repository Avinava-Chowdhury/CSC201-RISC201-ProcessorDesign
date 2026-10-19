#pragma once

#include "cpu.h"
#include "sim_exception.h"
#include "isa.h"
#include "horizontal_microcode.h" // access describe()

#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>

using namespace std;
using namespace risc201;

namespace debugger
{
    inline bool parseAddress(const string& token, uint32_t& valueOut)
    {
        // Reject leading negative signs (#30)
        if (!token.empty() && token[0] == '-') return false;
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

    inline string formatOperand(const DecodedInstruction& instr)
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

    inline string formatInstruction(uint32_t word, uint32_t address)
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

    inline void printRegs(const CPU& cpu)
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
        else if (cpu.halted) cout << "Status: HALTED (clean)\n";
    }

    inline void printPipeline(const CPU& cpu)
    {
        if (cpu.faulted)
        {
            cout << "CPU is halted by a fault.\n";
            return;
        }

        auto disasm = [&](uint32_t stagePc) {
            uint32_t index = stagePc / BYTES_PER_WORD;
            if (index < cpu.instrMem.size()) return formatInstruction(cpu.instrMem[index], stagePc);
            return string("(out of bounds)");
        };

        cout << "--- Pipeline State ---\n";
        if (cpu.mode == PipelineMode::STAGE_4) {
            cout << "IF    : pc=" << hex32(cpu.pc) << "  " << disasm(cpu.pc) << "\n";
            cout << "ID    : " << (cpu.if_id.active ? "pc=" + hex32(cpu.if_id.pc) + "  " + disasm(cpu.if_id.pc) : "(bubble)") << "\n";
            cout << "EX    : " << (cpu.id_ex.active ? "pc=" + hex32(cpu.id_ex.pc) + "  " + disasm(cpu.id_ex.pc) : "(bubble)") << "\n";
            cout << "MEMWB : " << (cpu.ex_memwb.active ? "pc=" + hex32(cpu.ex_memwb.pc) + "  " + disasm(cpu.ex_memwb.pc) : "(bubble)") << "\n";
        } else {
            cout << "IF    : pc=" << hex32(cpu.pc) << "  " << disasm(cpu.pc) << "\n";
            cout << "ID    : " << (cpu.if_id.active ? "pc=" + hex32(cpu.if_id.pc) + "  " + disasm(cpu.if_id.pc) : "(bubble)") << "\n";
            cout << "RR    : " << (cpu.id_rr.active ? "pc=" + hex32(cpu.id_rr.pc) + "  " + disasm(cpu.id_rr.pc) : "(bubble)") << "\n";
            cout << "EX    : " << (cpu.rr_ex.active ? "pc=" + hex32(cpu.rr_ex.pc) + "  " + disasm(cpu.rr_ex.pc) : "(bubble)") << "\n";
            cout << "MEM   : " << (cpu.ex_mem.active ? "pc=" + hex32(cpu.ex_mem.pc) + "  " + disasm(cpu.ex_mem.pc) : "(bubble)") << "\n";
            cout << "WB    : " << (cpu.mem_wb_6.active ? "pc=" + hex32(cpu.mem_wb_6.pc) + "  " + disasm(cpu.mem_wb_6.pc) : "(bubble)") << "\n";
        }
    }

    inline void printMicro(const CPU& cpu)
    {
        cout << "cycle " << cpu.totalCycles << "  [" 
             << (cpu.mode == PipelineMode::STAGE_6 ? "6-stage" : "4-stage") << "]\n";
        
        auto printStage = [&](const string& name, int stageIdx) {
            auto t = cpu.microTrace[stageIdx];
            if (t.active) {
                cout << left << setw(5) << name 
                     << " uPC=" << hex32(t.uPC) 
                     << "  pc=" << hex32(t.pc) << "  ";
                if (name == "IF") cout << "FETCH\n";
                else cout << describe(cpu.controlStore[t.uPC]) << "\n";
            }
        };

        if (cpu.mode == PipelineMode::STAGE_4) {
            printStage("IF", static_cast<int>(MicroStage::IF));
            printStage("ID", static_cast<int>(MicroStage::ID));
            printStage("EX", static_cast<int>(MicroStage::EX));
            printStage("MEMWB", static_cast<int>(MicroStage::MEMWB));
        } else {
            printStage("IF", static_cast<int>(MicroStage::IF));
            printStage("ID", static_cast<int>(MicroStage::ID));
            printStage("RR", static_cast<int>(MicroStage::RR));
            printStage("EX", static_cast<int>(MicroStage::EX));
            printStage("MEM", static_cast<int>(MicroStage::MEM));
            printStage("WB", static_cast<int>(MicroStage::WB));
        }
    }

    inline void printMicroRom(const CPU& cpu)
    {
        cout << "--- Control Store ROM ---\n";
        for (size_t i = 0; i < CONTROL_STORE_SIZE; ++i) {
            if (cpu.controlStore[i].valid) {
                cout << "uPC=" << hex32(static_cast<uint32_t>(i)) << " : " 
                     << describe(cpu.controlStore[i]) << "\n";
            }
        }
    }

    inline void printStats(const CPU& cpu)
    {
        double cpi = cpu.instructionsRetired > 0 ? 
                     static_cast<double>(cpu.totalCycles) / cpu.instructionsRetired : 0.0;
        cout << "--- Pipeline Statistics ---\n"
             << "Cycles           : " << cpu.totalCycles << "\n"
             << "Retired          : " << cpu.instructionsRetired << "\n"
             << "CPI              : " << fixed << setprecision(2) << cpi << "\n"
             << "Stall cycles     : " << cpu.stallCycles << "\n"
             << "Forwards         : " << cpu.forwards << "\n"
             << "Branch flushes   : " << cpu.branchFlushes << "\n";
    }

    inline void printMemory(const CPU& cpu, uint32_t startAddr, int wordCount)
    {
        if (wordCount < 0) return;
        if (wordCount > 1024) wordCount = 1024; // Clamp memory count
        
        for (int i = 0; i < wordCount; i++)
        {
            uint32_t addr = startAddr + static_cast<uint32_t>(i) * BYTES_PER_WORD;
            
            // Check for address wrap around bounds (#30)
            if (i > 0 && addr <= startAddr) {
                cout << "  (address wrapped around)\n";
                break;
            }
            
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
        if (maxWords < 0) return;
        if (maxWords > 1024) maxWords = 1024; // Clamp stack count

        uint32_t sp = static_cast<uint32_t>(cpu.reg[STACK_POINTER_REG_INDEX]);
        cout << "========== RISC201 STACK ==========\n";
        cout << "Stack base : " << hex32(STACK_STARTING_ADDRESS) << "\n";
        cout << "Stack limit: " << hex32(STACK_LOWEST_LIMIT) << "\n";
        cout << "SP         : " << hex32(sp) << "\n";
        cout << "-----------------------------------\n";
        cout << "Address      | Contents   | Status\n";
        cout << "-----------------------------------\n";
        
        if (sp >= STACK_STARTING_ADDRESS)
        {
            cout << "  Stack is empty; no occupied entries.\n";
            cout << "-----------------------------------\n";
            cout << "  Next free slot: "
                 << hex32(STACK_STARTING_ADDRESS - BYTES_PER_WORD)
                 << " | -------- | FREE\n";
            cout << "===================================\n";
            return;
        }
        
        uint32_t addr = STACK_STARTING_ADDRESS - BYTES_PER_WORD;
        int shown = 0;
        
        while (addr >= sp && shown < maxWords)
        {
            try
            {
                uint32_t value = cpu.readWord(addr, cpu.pc);
                cout << "  " << hex32(addr) << " | " << hex32(value) << " | "
                     << (addr == sp ? "<- SP (TOP)" : "USED") << "\n";
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
        cout << "Commands:\n"
             << "  load <file.hex>     - Load program into memory\n"
             << "  reset               - Reset CPU state and clear breakpoints\n"
             << "  tick [n]            - Advance the clock by n cycles (default 1)\n"
             << "  continue            - Run until a breakpoint or fault\n"
             << "  break <addr>        - Set a breakpoint at an address (hex or decimal)\n"
             << "  break list          - List all breakpoints\n"
             << "  delete <addr>       - Remove a breakpoint\n"
             << "  pipe                - Show the instruction in each pipeline stage\n"
             << "  regs                - Show all 16 registers, PC, and flags\n"
             << "  mem <addr> [n]      - Dump n data words starting at addr (default 8)\n"
             << "  stack [n]           - Show up to n words of the stack (default 16)\n"
             << "  mode <4|6>          - Switch between 4-stage and 6-stage pipeline\n"
             << "  micro               - Show the micro-instruction active in each stage\n"
             << "  ucontrol            - Dump the entire horizontal control store ROM\n"
             << "  stats               - Show performance statistics\n"
             << "  diagram [n] [w]     - Pipeline diagram: instruction x cycle chart (n instructions, w cycles per block)\n"
             << "  cycles [rows]       - Cycle-by-stage table\n"
             << "  timeline csv <f>    - Export the recorded cycle-by-stage timeline as CSV\n"
             << "  compare <f> [diagram] [clk4 clk6]\n"
             << "                      - Run <f.hex> on BOTH pipelines and print the CPI table\n"
             << "  help                - Show this message\n"
             << "  quit                - Exit the debugger\n";
    }

    constexpr uint64_t MAX_CONTINUE_CYCLES = 200000;

    inline void doContinue(CPU& cpu, const set<uint32_t>& breakpoints)
    {
        if (cpu.faulted || cpu.halted)
        {
            cout << "CPU is halted (fault or HALT instruction). Type 'reset' to start over.\n";
            if (cpu.halted) printStats(cpu);
            return;
        }
        
        uint64_t cyclesRun = 0;
        bool hitBreakpoint = false;
        bool first = true;

        while (cyclesRun < MAX_CONTINUE_CYCLES && !cpu.halted && !cpu.faulted)
        {
            // Evaluate breakpoints using nextExecPC() (the instruction entering EX)
            if (!first && breakpoints.count(cpu.nextExecPC()))
            {
                hitBreakpoint = true;
                break;
            }
            first = false;

            try
            {
                cpu.tick();
            }
            catch (const SimException& e)
            {
                cout << "\n" << e.report() << "\n";
                cout << "CPU halted by a fault. Type 'reset' to start over.\n";
                return;
            }
            cyclesRun++;
        }

        if (hitBreakpoint)
        {
            cout << "Breakpoint hit at " << hex32(cpu.nextExecPC()) << " after " << cyclesRun << " cycle(s).\n";
        }
        else if (cpu.halted)
        {
            cout << "Program HALTED after " << cyclesRun << " cycle(s).\n";
            printStats(cpu);
        }
        else if (cpu.faulted)
        {
            // Already caught and returned in the try-catch block above
        }
        else
        {
            cout << "Stopped after the " << MAX_CONTINUE_CYCLES
                 << "-cycle safety limit (no breakpoint or fault hit).\n";
        }
    }
}
