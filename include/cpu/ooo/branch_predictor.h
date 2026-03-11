#pragma once

#include "common/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace riscv {

// 默认分支预测器：Tournament(Local + GShare + Chooser) + BTB(仅JALR目标)。
// - Fetch调用predict生成next PC，并对条件分支执行投机GHR更新
// - Commit调用update训练预测器；条件分支误预测时调用recover_after_branch_mispredict回滚GHR
class BranchPredictor {
public:
    struct BranchMeta {
        bool valid = false;
        bool is_conditional_branch = false;
        uint16_t ghr_before = 0;
        uint16_t local_history_before = 0;
        uint16_t local_history_index = 0;
        bool local_pred_taken = false;
        bool global_pred_taken = false;
        bool global_use_short_history = false;
        bool chooser_use_global = false;
    };

    struct Prediction {
        uint64_t next_pc = 0;
        bool btb_used = false;
        bool btb_hit = false;
        bool bht_used = false;
        bool bht_pred_taken = false;
        BranchMeta branch_meta{};
    };

    BranchPredictor();

    void reset();

    Prediction predict(uint64_t pc, const DecodedInstruction& decoded, uint64_t fallthrough);
    void update(uint64_t pc,
                const DecodedInstruction& decoded,
                bool actual_taken,
                uint64_t actual_target,
                const BranchMeta* meta = nullptr);
    void recover_after_branch_mispredict(uint64_t pc, bool actual_taken, const BranchMeta* meta = nullptr);
    void on_pipeline_flush();

    bool usesTournamentPredictor() const;
    std::string modeName() const;

private:
    static constexpr size_t kBtbEntries = 512;
    static constexpr uint8_t kCounterWeakNotTaken = 1;   // WN
    static constexpr uint8_t kCounterWeakGlobal = 2;     // chooser默认偏global

    static constexpr size_t kGhrBits = 10;
    static constexpr size_t kShortGhrBits = 6;
    static constexpr size_t kGlobalPhtEntries = 1ULL << kGhrBits;   // 4096
    static constexpr size_t kLocalHistoryEntries = 1024;
    static constexpr size_t kLocalHistoryBits = 3;
    static constexpr size_t kLocalPhtEntries = 1ULL << kLocalHistoryBits; // 64
    static constexpr size_t kChooserEntries = 1ULL << kGhrBits;      // 4096

    static constexpr uint16_t kGhrMask = static_cast<uint16_t>((1ULL << kGhrBits) - 1ULL);
    static constexpr uint16_t kShortGhrMask = static_cast<uint16_t>((1ULL << kShortGhrBits) - 1ULL);
    static constexpr uint16_t kLocalHistoryMask = static_cast<uint16_t>((1ULL << kLocalHistoryBits) - 1ULL);

    struct BtbEntry {
        bool valid = false;
        uint64_t tag_pc = 0;
        uint64_t target = 0;
    };

    std::array<BtbEntry, kBtbEntries> btb_{};

    enum class Mode {
        Tournament,
        Simple,
        Gshare,
        TournamentNoSpec,
    };

    Mode mode_ = Mode::Tournament;

    std::array<uint8_t, kGlobalPhtEntries> global_pht_{};
    std::array<uint16_t, kLocalHistoryEntries> committed_local_history_table_{};
    std::array<uint16_t, kLocalHistoryEntries> speculative_local_history_table_{};
    std::array<uint8_t, kLocalPhtEntries> local_pht_{};
    std::array<uint8_t, kChooserEntries> chooser_{};

    uint16_t committed_ghr_ = 0;
    uint16_t speculative_ghr_ = 0;

    static constexpr size_t btbIndex(uint64_t pc) {
        return static_cast<size_t>((pc >> 1) & (kBtbEntries - 1));
    }

    static constexpr size_t localHistoryIndex(uint64_t pc) {
        return static_cast<size_t>((pc >> 1) & (kLocalHistoryEntries - 1));
    }

    static constexpr size_t globalIndex(uint64_t pc, uint16_t ghr) {
        return static_cast<size_t>(((pc >> 1) ^ static_cast<uint64_t>(ghr)) & (kGlobalPhtEntries - 1));
    }

    static constexpr size_t chooserIndex(uint64_t pc, uint16_t ghr) {
        return static_cast<size_t>(((pc >> 1) ^ static_cast<uint64_t>(ghr)) & (kChooserEntries - 1));
    }

    static constexpr size_t localPhtIndex(uint16_t local_history) {
        return static_cast<size_t>(local_history & static_cast<uint16_t>(kLocalPhtEntries - 1));
    }

    static bool counterPredictTaken(uint8_t counter);
    static void counterUpdate(uint8_t& counter, bool taken);
    static uint16_t pushHistory(uint16_t history, bool taken, uint16_t mask);

    bool btbLookup(uint64_t pc, uint64_t& target) const;
    void btbUpdate(uint64_t pc, uint64_t target);
};

} // namespace riscv
