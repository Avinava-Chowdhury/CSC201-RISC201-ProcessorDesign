#pragma once
// PC-indexed bimodal predictor: exactly the textbook 2-bit saturating FSM.
// No speculative training, BTB, return prediction, or ISA changes.
#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include "isa.h"

namespace risc201 {
struct BranchPrediction {
    uint32_t nextPC = 0;
    bool taken = false;
};

class BranchPredictor {
public:
    static constexpr std::size_t Entries = 256;
    static constexpr std::size_t StorageBytes = Entries / 4;
    static_assert((Entries & (Entries - 1)) == 0);
    bool enabled = false; // Original sequential fetch remains the default.
    struct Stats {
        uint64_t conditional = 0, taken = 0, correct = 0;
        uint64_t nextPCMisses = 0, conditionalFlushes = 0;
    } stats;

    BranchPredictor() { reset(); }
    void reset() noexcept { counters.fill(0x55); stats = {}; } // four 01 counters/byte
    static constexpr std::size_t index(uint32_t pc) noexcept {
        return (pc >> 2) & (Entries - 1); // discard guaranteed-zero alignment bits
    }
    uint8_t counter(uint32_t pc) const noexcept {
        const auto i = index(pc);
        return (counters[i >> 2] >> ((i & 3) * 2)) & 3;
    }
    BranchPrediction predict(uint32_t pc, uint32_t word) const noexcept {
        BranchPrediction prediction{pc + BYTES_PER_WORD, false};
        const auto op = static_cast<OperationCode>(word >> 27);
        if (enabled && (op == OperationCode::BEQ || op == OperationCode::BGT)
                    && counter(pc) >= 2) {
            prediction.taken = true;
            // Same signed 27-bit byte displacement as the ISA decoder; unsigned
            // addition implements the existing architectural address arithmetic.
            prediction.nextPC = pc + static_cast<uint32_t>(extendSignBit(word, 27));
        }
        return prediction;
    }
    // Called exactly once at EX for a surviving conditional branch. 'saved' is
    // its fetch-time prediction, not a new lookup after intervening training.
    void resolve(uint32_t pc, bool taken, const BranchPrediction& saved,
                 uint32_t actualNextPC, bool flushed) noexcept {
        ++stats.conditional;
        stats.taken += taken;
        stats.correct += (saved.taken == taken);
        stats.nextPCMisses += (saved.nextPC != actualNextPC);
        stats.conditionalFlushes += flushed;
        if (!enabled) return;
        const auto i = index(pc);
        const unsigned shift = (i & 3) * 2;
        auto& byte = counters[i >> 2];
        const unsigned old = (byte >> shift) & 3;
        const unsigned next = taken ? old + (old < 3) : old - (old > 0);
        byte = static_cast<uint8_t>((byte & ~(3u << shift)) | (next << shift));
    }
    void print(std::ostream& out, bool table = false) const;
private:
    std::array<uint8_t, StorageBytes> counters{};
};
} // namespace risc201
