#pragma once
// Four independent cold-start runs. The old two-pipeline 'compare' stays available.
#include "compare.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace debugger {
struct BranchCompareOptions : CompareOptions { unsigned repeats = 7; };
struct BranchCompareRow {
    std::array<RunResult, 4> runs; // 4 OFF, 4 ON, 6 OFF, 6 ON
    std::array<bool, 4> matches{};
    bool allMatch = false;
};
inline bool branchComplete(const RunResult& r) {
    return r.loaded && r.halted && !r.faulted && !r.timedOut;
}
inline bool validBranchClocks(const BranchCompareOptions& opt) {
    return std::isfinite(opt.clk4) && std::isfinite(opt.clk6)
        && opt.clk4 >= 0 && opt.clk6 >= 0 && ((opt.clk4 > 0) == (opt.clk6 > 0));
}
inline BranchCompareRow compareBranches(const string& path, const BranchCompareOptions& opt) {
    BranchCompareRow row;
    std::array<std::vector<double>, 4> samples;
    auto cpu = std::make_unique<CPU>();
    cpu->alu.config = opt.aluConfig;
    // One untimed warmup per configuration reduces first-use host effects.
    // Each measured run still starts with a cold predictor and zeroed data memory.
    for (unsigned i = 0; i < 4; ++i)
        runProgram(*cpu, path, i < 2 ? PipelineMode::STAGE_4 : PipelineMode::STAGE_6,
                   opt.maxCycles, false, opt.control, i % 2 != 0);
    // Rotate ordering to reduce systematic host timing bias.
    for (unsigned rep = 0; rep < opt.repeats; ++rep) {
        for (unsigned j = 0; j < 4; ++j) {
            const unsigned i = (rep + j) % 4;
            auto r = runProgram(*cpu, path, i < 2 ? PipelineMode::STAGE_4 : PipelineMode::STAGE_6,
                                opt.maxCycles, false, opt.control, i % 2 != 0);
            samples[i].push_back(r.hostUs);
            if (rep == 0) row.runs[i] = r;
            else if (!sameArchState(row.runs[i], r) || row.runs[i].cycles != r.cycles)
                row.runs[i].error = "Repeated run mismatch or incomplete execution";
        }
    }
    for (unsigned i = 0; i < 4; ++i) {
        auto& t = samples[i]; std::sort(t.begin(), t.end());
        row.runs[i].hostUs = t.size() % 2 ? t[t.size()/2] : (t[t.size()/2-1] + t[t.size()/2])/2;
        row.matches[i] = branchComplete(row.runs[0]) && branchComplete(row.runs[i])
            && row.runs[i].error.empty() && sameArchState(row.runs[0], row.runs[i]);
        if (opt.diagram && row.runs[i].loaded) {
            cout << "\n=== " << row.runs[i].name << " " << (i < 2 ? "4-stage" : "6-stage")
                 << (i % 2 ? " BP ON" : " BP OFF") << " timeline (excluded from timing) ===\n";
            runProgram(*cpu, path, i < 2 ? PipelineMode::STAGE_4 : PipelineMode::STAGE_6,
                       opt.maxCycles, true, opt.control, i % 2 != 0);
            printGantt(*cpu, opt.diagramInstr);
        }
    }
    row.allMatch = std::all_of(row.matches.begin(), row.matches.end(), [](bool b) { return b; });
    return row;
}
inline void printBranchComparison(const BranchCompareRow& row, const BranchCompareOptions& opt) {
    const auto flags = cout.flags(); const auto precision = cout.precision();
    cout << "\n=== " << row.runs[0].name << ": original vs 2-bit dynamic prediction ===\n"
         << "Control: " << (opt.control == ControlUnit::VERTICAL ? "vertical" : "horizontal")
         << "; cold predictor per run; conditional BEQ/BGT only.\n"
         << "Host runtime: median of " << opt.repeats << " executions, after one warmup/configuration.\n";
    cout << left << setw(10) << "Pipeline" << setw(8) << "BP" << right
         << setw(10) << "Instr" << setw(10) << "Cycles" << setw(9) << "CPI" << setw(9) << "IPC"
         << setw(9) << "Stalls" << setw(9) << "Flushes" << setw(9) << "Forwards"
         << setw(12) << "Host(us)" << setw(12) << "Host MIPS" << "  Result/status\n";
    for (unsigned i = 0; i < 4; ++i) {
        const auto& r = row.runs[i];
        cout << left << setw(10) << (i < 2 ? "4-stage" : "6-stage") << setw(8) << (i % 2 ? "2-bit" : "OFF")
             << right << setw(10) << r.retired << setw(10) << r.cycles << fixed << setprecision(3)
             << setw(9) << r.cpi() << setw(9) << (r.cycles ? double(r.retired)/r.cycles : 0)
             << setw(9) << r.stalls << setw(9) << r.flushes << setw(9) << r.forwards
             << setw(12) << r.hostUs << setw(12) << (r.hostUs > 0 ? r.retired/r.hostUs : 0)
             << "  " << (row.matches[i] ? "MATCH " : "UNVERIFIED ") << r.status();
        if (!r.error.empty()) cout << " " << r.error;
        cout << '\n';
    }
    cout << "\nConditional-branch statistics (OFF = sequential/not-taken baseline):\n"
         << left << setw(10) << "Pipeline" << setw(8) << "BP" << right << setw(12) << "Resolved"
         << setw(10) << "Taken" << setw(10) << "Correct" << setw(10) << "Dir miss"
         << setw(12) << "Accuracy%" << setw(13) << "NextPC miss" << setw(12) << "Cond flush" << '\n';
    for (unsigned i = 0; i < 4; ++i) {
        const auto& b = row.runs[i].branchStats;
        cout << left << setw(10) << (i < 2 ? "4-stage" : "6-stage") << setw(8) << (i % 2 ? "2-bit" : "OFF")
             << right << setw(12) << b.conditional << setw(10) << b.taken << setw(10) << b.correct
             << setw(10) << b.conditional-b.correct;
        if (b.conditional) cout << setw(12) << setprecision(2) << 100.0*b.correct/b.conditional;
        else cout << setw(12) << "N/A";
        cout << setw(13) << b.nextPCMisses << setw(12) << b.conditionalFlushes << '\n';
    }
    cout << "\nSpeedup vs original within the SAME pipeline (same clock assumed):\n";
    for (unsigned i : {0u, 2u}) {
        const auto& a = row.runs[i]; const auto& b = row.runs[i+1];
        cout << (i == 0 ? "4-stage: " : "6-stage: ");
        if (!row.matches[i] || !row.matches[i+1] || !a.cycles || !b.cycles) { cout << "N/A (incomplete/mismatched run)\n"; continue; }
        cout << setprecision(3) << double(a.cycles)/b.cycles << "x cycle speedup; "
             << setprecision(2) << 100.0*(double(a.cycles)-b.cycles)/a.cycles << "% fewer cycles; "
             << setprecision(3) << (b.hostUs > 0 ? a.hostUs/b.hostUs : 0) << "x host runtime ratio.\n";
        const double clock = i == 0 ? opt.clk4 : opt.clk6;
        if (clock > 0) cout << "  Modeled time at supplied " << clock << " ns/clock: "
            << a.cycles*clock << " ns OFF, " << b.cycles*clock << " ns ON.\n";
    }
    cout << "Host time measures this C++ simulator, not hardware delay. ALU instrumentation is enabled equally.\n"
         << "No clock-frequency benefit is assumed; a real predictor's timing/area needs hardware analysis.\n"
         << "B/CALL/RET keep original redirection; total Flushes includes these, Cond flush does not.\n"
         << "Direction misses and next-PC misses can differ for a branch targeting PC+4.\n"
         << "All four completed architectural states (registers, flags, all 64 KiB data, retired count): "
         << (row.allMatch ? "MATCH" : "NOT VERIFIED / MISMATCH") << "\n";
    cout << "\nFinal register OUTPUT (hex):\n" << left << setw(9) << "Register";
    for (auto label : {"4 OFF", "4 ON", "6 OFF", "6 ON"}) cout << setw(14) << label;
    cout << '\n';
    for (unsigned reg = 0; reg < 16; ++reg) {
        cout << left << setw(9) << (string("r") + to_string(reg));
        for (const auto& r : row.runs) cout << setw(14) << hex32(static_cast<uint32_t>(r.reg[reg]));
        cout << '\n';
    }
    cout << left << setw(9) << "E / GT";
    for (const auto& r : row.runs) cout << setw(14) << (to_string(r.isEqual) + " / " + to_string(r.isGreater));
    cout << "\nData output: first 16 nonzero words per run (ALL bytes checked above):\n";
    for (unsigned i = 0; i < 4; ++i) {
        cout << (i < 2 ? "4-stage" : "6-stage") << (i%2 ? " ON : " : " OFF: ");
        unsigned shown = 0, nonzero = 0;
        const auto& mem = row.runs[i].dataMem;
        for (size_t a = 0; a+3 < mem.size(); a+=4) {
            uint32_t w = uint32_t(mem[a]) | uint32_t(mem[a+1])<<8 | uint32_t(mem[a+2])<<16 | uint32_t(mem[a+3])<<24;
            if (w) { ++nonzero; if (shown++ < 16) cout << '[' << hex32(static_cast<uint32_t>(a)) << "]=" << hex32(w) << ' '; }
        }
        if (!nonzero) cout << "all zero";
        if (nonzero > 16) cout << "... (" << nonzero << " nonzero words)";
        cout << '\n';
    }
    risc201::ALU selected; selected.config = opt.aluConfig; selected.printAlgorithms(cout);
    for (unsigned i = 0; i < 4; ++i) cout << "\nALU timing: " << (i < 2 ? "4-stage" : "6-stage")
        << (i%2 ? " ON" : " OFF") << " (first measured run)" << row.runs[i].aluTiming;
    cout.flags(flags); cout.precision(precision);
}
inline bool writeBranchCsv(const vector<BranchCompareRow>& rows, const string& path, const BranchCompareOptions& opt) {
    ofstream out(path); if (!out) return false;
    out << "program,pipeline,predictor,instructions,cycles,cpi,ipc,load_stall_cycles,total_branch_flushes,forwards,conditional_branches,taken,direction_correct,direction_misses,direction_accuracy_pct,next_pc_misses,conditional_flushes,median_host_us,host_mips,cycle_speedup,cycle_reduction_pct,host_ratio,clock_ns,modeled_time_ns,status,matches_4stage_original\n";
    for (const auto& row : rows) for (unsigned i = 0; i < 4; ++i) {
        const auto& r = row.runs[i]; const auto& b = r.branchStats; const auto& base = row.runs[i & ~1u];
        string escaped; for (char c : r.name) { escaped += c; if (c == '"') escaped += '"'; }
        out << '"' << escaped << "\"," << (i<2 ? 4 : 6) << ',' << (i%2 ? "2-bit" : "off") << ','
            << r.retired << ',' << r.cycles << ',' << fixed << setprecision(6) << r.cpi() << ','
            << (r.cycles ? double(r.retired)/r.cycles : 0) << ',' << r.stalls << ',' << r.flushes << ',' << r.forwards << ','
            << b.conditional << ',' << b.taken << ',' << b.correct << ',' << b.conditional-b.correct << ',';
        if (b.conditional) out << 100.0*b.correct/b.conditional;
        out << ',' << b.nextPCMisses << ',' << b.conditionalFlushes << ',' << r.hostUs << ','
            << (r.hostUs > 0 ? r.retired/r.hostUs : 0) << ',';
        if (row.matches[i] && row.matches[i & ~1u] && r.cycles && base.cycles) out << double(base.cycles)/r.cycles;
        out << ',';
        if (row.matches[i] && row.matches[i & ~1u] && base.cycles) out << 100.0*(double(base.cycles)-r.cycles)/base.cycles;
        out << ',';
        if (row.matches[i] && row.matches[i & ~1u] && r.hostUs > 0) out << base.hostUs/r.hostUs;
        const double clock = i<2 ? opt.clk4 : opt.clk6;
        out << ','; if (clock > 0) out << clock;
        out << ','; if (clock > 0) out << clock*r.cycles;
        out << ',' << r.status() << ',' << (row.matches[i] ? "yes" : "no") << '\n';
    }
    return bool(out);
}
inline unsigned parseBranchRepeats(const string& token) {
    size_t used = 0;
    if (token.empty() || token[0] == '-') throw invalid_argument("repeats must be 1..101");
    const auto n = stoul(token, &used);
    if (used != token.size() || n < 1 || n > 101) throw invalid_argument("repeats must be 1..101");
    return static_cast<unsigned>(n);
}
inline void branchUsage() {
    cout << "Interactive: bp on | bp off | bp stats | bp table\n"
         << "  on/off reset CPU, predictor, timing, breakpoints and watchpoints; keep loaded program/ALU selections.\n"
         << "  bp compare <file.hex> [diagram] [clk4_ns clk6_ns] [repeats N] [csv path]\n"
         << "Batch: sim --compare-bp <a.hex> [b.hex ...] [--repeats N] [--csv path]\n"
         << "  [--clk4 ns --clk6 ns] [--max-cycles N] [--diagram] [--diagram-instr N]\n"
         << "  [--vertical|--horizontal] [--alu-add ... --alu-mul ... --alu-div ... --alu-fmul ... --alu-fdiv ...]\n";
}
inline int runBranchBatch(int argc, char** argv) {
    BranchCompareOptions opt; vector<string> files;
    try {
        for (int i = 1; i < argc; ++i) {
            const string a = argv[i];
            auto need = [&]() -> string { if (++i >= argc) throw invalid_argument("missing value for " + a); return argv[i]; };
            auto positive = [&](const string& token) -> uint64_t {
                size_t used=0; if (token.empty() || token[0]=='-') throw invalid_argument("invalid positive integer");
                auto n=stoull(token,&used); if (!n || used!=token.size()) throw invalid_argument("invalid positive integer"); return n;
            };
            auto clock = [&](const string& token) { size_t used=0; double n=stod(token,&used);
                if (used!=token.size() || !isfinite(n) || n<=0) throw invalid_argument("clock period must be finite and positive"); return n; };
            if (a == "--compare-bp") {}
            else if (a == "--repeats") opt.repeats = parseBranchRepeats(need());
            else if (a == "--csv") opt.csvPath = need();
            else if (a == "--max-cycles") opt.maxCycles = positive(need());
            else if (a == "--diagram-instr") opt.diagramInstr = positive(need());
            else if (a == "--clk4") opt.clk4 = clock(need());
            else if (a == "--clk6") opt.clk6 = clock(need());
            else if (a == "--diagram") opt.diagram = true;
            else if (a == "--vertical") opt.control = ControlUnit::VERTICAL;
            else if (a == "--horizontal") opt.control = ControlUnit::HORIZONTAL;
            else if (a == "--alu-add" || a == "--alu-mul" || a == "--alu-div" || a == "--alu-fmul" || a == "--alu-fdiv") {
                risc201::ALU selected; selected.config = opt.aluConfig;
                if (!selected.selectAlgorithm(a.substr(6), need())) throw invalid_argument("invalid ALU choice");
                opt.aluConfig = selected.config;
            } else if (a.rfind("--",0)==0) throw invalid_argument("unknown option " + a);
            else files.push_back(a);
        }
        if (files.empty() || !validBranchClocks(opt)) throw invalid_argument("provide a hex file and either both positive clock periods or neither");
    } catch (const exception& e) { cerr << e.what() << '\n'; branchUsage(); return 2; }
    vector<BranchCompareRow> rows; bool ok = true;
    for (const auto& file : files) {
        rows.push_back(compareBranches(file,opt)); printBranchComparison(rows.back(),opt);
        ok = rows.back().allMatch && ok;
    }
    if (!opt.csvPath.empty()) {
        if (!writeBranchCsv(rows,opt.csvPath,opt)) { cerr << "Could not write " << opt.csvPath << '\n'; return 2; }
        cout << "CSV written to " << opt.csvPath << '\n';
    }
    return ok ? 0 : 1;
}
} // namespace debugger
