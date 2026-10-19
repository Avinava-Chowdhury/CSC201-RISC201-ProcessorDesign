#pragma once
// timeline.h -- cycle-by-stage pipeline diagrams built from CPU::timeline.
//
//   printCycleTable : one line per clock cycle, one column per pipeline stage
//   printGantt      : one line per instruction, one column per clock cycle (classic textbook chart)
//   writeTimelineCsv: the same data as a CSV file for a spreadsheet / report
//
// Legend used everywhere:    *  = instruction is held in that stage (load-use stall)
//                            x  = instruction was squashed (fetched on the wrong path of a
//                                 taken branch, or younger than a HALT)

#include "debugger.h"

#include <fstream>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

namespace debugger
{
    // ---- stage names / layout for each pipeline ---------------------------------------
    inline const char* stageLabel(PipelineMode m, int slot)
    {
        static const char* s6[6] = {"IF", "ID", "RR", "EX", "MEM", "WB"};
        static const char* s4[6] = {"IF", "ID", "--", "EX", "MW", "--"};   // MW = combined MEM/WB
        return m == PipelineMode::STAGE_4 ? s4[slot] : s6[slot];
    }

    inline vector<int> stageSlots(PipelineMode m)
    {
        return m == PipelineMode::STAGE_4 ? vector<int>{0, 1, 3, 4} : vector<int>{0, 1, 2, 3, 4, 5};
    }

    inline string instrText(const CPU& cpu, uint32_t pc)
    {
        const uint32_t idx = pc / BYTES_PER_WORD;
        if (idx < cpu.programWords && pc % BYTES_PER_WORD == 0) return formatInstruction(cpu.instrMem[idx], pc);
        return "(outside program)";
    }

    // ---- 1. cycle x stage table ------------------------------------------------------------
    inline void printCycleTable(const CPU& cpu, size_t maxRows = 60)
    {
        if (cpu.timeline.empty()) { cout << "No cycles recorded yet. Run some cycles first.\n"; return; }

        const auto slots = stageSlots(cpu.mode);
        cout << "--- Cycle-by-stage table (" << (cpu.mode == PipelineMode::STAGE_6 ? "6" : "4")
             << "-stage) ---\n";
        cout << left << setw(7) << "Cycle";
        for (int s : slots) cout << setw(11) << stageLabel(cpu.mode, s);
        cout << "\n";

        size_t shown = 0;
        for (const auto& row : cpu.timeline)
        {
            if (shown++ >= maxRows) { cout << "... (" << cpu.timeline.size() - maxRows << " more cycles, use a larger limit or 'timeline csv')\n"; break; }
            cout << left << setw(7) << row.cycle;
            for (int s : slots)
            {
                const StageCell& c = row.cell[s];
                string txt = "-";
                if (c.valid)
                {
                    ostringstream o;
                    o << (c.tag == 'x' ? "x" : "") << hex32(c.pc).substr(6) << (c.tag == 'S' ? "*" : "");
                    txt = o.str();
                }
                cout << setw(11) << txt;
            }
            cout << "\n";
        }
        cout << "(cells show the low 16 bits of the instruction address;  * = held by stall, x = squashed)\n";
    }

    // ---- 2. instruction x cycle chart ------------------------------------------------------------
    struct Instance
    {
        int seq = 0;                                  // order of fetch, starting at 1
        uint32_t pc = 0;
        map<uint64_t, pair<int, char>> cells;         // cycle -> (stage slot, tag)
    };

    // The pipeline is in-order, so an instruction in stage s at cycle t is either the SAME
    // instruction held there from cycle t-1 (stall) or the one that was in the previous
    // stage at t-1. That lets us follow every dynamic instruction, even inside loops where
    // the same address is executed many times.
    inline vector<Instance> buildInstances(const CPU& cpu)
    {
        vector<Instance> inst;
        const auto order = stageSlots(cpu.mode);
        int prevOcc[6]; char prevTag[6];
        for (int i = 0; i < 6; i++) { prevOcc[i] = -1; prevTag[i] = ' '; }

        for (const auto& row : cpu.timeline)
        {
            int occ[6]; char tg[6];
            for (int i = 0; i < 6; i++) { occ[i] = -1; tg[i] = ' '; }

            for (size_t k = 0; k < order.size(); k++)
            {
                const int s = order[k];
                const StageCell& c = row.cell[s];
                if (!c.valid) continue;

                int id = -1;
                if (prevOcc[s] >= 0 && prevTag[s] == 'S') id = prevOcc[s];         // held in place
                else if (k > 0)
                {
                    const int ps = order[k - 1];
                    if (prevOcc[ps] >= 0 && prevTag[ps] != 'x' && prevTag[ps] != 'S') id = prevOcc[ps];  // moved forward
                }
                if (id < 0)
                {
                    Instance n; n.pc = c.pc; n.seq = static_cast<int>(inst.size()) + 1;
                    inst.push_back(n);
                    id = static_cast<int>(inst.size()) - 1;
                }
                inst[id].cells[row.cycle] = {s, c.tag};
                occ[s] = id; tg[s] = c.tag;
            }
            for (int i = 0; i < 6; i++) { prevOcc[i] = occ[i]; prevTag[i] = tg[i]; }
        }
        return inst;
    }

