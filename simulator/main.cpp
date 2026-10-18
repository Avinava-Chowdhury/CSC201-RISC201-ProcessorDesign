#include "cpu.h"
#include "sim_exception.h"
#include "isa.h"
#include "../debugger/debugger.h"

#include <iostream>
#include <set>
#include <sstream>
#include <string>

using namespace std;
using namespace risc201;
using namespace debugger;

int main()
{
    CPU cpu;
    cpu.reset();
    set<uint32_t> breakpoints;
    bool utraceEnabled = false;

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
                string err;
                if (cpu.loadHex(filename, err)) cout << "Successfully loaded " << filename << "\n";
                else cout << "Error: " << err << "\n";
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
                uint64_t retired = cpu.runUntilRetire(steps);
                cout << "Stepped until " << retired << " instruction(s) retired.\n";
                printPipeline(cpu);
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
                cout << "Switched to " << m << "-stage pipeline. CPU reset and breakpoints cleared.\n";
            }
            else cout << "Usage: mode 4|6\n";
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
            printMicroRom(cpu);
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
            printStack(cpu, count > 0 ? count : 16);
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
