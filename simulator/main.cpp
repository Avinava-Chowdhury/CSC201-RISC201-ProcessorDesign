#include "cpu.h"
#include "sim_exception.h"
#include "isa.h"
#include "../debugger/debugger.h"
#include "../debugger/timeline.h"
#include "../debugger/compare.h"

#include <iostream>
#include <fstream>
#include <set>
#include <map>
#include <vector>
#include <sstream>
#include <string>

using namespace std;
using namespace risc201;
using namespace debugger;

int main(int argc, char** argv)
{
    if (argc > 1 && string(argv[1]) != "--script") return runBatch(argc, argv);

    CPU cpu;
    cpu.reset();
    set<uint32_t> breakpoints;
    map<string, uint32_t> symbolMap;
    map<uint32_t, uint32_t> watchpoints;
    bool utraceEnabled = false;

    istream* inStream = &cin;
    ifstream scriptFile;
    bool batchMode = false;

    if (argc > 2 && string(argv[1]) == "--script")
    {
        scriptFile.open(argv[2]);
        if (!scriptFile.is_open())
        {
            cout << "Error: Could not open script file " << argv[2] << "\n";
            return 1;
        }
        inStream = &scriptFile;
        batchMode = true;
    }

    if (!batchMode)
    {
        cout << "RISC201 Interactive Debugger\n";
        cout << "Type 'help' for commands or 'quit' to exit.\n";
    }

    // Lambda to evaluate watchpoints after every tick
    auto checkWatchpoints = [&]() -> bool {
        bool triggered = false;
        for (auto& wp : watchpoints) {
            try {
                uint32_t currentVal = cpu.readWord(wp.first, cpu.pc);
                if (currentVal != wp.second) {
                    cout << "Watchpoint triggered at address " << hex32(wp.first) 
                         << "! Value changed from " << hex32(wp.second) 
                         << " to " << hex32(currentVal) << ".\n";
                    wp.second = currentVal;
                    triggered = true;
                }
            } catch (...) {}
        }
        return triggered;
    };

    string line;
    while (true)
    {
        if (!batchMode) cout << "\n(debug) > ";
        if (!getline(*inStream, line)) break;

        if (batchMode) cout << "\n(debug) > " << line << "\n";

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
                if (filename.length() > 2 && filename.substr(filename.length() - 2) == ".s")
                {
                    cout << "Auto-assembling " << filename << " in the background...\n";
                    string hexfile = filename.substr(0, filename.length() - 2) + ".hex";
                    
                    string cmd = "make " + hexfile;
                    if (system(cmd.c_str()) != 0) 
                    {
                        cmd = "mingw32-make " + hexfile;
                        system(cmd.c_str());
                    }
                    filename = hexfile;
                }

                string err;
                if (cpu.loadHex(filename, err)) {
                    cout << "Successfully loaded " << filename << "\n";
                    
                    // Feature: Symbol Loading
                    symbolMap.clear();
                    string symFilename = filename.substr(0, filename.find_last_of('.')) + ".sym";
                    ifstream symFile(symFilename);
                    if (symFile.is_open()) {
                        string symLine;
                        int symCount = 0;
                        while (getline(symFile, symLine)) {
                            size_t colon = symLine.find(':');
                            if (colon != string::npos) {
                                string label = symLine.substr(0, colon);
                                string addrStr = symLine.substr(colon + 1);
                                addrStr.erase(0, addrStr.find_first_not_of(" \t"));
                                uint32_t addr;
                                if (parseAddress(addrStr, addr)) {
                                    symbolMap[label] = addr;
                                    symCount++;
                                }
                            }
                        }
                        if (symCount > 0) cout << "Loaded " << symCount << " symbols from " << symFilename << "\n";
                    }
                }
                else cout << "Error: " << err << "\n";
            }
            else cout << "Usage: load <file.hex | file.s>\n";
        }
        else if (command == "disasm")
        {
            string arg;
            uint32_t addr;
            if (ss >> arg && parseAddress(arg, addr))
            {
                uint32_t index = addr / 4;
                if (index < cpu.instrMem.size()) cout << hex32(addr) << ":  " << formatInstruction(cpu.instrMem[index], addr) << "\n";
                else cout << "Error: Address " << hex32(addr) << " is outside instruction memory bounds.\n";
            }
            else cout << "Usage: disasm <addr>\n";
        }
        else if (command == "setreg")
        {
            string regArg, valArg;
            uint32_t val;
            if (ss >> regArg >> valArg && parseAddress(valArg, val))
            {
                int rIdx = -1;
                if (regArg.size() > 1 && regArg[0] == 'r') {
                    try { rIdx = stoi(regArg.substr(1)); } catch (...) {}
                } else if (regArg == "sp") rIdx = 14;
                else if (regArg == "ra") rIdx = 15;
                else if (regArg == "pc") {
                    cpu.pc = val;
                    cout << "pc set to " << hex32(val) << "\n";
                    continue;
                }

                else if (regArg == "upc") 
                {
                     cpu.curUPC = static_cast<uint8_t>(val);
                     cout << "upc set to " << val << "\n";
                     continue;
                }
                
                if (rIdx >= 0 && rIdx < 16) {
                    cpu.reg[rIdx] = val;
                    cout << "r" << rIdx << " set to " << hex32(val) << "\n";
                } else {
                    cout << "Invalid register. Use r0-r15, sp, ra, or pc.\n";
                }
            }
            else cout << "Usage: setreg <reg> <value>\n";
        }
        else if (command == "setmem")
        {
            string addrArg, valArg;
            uint32_t addr, val;
            if (ss >> addrArg >> valArg && parseAddress(addrArg, addr) && parseAddress(valArg, val))
            {
                try {
                    cpu.writeWord(addr, val, cpu.pc);
                    cout << "Memory at " << hex32(addr) << " set to " << hex32(val) << "\n";
                } catch (const SimException& e) {
                    cout << "Error: " << e.what() << "\n";
                }
            }
            else cout << "Usage: setmem <addr> <value>\n";
        }
        else if (command == "watch")
        {
            // Feature: Watchpoints
            string arg;
            uint32_t addr;
            if (ss >> arg && parseAddress(arg, addr)) {
                uint32_t initialVal = 0;
                try { initialVal = cpu.readWord(addr, cpu.pc); } catch (...) {}
                watchpoints[addr] = initialVal;
                cout << "Watchpoint set at " << hex32(addr) << " (current value: " << hex32(initialVal) << ")\n";
            } else {
                cout << "Usage: watch <addr>\n";
            }
        }
        else if (command == "reset")
        {
            cpu.reset();
            breakpoints.clear();
            watchpoints.clear();
            cout << "CPU reset to initial state. Breakpoints and watchpoints cleared.\n";
        }
        else if (command == "step")
        {
            if (cpu.faulted || cpu.halted)
            {
                cout << "CPU is halted. Type 'reset' to start over.\n";
                continue;
            }
            int steps = 1;
            string arg;
            if (ss >> arg)
            {
                try { steps = stoi(arg); }
                catch (...) { cout << "Usage: step [n]\n"; continue; }
            }
            if (steps < 1)
            {
                cout << "Error: Number of instructions must be >= 1.\n";
                continue;
            }

            try
            {
                uint64_t startRetired = cpu.instructionsRetired;
                uint64_t targetRetired = startRetired + steps;
                
                while (cpu.instructionsRetired < targetRetired && !cpu.halted && !cpu.faulted) {
                    cpu.tick();
                    if (checkWatchpoints()) {
                        cout << "Execution paused: watchpoint modified.\n";
                        break;
                    }
                }
                
                uint64_t actuallyRetired = cpu.instructionsRetired - startRetired;
                cout << "Stepped until " << actuallyRetired << " instruction(s) retired.\n";
                
                // Feature: Retired Instruction Output
                if (actuallyRetired > 0) {
                    uint32_t retiredPc = (cpu.mode == PipelineMode::STAGE_4) ? cpu.ex_memwb.pc : cpu.mem_wb_6.pc;
                    uint32_t index = retiredPc / 4;
                    if (index < cpu.instrMem.size()) {
                        cout << "Retired: " << hex32(retiredPc) << "  " << formatInstruction(cpu.instrMem[index], retiredPc) << "\n";
                    }
                }

                printPipeline(cpu);
                
                // Auto-show stack during step
                cpu.printStackMemory(std::cout, 8);
            }
            catch (const SimException& e)
            {
                cout << "\n" << e.report() << "\n";
                cout << "CPU halted. Type 'reset' to start over.\n";
            }
        }
        else if (command == "tick")
        {
            if (cpu.faulted || cpu.halted)
            {
                cout << "CPU is halted. Type 'reset' to start over.\n";
                continue;
            }
            int cycles = 1;
            string arg;
            if (ss >> arg)
            {
                try { cycles = stoi(arg); }
                catch (...) { cout << "Usage: tick [n]\n"; continue; }
            }
            if (cycles < 1)
            {
                cout << "Error: Number of cycles must be >= 1.\n";
                continue;
            }

            try
            {
                for (int i = 0; i < cycles; i++)
                {
                    if (!breakpoints.empty() && breakpoints.count(cpu.nextExecPC()))
                    {
                        cout << "Breakpoint hit at " << hex32(cpu.nextExecPC()) << ".\n";
                        break;
                    }
                    cpu.tick();
                    if (utraceEnabled) printMicro(cpu);
                    if (checkWatchpoints()) {
                        cout << "Execution paused: watchpoint modified.\n";
                        break;
                    }
                    if (cpu.halted || cpu.faulted) break;
                }
                printPipeline(cpu);
            }
            catch (const SimException& e)
            {
                cout << "\n" << e.report() << "\n";
                cout << "CPU halted. Type 'reset' to start over.\n";
            }
        }
        else if (command == "mode")
        {
            int m;
            if (ss >> m && (m == 4 || m == 6))
            {
                cpu.setMode(m == 4 ? PipelineMode::STAGE_4 : PipelineMode::STAGE_6);
                breakpoints.clear();
                watchpoints.clear();
                cout << "Switched to " << m << "-stage pipeline. CPU reset and breakpoints cleared.\n";
            }
            else cout << "Usage: mode 4|6\n";
        }
        else if (command == "continue")
        {
            if (cpu.faulted || cpu.halted) {
                cout << "CPU is halted. Type 'reset' to start over.\n";
                continue;
            }
            
            int cyclesRun = 0;
            bool hitBreakpoint = false;
            bool first = true;
            
            while (cyclesRun < 200000 && !cpu.halted && !cpu.faulted) {
                if (!first && breakpoints.count(cpu.nextExecPC())) {
                    hitBreakpoint = true;
                    break;
                }
                first = false;
                
                try {
                    cpu.tick();
                } catch (const SimException& e) {
                    cout << "\n" << e.report() << "\n";
                    cout << "CPU halted. Type 'reset' to start over.\n";
                    break;
                }
                
                cyclesRun++;
                
                if (checkWatchpoints()) {
                    cout << "Execution paused: watchpoint modified after " << cyclesRun << " cycles.\n";
                    break;
                }
            }
            
            if (hitBreakpoint) {
                cout << "Breakpoint hit at " << hex32(cpu.nextExecPC()) << " after " << cyclesRun << " cycle(s).\n";
            } else if (!cpu.halted && !cpu.faulted && cyclesRun == 200000) {
                cout << "Stopped after 200000-cycle safety limit.\n";
            }
        }
        else if (command == "break")
        {
            string arg;
            if (ss >> arg)
            {
                if (arg == "list")
                {
                    if (breakpoints.empty()) cout << "No breakpoints set.\n";
                    else {
                        cout << "Breakpoints:\n";
                        for (uint32_t addr : breakpoints) cout << "  " << hex32(addr) << "\n";
                    }
                }
                else
                {
                    uint32_t addr;
                    // Feature: Break by Label
                    if (symbolMap.count(arg)) {
                        addr = symbolMap[arg];
                        breakpoints.insert(addr);
                        cout << "Breakpoint set at " << arg << " (" << hex32(addr) << ")\n";
                    }
                    else if (parseAddress(arg, addr))
                    {
                        breakpoints.insert(addr);
                        cout << "Breakpoint set at " << hex32(addr) << "\n";
                    }
                    else cout << "Usage: break <addr|label> | break list\n";
                }
            }
            else cout << "Usage: break <addr|label> | break list\n";
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
            else cout << "Usage: delete <addr>\n";
        }
        else if (command == "pipe")
        {
            printPipeline(cpu);
        }
        else if (command == "micro")
        {
            printMicro(cpu);
        }
        else if (command == "ucontrol")
        {
            string arg;
            ss >> arg;
            if (arg.empty() || arg == "horizontal" || arg == "h" || arg == "vertical" || arg == "v" || arg == "compare" || arg == "c")
                printMicroRom(cpu, arg.empty() ? "horizontal" : arg);
            else cout << "Usage: ucontrol [vertical|compare]\n";
        }
        else if (command == "cu")
        {
            string arg;
            if (ss >> arg && (arg == "h" || arg == "horizontal" || arg == "v" || arg == "vertical"))
            {
                cpu.setControlUnit(arg[0] == 'v' ? ControlUnit::VERTICAL : ControlUnit::HORIZONTAL);
                breakpoints.clear();
                cout << "Switched to " << (arg[0] == 'v' ? "VERTICAL" : "HORIZONTAL")
                     << " control unit. CPU reset and breakpoints cleared.\n";
            }
            else cout << "Usage: cu h|v   (currently " << (cpu.controlUnit == ControlUnit::VERTICAL ? "vertical" : "horizontal") << ")\n";
        }
        else if (command == "utrace")
        {
            string arg;
            if (ss >> arg)
            {
                if (arg == "on") { utraceEnabled = true; cout << "Microtrace ON.\n"; }
                else if (arg == "off") { utraceEnabled = false; cout << "Microtrace OFF.\n"; }
                else cout << "Usage: utrace on|off\n";
            }
            else cout << "Usage: utrace on|off\n";
        }
        else if (command == "stats")
        {
            printStats(cpu);
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
            else cout << "Usage: mem <addr> [count]\n";
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
            // Use built-in CPU function instead of debugger printStack
            cpu.printStackMemory(std::cout, count > 0 ? count : 16);
        }
        else if (command == "diagram")
        {
            size_t maxInstr = 40, perBlock = 16;
            string a1;
            bool exportToFile = false;
            string filename;
            vector<string> args;
            
            while (ss >> a1) args.push_back(a1);
            
            for (size_t i = 0; i < args.size(); ++i) {
                if (args[i] == "export" && i + 1 < args.size()) {
                    exportToFile = true;
                    filename = args[i + 1];
                    i++;
                } else if (i == 0) {
                    try { maxInstr = stoul(args[i]); } catch (...) {}
                } else if (i == 1) {
                    try { perBlock = stoul(args[i]); } catch (...) {}
                }
            }

            if (maxInstr < 1) maxInstr = 1;
            if (perBlock < 4) perBlock = 4;

            // Feature: Pipeline Diagram Export
            if (exportToFile) {
                ofstream outFile(filename);
                if (outFile.is_open()) {
                    printGantt(cpu, maxInstr, perBlock, outFile);
                    cout << "Pipeline diagram exported to " << filename << "\n";
                } else {
                    cout << "Error: Could not open " << filename << " for writing.\n";
                }
            } else {
                printGantt(cpu, maxInstr, perBlock);
            }
        }
        else if (command == "cycles")
        {
            size_t rows = 60;
            string a1;
            try { if (ss >> a1) rows = stoul(a1); }
            catch (...) { cout << "Usage: cycles [max_rows]\n"; continue; }
            printCycleTable(cpu, rows);
        }
        else if (command == "timeline")
        {
            string sub, path;
            if (ss >> sub >> path && sub == "csv")
            {
                string err;
                if (writeTimelineCsv(cpu, path, err)) cout << "Wrote " << cpu.timeline.size() << " cycles to " << path << "\n";
                else cout << "Error: " << err << "\n";
            }
            else cout << "Usage: timeline csv <file.csv>\n";
        }
        else if (command == "compare")
        {
            string file, a1;
            if (!(ss >> file)) { cout << "Usage: compare <file.hex> [diagram] [clk4_ns clk6_ns]\n"; continue; }
            CompareOptions opt;
            double c4 = 0, c6 = 0;
            vector<string> rest;
            while (ss >> a1) rest.push_back(a1);
            for (size_t i = 0; i < rest.size(); i++)
            {
                if (rest[i] == "diagram") opt.diagram = true;
                else { try { double v = stod(rest[i]); (c4 == 0 ? c4 : c6) = v; } catch (...) {} }
            }
            if (c4 > 0 && c6 > 0) { opt.clk4 = c4; opt.clk6 = c6; }
            vector<CompareRow> rows;
            rows.push_back(compareProgram(file, opt));
            printCompareTable(rows, opt);
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