    inline string gridCell(PipelineMode m, int slot, char tag)
    {
        string label = stageLabel(m, slot);
        if (tag == 'x') return "x" + label;
        if (tag == 'S') return label + "*";
        return label;
    }

    // Feature: Pipeline Diagram Export Output Parameter
    inline void printGantt(const CPU& cpu, size_t maxInstr = 40, size_t cyclesPerBlock = 16, std::ostream& out = std::cout)
    {
        if (cpu.timeline.empty()) { out << "No cycles recorded yet. Run some cycles first.\n"; return; }

        const vector<Instance> all = buildInstances(cpu);
        const size_t n = min(all.size(), maxInstr);
        const uint64_t lastCycle = cpu.timeline.back().cycle;

        out << "--- Pipeline diagram (" << (cpu.mode == PipelineMode::STAGE_6 ? "6" : "4")
             << "-stage): " << n << " of " << all.size() << " instructions, "
             << lastCycle << " cycles ---\n";
        out << "    * = held (load-use stall)   x = squashed (wrong path / after HALT)"
             << (cpu.mode == PipelineMode::STAGE_4 ? "   MW = memory + write-back" : "") << "\n";

        // Only show blocks that contain at least one cell of the shown instructions
        uint64_t lastShown = 0;
        for (size_t i = 0; i < n; i++) if (!all[i].cells.empty()) lastShown = max(lastShown, all[i].cells.rbegin()->first);

        for (uint64_t start = 1; start <= lastShown; start += cyclesPerBlock)
        {
            const uint64_t end = min<uint64_t>(start + cyclesPerBlock - 1, lastShown);
            out << "\n" << left << setw(34) << " #  addr    instruction";
            for (uint64_t c = start; c <= end; c++) out << setw(5) << c;
            out << "\n" << string(34 + 5 * (end - start + 1), '-') << "\n";

            for (size_t i = 0; i < n; i++)
            {
                const Instance& in = all[i];
                if (in.cells.empty() || in.cells.rbegin()->first < start || in.cells.begin()->first > end) continue;

                string text = instrText(cpu, in.pc);
                if (text.size() > 20) text = text.substr(0, 20);
                ostringstream label;
                label << right << setw(3) << in.seq << " " << hex32(in.pc).substr(6) << "  " << left << setw(20) << text;
                out << left << setw(34) << label.str();
                for (uint64_t c = start; c <= end; c++)
                {
                    auto it = in.cells.find(c);
                    out << setw(5) << (it == in.cells.end() ? "" : gridCell(cpu.mode, it->second.first, it->second.second));
                }
                out << "\n";
            }
        }
        if (all.size() > n) out << "\n(" << all.size() - n << " more instructions not shown; pass a larger limit)\n";
        if (cpu.timeline.size() >= CPU::TIMELINE_CAP) out << "(timeline recording stopped after " << CPU::TIMELINE_CAP << " cycles)\n";
    }

    // ---- 3. CSV export --------------------------------------------------------------------------
    inline bool writeTimelineCsv(const CPU& cpu, const string& path, string& err)
    {
        ofstream out(path);
        if (!out) { err = "could not write " + path; return false; }
        const auto slots = stageSlots(cpu.mode);
        out << "cycle";
        for (int s : slots) out << "," << stageLabel(cpu.mode, s);
        out << "\n";
        for (const auto& row : cpu.timeline)
        {
            out << row.cycle;
            for (int s : slots)
            {
                const StageCell& c = row.cell[s];
                out << ",";
                if (c.valid) out << (c.tag == 'x' ? "x" : "") << hex32(c.pc) << (c.tag == 'S' ? "*" : "");
            }
            out << "\n";
        }
        return true;
    }
}
