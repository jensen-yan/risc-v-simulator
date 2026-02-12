#pragma once

#include "common/types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace riscv {

// 最小可用预测器：BTB(仅JALR目标) + 2-bit BHT(仅条件分支方向)。
// - Fetch调用predict生成next PC
// - Commit调用update进行训练
class BranchPredictor {
public:
    struct Prediction {
        uint64_t next_pc = 0;
        bool btb_used = false;
        bool btb_hit = false;
        bool bht_used = false;
        bool bht_pred_taken = false;
    };

    BranchPredictor();

    void reset();

    Prediction predict(uint64_t pc, const DecodedInstruction& decoded, uint64_t fallthrough) const;
    void update(uint64_t pc, const DecodedInstruction& decoded, bool actual_taken, uint64_t actual_target);

private:
    static constexpr size_t kBtbEntries = 512;
    static constexpr size_t kBhtEntries = 2048;
    static constexpr uint8_t kBhtInit = 1; // WN: weakly not-taken

    struct BtbEntry {
        bool valid = false;
        uint64_t tag_pc = 0;
        uint64_t target = 0;
    };

    std::array<BtbEntry, kBtbEntries> btb_{};
    std::array<uint8_t, kBhtEntries> bht_{};

    static constexpr size_t btbIndex(uint64_t pc) {
        return static_cast<size_t>((pc >> 1) & (kBtbEntries - 1));
    }

    static constexpr size_t bhtIndex(uint64_t pc) {
        return static_cast<size_t>((pc >> 1) & (kBhtEntries - 1));
    }

    bool btbLookup(uint64_t pc, uint64_t& target) const;
    void btbUpdate(uint64_t pc, uint64_t target);

    bool bhtPredict(uint64_t pc) const;
    void bhtUpdate(uint64_t pc, bool taken);
};

} // namespace riscv

