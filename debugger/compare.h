#pragma once
// compare.h -- run the same program on the 4-stage and the 6-stage pipeline and compare.
//
//   interactive :  compare <file.hex> [diagram]
//   batch       :  ./sim --compare a.hex b.hex ... [--csv out.csv] [--diagram]
//                        [--max-cycles N] [--clk4 <ns> --clk6 <ns>]
//
// Every run starts from a fresh CPU, so the interactive state is never disturbed. Besides the
// performance counters, the final architectural state (registers, flags, data memory) of the
// two pipelines is compared: a faster pipeline must still compute exactly the same answer.

#include "timeline.h"

#include <cstdlib>
#include <cstring>
#include <chrono>

namespace debugger
{
    struct RunResult
    {
        string   name;
        bool     loaded = false;
        string   error;                 // load error
        uint64_t cycles = 0, retired = 0, stalls = 0, flushes = 0, forwards = 0;
        bool     halted = false, faulted = false, timedOut = false;
        double hostUs = 0;
        risc201::BranchPredictor::Stats branchStats;
        string   aluTiming;             // measured host time; excluded from deterministic cycle CSV
        string   faultIdentity;         // reason and faulting PC; excludes cycle/stage context
        string   fault;                 // fault name when faulted
        int32_t  reg[16] = {};
        bool     isEqual = false, isGreater = false;
        vector<uint8_t> dataMem;

        double cpi() const { return retired ? static_cast<double>(cycles) / retired : 0.0; }
        string status() const
        {
            if (!loaded) return "LOAD ERROR";
            if (faulted) return "FAULT " + fault;
            if (timedOut) return "TIMEOUT";
            return halted ? "HALT" : "running";
        }
    };

    inline string baseName(const string& path)
    {
        size_t slash = path.find_last_of("/\\");
        string f = slash == string::npos ? path : path.substr(slash + 1);
        size_t dot = f.find_last_of('.');
        return dot == string::npos ? f : f.substr(0, dot);
    }

