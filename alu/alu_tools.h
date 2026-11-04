#pragma once
// Selection, timing and comparisons are kept here so ALU.cpp contains arithmetic.
// Included after the ALU class in alu.h; inline definitions need no Makefile changes.
#include <chrono>
#include <iomanip>
#include <ostream>
#include <vector>

namespace risc201 {
namespace alu_tools_detail {
inline const char* addName(AddAlgorithm a) {
    switch (a) { case AddAlgorithm::RIPPLE: return "ripple";
 default: return "lookahead"; }
}
inline const char* mulName(MulAlgorithm a) {
    switch (a) { case MulAlgorithm::ITERATIVE: return "iterative"; case MulAlgorithm::BOOTH: return "booth";
        default: return "booth"; }
}
inline const char* divName(DivAlgorithm a) { return a == DivAlgorithm::RESTORING ? "restoring-fast" : "non-restoring"; }
inline const char* fpDivName(FpDivAlgorithm a) {
    return a == FpDivAlgorithm::SIMPLE ? "simple-fast" : "newton";
}
inline bool parseMul(const std::string& s, MulAlgorithm& a) {
    if (s == "iterative") a = MulAlgorithm::ITERATIVE;
    else if (s == "booth") a = MulAlgorithm::BOOTH;
    else return false;
    return true;
}
}

inline bool ALU::selectAlgorithm(const std::string& family, const std::string& name) {
    if (family == "add") {
        if (name == "ripple") config.add = AddAlgorithm::RIPPLE;
        else if (name == "lookahead") config.add = AddAlgorithm::LOOKAHEAD;
        else return false;
    } else if (family == "mul") return alu_tools_detail::parseMul(name, config.mul);
    else if (family == "fmul") return alu_tools_detail::parseMul(name, config.fpMul);
    else if (family == "div") {
        if (name == "restoring" || name == "restoring-fast") config.div = DivAlgorithm::RESTORING;
        else if (name == "non-restoring") config.div = DivAlgorithm::NON_RESTORING;
        else return false;
    } else if (family == "fdiv") {
        if (name == "simple" || name == "simple-fast") config.fpDiv = FpDivAlgorithm::SIMPLE;
        else if (name == "newton" || name == "newton-raphson") config.fpDiv = FpDivAlgorithm::NEWTON;
        else return false;
    } else return false;
    return true;
}

inline void ALU::printAlgorithms(std::ostream& out) const {
    out << "ALU selections (apply to subsequent execution; preserved across CPU reset/load):\n"
        << "  add  : " << alu_tools_detail::addName(config.add) << "  [lookahead | ripple]\n"
        << "  mul  : " << alu_tools_detail::mulName(config.mul) << "  [booth | iterative]\n"
        << "  div  : " << alu_tools_detail::divName(config.div) << "  [restoring-fast | non-restoring] (DIV and MOD)\n"
        << "  fmul : " << alu_tools_detail::mulName(config.fpMul) << "  [booth | iterative]\n"
        << "  fdiv : " << alu_tools_detail::fpDivName(config.fpDiv) << "  [simple-fast | newton]\n"
        << "  fadd/fsub use align/add/normalize/round with the selected adder.\n"
        << "  lookahead: packed parallel-prefix carry network; booth: radix-4 (16 iterations).\n"
        << "  Division: exact shortcuts, aligned restoring, leading-zero-skipping non-restoring.\n"
        << "  FP restoring: 27 quotient decisions; Newton: 64 seeds + 3 refinements + exact correction.\n";
}

inline std::string ALU::algorithmLabel(Opcode op, uint8_t function) const {
    std::string label;
    if (op == Opcode::FPU) {
        if (function == 2) label = std::string("fmul/") + alu_tools_detail::mulName(config.fpMul);
        else if (function == 3) {
            label = std::string("fdiv/") + alu_tools_detail::fpDivName(config.fpDiv);
            if (config.fpDiv == FpDivAlgorithm::NEWTON)
                label += std::string("/mul=") + alu_tools_detail::mulName(config.fpMul);
        }
        else label = function == 1 ? "fsub/generic" : "fadd/generic";
    } else if (op == Opcode::MUL) label = std::string("mul/") + alu_tools_detail::mulName(config.mul);
    else if (op == Opcode::DIV || op == Opcode::MOD)
        label = std::string(op == Opcode::DIV ? "div/" : "mod/") + alu_tools_detail::divName(config.div);
    else if (op == Opcode::ADD) label = "add";
    else if (op == Opcode::SUB) label = "sub";
    else if (op == Opcode::NEG || op == Opcode::ABS) label = "neg-abs";
    else if (op == Opcode::LSL || op == Opcode::LSR || op == Opcode::ASR || op == Opcode::ROL || op == Opcode::ROR)
        return "shift-rotate/bitwise";
    else return "logical-other/bitwise";
    return label + "/add=" + alu_tools_detail::addName(config.add);
}

inline void ALU::printTiming(std::ostream& out) const {
    const auto oldFlags = out.flags(); const auto oldPrecision = out.precision();
    out << "\nALU host execution time (steady_clock; includes EX work even if later squashed):\n"
        << std::left << std::setw(47) << "Algorithm / selected adder" << std::right
        << std::setw(10) << "Calls" << std::setw(14) << "Total(us)"
        << std::setw(14) << "Average(ns)" << std::setw(12) << "Last(ns)" << "\n";
    for (const auto& entry : timings) {
        const auto& t = entry.second;
        out << std::left << std::setw(47) << entry.first << std::right << std::setw(10) << t.calls
            << std::fixed << std::setprecision(2) << std::setw(14) << t.totalNs / 1000.0
            << std::setw(14) << (t.calls ? double(t.totalNs) / t.calls : 0)
            << std::setw(12) << t.lastNs << "\n";
    }
    if (timings.empty()) out << "  No measured ALU calls since reset/load or 'alu timing reset'.\n";
    out << "Measured software duration, not simulated hardware delay or CPU cycles.\n";
    out.flags(oldFlags); out.precision(oldPrecision);
}

inline bool ALU::compareAlgorithms(std::ostream& out, const std::string& operation,
                            int32_t a, int32_t b, unsigned repeats) {
    if (repeats == 0 || repeats > 1000000) return false;
    Opcode op; uint8_t function = 0; std::string family;
    std::vector<std::string> choices;
    if (operation == "add" || operation == "sub" || operation == "fadd" || operation == "fsub") {
        op = operation == "add" ? Opcode::ADD : operation == "sub" ? Opcode::SUB : Opcode::FPU;
        function = operation == "fsub" ? 1 : 0;
        family = "add"; choices = {"lookahead", "ripple"};
    } else if (operation == "mul" || operation == "fmul") {
        op = operation == "mul" ? Opcode::MUL : Opcode::FPU; function = 2;
        family = operation;
        choices = operation == "fmul" ? std::vector<std::string>{"iterative", "booth"}
                                      : std::vector<std::string>{"booth", "iterative"};
    } else if (operation == "div" || operation == "mod") {
        op = operation == "div" ? Opcode::DIV : Opcode::MOD;
        family = "div"; choices = {"restoring-fast", "non-restoring"};
    } else if (operation == "fdiv") {
        op = Opcode::FPU; function = 3; family = "fdiv";
        choices = {"simple-fast", "newton"};
    } else return false;
    // Work on a separate ALU: selections, timing counters and CPU state are untouched.
    ALU worker;
    worker.config = config;
    worker.timingEnabled = false;
    worker.selectAlgorithm(family, operation == "fmul" ? "iterative" : choices.front());
    const ALUResult expected = worker.executeCore(op, a, b, function);
    const auto oldFlags = out.flags(); const auto oldPrecision = out.precision();
    out << "\nALU comparison: " << operation << " a=0x" << std::hex << uint32_t(a)
        << " b=0x" << uint32_t(b) << std::dec << " repeats=" << repeats << "\n"
        << "FP operands/results are binary32 bit patterns. Current adder: " << alu_tools_detail::addName(config.add) << "\n"
        << std::left << std::setw(16) << "Algorithm" << std::right << std::setw(14) << "Result(hex)"
        << std::setw(8) << "Match" << std::setw(14) << "Total(us)" << std::setw(14) << "Average(ns)" << "\n";
    bool allMatch = true;
    for (const auto& choice : choices) {
        worker.selectAlgorithm(family, choice);
        volatile uint32_t warmup = 0;
        for (unsigned i = 0; i < 16; ++i) warmup = uint32_t(worker.executeCore(op, a, b, function).value);
        (void)warmup;
        const auto result = worker.executeCore(op, a, b, function);
        volatile uint32_t checksum = 0;
        volatile int32_t operandA = a, operandB = b; // prevent optimizer from hoisting constant work
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < repeats; ++i) {
            const auto r = worker.executeCore(op, operandA, operandB, function);
            checksum = checksum ^ uint32_t(r.value) ^ uint32_t(r.flags.overflow);
        }
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        (void)checksum;
        const bool match = result.value == expected.value && result.flags.zero == expected.flags.zero
            && result.flags.carry == expected.flags.carry && result.flags.overflow == expected.flags.overflow;
        allMatch = allMatch && match;
        out << std::left << std::setw(16) << choice << std::right << std::hex << std::setw(14)
            << uint32_t(result.value) << std::dec << std::setw(8) << (match ? "yes" : "NO")
            << std::fixed << std::setprecision(2) << std::setw(14) << ns / 1000.0
            << std::setw(14) << double(ns) / repeats << "\n";
    }
    out << "Match checks result and flags for these operands only. Timings include the C++ dispatch/loop;\n"
           "they do not model hardware parallelism. Pipeline cycle counts are unchanged.\n";
    out.flags(oldFlags); out.precision(oldPrecision);
    return allMatch;
}



} // namespace risc201
