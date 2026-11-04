#include "branch_predictor.h"
#include <iomanip>
#include <ostream>

namespace risc201 {
void BranchPredictor::print(std::ostream& out, bool table) const {
    const auto flags = out.flags(); const auto precision = out.precision();
    out << "--- Branch prediction ---\n"
        << "Mode             : " << (enabled ? "2-bit dynamic" : "OFF (original sequential fetch)") << "\n"
        << "Table            : " << Entries << " entries, " << StorageBytes << " counter bytes; initial 01\n"
        << "Conditional      : " << stats.conditional << "\n"
        << "Taken            : " << stats.taken << "\n"
        << "Direction correct: " << stats.correct << "\n"
        << "Direction misses : " << stats.conditional - stats.correct << "\n"
        << "Direction accuracy: ";
    if (stats.conditional) out << std::fixed << std::setprecision(2)
        << 100.0 * stats.correct / stats.conditional << "%\n";
    else out << "N/A (no conditional branches)\n";
    out << "Next-PC misses   : " << stats.nextPCMisses << "\n"
        << "Conditional flush: " << stats.conditionalFlushes << "\n"
        << "B/CALL/RET retain original EX-stage redirection in both modes.\n";
    if (table) {
        out << "Index: counter (00/01 predict N; 10/11 predict T; entries may alias)\n";
        for (std::size_t i = 0; i < Entries; ++i) {
            const auto c = counter(static_cast<uint32_t>(i * 4));
            out << std::right << std::setw(3) << i << ':' << (c >> 1) << (c & 1)
                << ((i % 8 == 7) ? '\n' : ' ');
        }
    }
    out.flags(flags); out.precision(precision);
}
} // namespace risc201