    // Runs one program to HALT / fault / cycle limit. 'cpu' is left in its final state so the
    // caller can print its diagram.
    inline RunResult runProgram(CPU& cpu, const string& path, PipelineMode m, uint64_t maxCycles, bool record,
                                ControlUnit cu = ControlUnit::HORIZONTAL, bool prediction = false)
    {
        RunResult r;
        r.name = baseName(path);
        cpu.recordTimeline = record;
        cpu.controlUnit = cu;
        cpu.branchPredictor.enabled = prediction;
        cpu.setMode(m);
        if (!cpu.loadHex(path, r.error)) return r;
        r.loaded = true;

        const auto hostStart = std::chrono::steady_clock::now();
        while (!cpu.halted && !cpu.faulted && cpu.totalCycles < maxCycles)
        {
            try { cpu.tick(); }
            catch (const SimException& e) {
                r.fault = faultName(e.type());
                const string report = e.report();
                r.faultIdentity = report.substr(0, report.find("\n  stage"));
                break;
            }
        }
        r.hostUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - hostStart).count();
        r.branchStats = cpu.branchPredictor.stats;
        r.cycles = cpu.totalCycles;   r.retired = cpu.instructionsRetired;
        r.stalls = cpu.stallCycles;   r.flushes = cpu.branchFlushes;  r.forwards = cpu.forwards;
        r.halted = cpu.halted;        r.faulted = cpu.faulted;
        r.timedOut = !cpu.halted && !cpu.faulted;
        for (int i = 0; i < 16; i++) r.reg[i] = cpu.reg[i];
        r.isEqual = cpu.flags.IsEqual; r.isGreater = cpu.flags.IsGreaterThan;
        r.dataMem = cpu.dataMem;
        ostringstream timing; cpu.alu.printTiming(timing); r.aluTiming = timing.str();
        return r;
    }

    inline bool sameArchState(const RunResult& a, const RunResult& b)
    {
        if (!a.loaded || !b.loaded || a.timedOut || b.timedOut) return false;
        if (a.faulted != b.faulted || a.halted != b.halted || a.fault != b.fault || a.faultIdentity != b.faultIdentity) return false;
        if (a.retired != b.retired) return false;
        if (memcmp(a.reg, b.reg, sizeof a.reg) != 0) return false;
        if (a.isEqual != b.isEqual || a.isGreater != b.isGreater) return false;
        return a.dataMem == b.dataMem;
    }

    struct CompareRow { RunResult p4, p6; bool sameState = false; };

    struct CompareOptions
    {
        uint64_t maxCycles = 5000000;
        bool diagram = false;
        double clk4 = 0, clk6 = 0;      // optional clock periods in ns; 0 = not given
        string csvPath;
        size_t diagramInstr = 24;
        risc201::ALUConfig aluConfig;
        ControlUnit control = ControlUnit::HORIZONTAL;   // --vertical selects the encoded control store
    };

    inline CompareRow compareProgram(const string& path, const CompareOptions& opt)
    {
        CompareRow row;
        CPU* cpu = new CPU();                       // heap: the CPU object is large
        cpu->alu.config = opt.aluConfig;
        row.p4 = runProgram(*cpu, path, PipelineMode::STAGE_4, opt.maxCycles, opt.diagram, opt.control);
        if (opt.diagram && row.p4.loaded) { cout << "\n===== " << row.p4.name << " =====\n"; printGantt(*cpu, opt.diagramInstr); }
        row.p6 = runProgram(*cpu, path, PipelineMode::STAGE_6, opt.maxCycles, opt.diagram, opt.control);
        if (opt.diagram && row.p6.loaded) { cout << "\n"; printGantt(*cpu, opt.diagramInstr); cout << "\n"; }
        delete cpu;
        row.sameState = row.p4.loaded && row.p6.loaded && sameArchState(row.p4, row.p6);
        return row;
    }

    inline void printCompareTable(const vector<CompareRow>& rows, const CompareOptions& opt)
    {
        const bool timed = opt.clk4 > 0 && opt.clk6 > 0;
        cout << "\n=== 4-stage vs 6-stage comparison ===\n";
        cout << left << setw(16) << "Program" << right << setw(8) << "Instr"
             << " |" << setw(8) << "Cyc-4" << setw(7) << "CPI-4" << setw(7) << "Stall" << setw(7) << "Flush" << setw(6) << "Fwd"
             << " |" << setw(8) << "Cyc-6" << setw(7) << "CPI-6" << setw(7) << "Stall" << setw(7) << "Flush" << setw(6) << "Fwd"
             << " | " << setw(8) << "6 vs 4";
        if (timed) cout << setw(11) << "Time-4(ns)" << setw(11) << "Time-6(ns)" << setw(9) << "Speedup";
        cout << "  " << "Same result / status\n";
        cout << string(timed ? 160 : 129, '-') << "\n";

        double sum4 = 0, sum6 = 0; size_t ok = 0;
        for (const auto& r : rows)
        {
            const RunResult& a = r.p4; const RunResult& b = r.p6;
            if (!a.loaded || !b.loaded) { cout << left << setw(16) << (a.name.empty() ? "?" : a.name) << "  " << (a.loaded ? b.error : a.error) << "\n"; continue; }

            cout << left << setw(16) << a.name.substr(0, 15) << right << setw(8) << a.retired
                 << " |" << setw(8) << a.cycles << setw(7) << fixed << setprecision(2) << a.cpi() << setw(7) << a.stalls << setw(7) << a.flushes << setw(6) << a.forwards
                 << " |" << setw(8) << b.cycles << setw(7) << b.cpi() << setw(7) << b.stalls << setw(7) << b.flushes << setw(6) << b.forwards;

            ostringstream delta;
            if (a.cycles) delta << showpos << fixed << setprecision(1) << (100.0 * (static_cast<double>(b.cycles) - a.cycles) / a.cycles) << "%";
            cout << " | " << setw(8) << delta.str();

            if (timed)
            {
                const double t4 = a.cycles * opt.clk4, t6 = b.cycles * opt.clk6;
                cout << setw(11) << setprecision(1) << t4 << setw(11) << t6 << setw(8) << setprecision(2) << (t6 ? t4 / t6 : 0.0) << "x";
            }
            string note = r.sameState ? "yes" : "NO";
            if (a.retired != b.retired) note += " (retired counts differ!)";
            if (a.status() != "HALT" || b.status() != "HALT") note += "  [" + a.status() + " / " + b.status() + "]";
            cout << "  " << note << "\n";

            if (a.status() == "HALT" && b.status() == "HALT") { sum4 += a.cycles; sum6 += b.cycles; ok++; }
        }
        cout << string(timed ? 160 : 129, '-') << "\n";
        if (ok)
        {
            cout << "Total over " << ok << " program(s): 4-stage " << static_cast<uint64_t>(sum4) << " cycles, 6-stage "
                 << static_cast<uint64_t>(sum6) << " cycles  (6-stage uses " << showpos << setprecision(1)
                 << (100.0 * (sum6 - sum4) / sum4) << noshowpos << "% cycles).\n";
            if (timed)
                cout << "With clock periods " << opt.clk4 << " ns (4-stage) and " << opt.clk6 << " ns (6-stage): "
                     << "total time " << sum4 * opt.clk4 << " ns vs " << sum6 * opt.clk6 << " ns.\n";
            else
                cout << "Note: cycle counts ignore clock speed. A 6-stage pipe usually has a shorter clock period;\n"
                        "      pass --clk4 <ns> --clk6 <ns> (or 'compare <f> <clk4> <clk6>') to compare real time.\n";
        }
        cout.unsetf(ios::floatfield); cout << noshowpos;
        risc201::ALU selected; selected.config = opt.aluConfig; selected.printAlgorithms(cout);
        for (const auto& r : rows) {
            cout << "\n" << r.p4.name << " [4-stage]" << r.p4.aluTiming;
            cout << "\n" << r.p6.name << " [6-stage]" << r.p6.aluTiming;
        }
    }

    inline bool writeCompareCsv(const vector<CompareRow>& rows, const string& path)
    {
        ofstream out(path);
        if (!out) return false;
        out << "program,pipeline,instructions,cycles,cpi,stall_cycles,branch_flushes,forwards,status,same_result_as_other_pipeline\n";
        for (const auto& r : rows)
            for (const RunResult* p : {&r.p4, &r.p6})
            {
                if (!p->loaded) continue;
                out << p->name << "," << (p == &r.p4 ? "4-stage" : "6-stage") << "," << p->retired << "," << p->cycles << ","
                    << fixed << setprecision(4) << p->cpi() << "," << p->stalls << "," << p->flushes << "," << p->forwards << ","
                    << p->status() << "," << (r.sameState ? "yes" : "no") << "\n";
            }
        return true;
    }

    // ./sim --compare a.hex b.hex ... [options]. Returns the process exit code.
    inline int runBatch(int argc, char** argv)
    {
        CompareOptions opt;
        vector<string> files;
        bool compareMode = false;
        for (int i = 1; i < argc; i++)
        {
            const string a = argv[i];
            auto need = [&](const char* what) -> const char* {
                if (i + 1 >= argc) { cerr << "missing value for " << what << "\n"; exit(2); }
                return argv[++i];
            };
            try
            {
                if (a == "--compare") compareMode = true;
                else if (a == "--diagram") opt.diagram = true;
                else if (a == "--vertical") opt.control = ControlUnit::VERTICAL;
                else if (a == "--horizontal") opt.control = ControlUnit::HORIZONTAL;
                else if (a == "--alu-add" || a == "--alu-mul" || a == "--alu-div" || a == "--alu-fmul" || a == "--alu-fdiv") {
                    risc201::ALU selected; selected.config = opt.aluConfig;
                    if (!selected.selectAlgorithm(a.substr(6), need(a.c_str()))) {
                        cerr << "invalid algorithm for " << a << "\n"; return 2;
                    }
                    opt.aluConfig = selected.config;
                }
                else if (a == "--csv") opt.csvPath = need("--csv");
                else if (a == "--max-cycles") opt.maxCycles = stoull(need("--max-cycles"));
                else if (a == "--clk4") opt.clk4 = stod(need("--clk4"));
                else if (a == "--clk6") opt.clk6 = stod(need("--clk6"));
                else if (a == "--diagram-instr") opt.diagramInstr = stoul(need("--diagram-instr"));
                else if (a.rfind("--", 0) == 0) { cerr << "unknown option " << a << "\n"; return 2; }
                else files.push_back(a);
            }
            catch (...) { cerr << "bad value for " << a << "\n"; return 2; }
        }
        if (!compareMode || files.empty())
        {
            cerr << "Usage: sim --compare <a.hex> [b.hex ...] [--diagram] [--diagram-instr N] [--csv out.csv]\n"
                    "                [--max-cycles N] [--clk4 <ns> --clk6 <ns>] [--vertical]\n"
                    "                [--alu-add lookahead|ripple] [--alu-mul booth|iterative]\n"
                    "                [--alu-div restoring-fast|non-restoring] [--alu-fmul iterative|booth]\n"
                    "                [--alu-fdiv simple-fast|newton]\n"
                    "       sim                      (no arguments: interactive debugger)\n";
            return 2;
        }

        vector<CompareRow> rows;
        bool allOk = true;
        for (const auto& f : files)
        {
            rows.push_back(compareProgram(f, opt));
            const auto& r = rows.back();
            if (!r.sameState || r.p4.status() != "HALT" || r.p6.status() != "HALT") allOk = false;
        }
        printCompareTable(rows, opt);
        if (!opt.csvPath.empty())
        {
            if (writeCompareCsv(rows, opt.csvPath)) cout << "CSV written to " << opt.csvPath << "\n";
            else { cerr << "could not write " << opt.csvPath << "\n"; return 2; }
        }
        return allOk ? 0 : 1;
    }
}
