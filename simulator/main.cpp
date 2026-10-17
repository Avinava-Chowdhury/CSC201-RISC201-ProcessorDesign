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