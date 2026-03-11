#include "cpu/ooo/branch_predictor.h"

#include <algorithm>
#include <cstdlib>

namespace riscv {

BranchPredictor::BranchPredictor() {
    const char* mode = std::getenv("RISCV_SIM_BP_MODE");
    if (mode != nullptr) {
        const std::string mode_str(mode);
        if (mode_str == "simple") {
            mode_ = Mode::Simple;
        } else if (mode_str == "gshare") {
            mode_ = Mode::Gshare;
        } else if (mode_str == "tournament_nospec") {
            mode_ = Mode::TournamentNoSpec;
        }
    }
    reset();
}

void BranchPredictor::reset() {
    for (auto& entry : btb_) {
        entry = BtbEntry{};
    }
    global_pht_.fill(kCounterWeakNotTaken);
    committed_local_history_table_.fill(0);
    speculative_local_history_table_.fill(0);
    local_pht_.fill(kCounterWeakNotTaken);
    chooser_.fill(kCounterWeakGlobal);
    committed_ghr_ = 0;
    speculative_ghr_ = 0;
}

bool BranchPredictor::btbLookup(uint64_t pc, uint64_t& target) const {
    const auto& entry = btb_[btbIndex(pc)];
    if (entry.valid && entry.tag_pc == pc) {
        target = entry.target;
        return true;
    }
    return false;
}

void BranchPredictor::btbUpdate(uint64_t pc, uint64_t target) {
    auto& entry = btb_[btbIndex(pc)];
    entry.valid = true;
    entry.tag_pc = pc;
    entry.target = target;
}

bool BranchPredictor::counterPredictTaken(uint8_t counter) {
    const uint8_t c = static_cast<uint8_t>(counter & 0x3U);
    return c >= 2;
}

void BranchPredictor::counterUpdate(uint8_t& counter, bool taken) {
    uint8_t& c = counter;
    c &= 0x3U;
    if (taken) {
        c = static_cast<uint8_t>(std::min<uint8_t>(3, static_cast<uint8_t>(c + 1)));
    } else {
        c = static_cast<uint8_t>(std::max<uint8_t>(0, static_cast<uint8_t>(c - 1)));
    }
}

uint16_t BranchPredictor::pushHistory(uint16_t history, bool taken, uint16_t mask) {
    const uint16_t shifted = static_cast<uint16_t>((history << 1) & mask);
    return static_cast<uint16_t>(shifted | (taken ? 1U : 0U));
}

BranchPredictor::Prediction BranchPredictor::predict(uint64_t pc,
                                                     const DecodedInstruction& decoded,
                                                     uint64_t fallthrough) {
    Prediction pred{};
    pred.next_pc = fallthrough;

    switch (decoded.opcode) {
        case Opcode::BRANCH: {
            pred.bht_used = true;
            if (mode_ == Mode::Simple) {
                const size_t global_idx = globalIndex(pc, 0);
                const bool pred_taken = counterPredictTaken(global_pht_[global_idx]);
                pred.bht_pred_taken = pred_taken;
                if (pred_taken) {
                    pred.next_pc = pc + static_cast<uint64_t>(static_cast<int64_t>(decoded.imm));
                }
                return pred;
            }

            const bool use_speculative_history = (mode_ != Mode::TournamentNoSpec);
            const uint16_t ghr_before = use_speculative_history ? speculative_ghr_ : committed_ghr_;
            const uint16_t lht_index = static_cast<uint16_t>(localHistoryIndex(pc));
            const uint16_t local_history_before = use_speculative_history
                ? static_cast<uint16_t>(speculative_local_history_table_[lht_index] & kLocalHistoryMask)
                : static_cast<uint16_t>(committed_local_history_table_[lht_index] & kLocalHistoryMask);
            const size_t local_idx = localPhtIndex(local_history_before);
            const bool use_short_global_history = decoded.imm < 0;
            const uint16_t ghr_for_global_index = use_short_global_history
                                                     ? static_cast<uint16_t>(ghr_before & kShortGhrMask)
                                                     : ghr_before;
            const size_t global_idx = globalIndex(pc, ghr_for_global_index);
            const size_t chooser_idx = chooserIndex(pc, ghr_before);

            const bool local_pred_taken = counterPredictTaken(local_pht_[local_idx]);
            const bool global_pred_taken = counterPredictTaken(global_pht_[global_idx]);
            const uint8_t chooser_counter = chooser_[chooser_idx] & 0x3U;
            const bool use_local = (mode_ == Mode::Tournament && chooser_counter == 0);
            const bool force_global = (mode_ == Mode::Gshare || mode_ == Mode::TournamentNoSpec);
            const bool pred_taken = (force_global || !use_local)
                                        ? global_pred_taken
                                        : local_pred_taken;

            pred.bht_pred_taken = pred_taken;
            pred.branch_meta.valid = true;
            pred.branch_meta.is_conditional_branch = true;
            pred.branch_meta.ghr_before = ghr_before;
            pred.branch_meta.local_history_before = local_history_before;
            pred.branch_meta.local_history_index = lht_index;
            pred.branch_meta.local_pred_taken = local_pred_taken;
            pred.branch_meta.global_pred_taken = global_pred_taken;
            pred.branch_meta.global_use_short_history = use_short_global_history;
            pred.branch_meta.chooser_use_global = (force_global || !use_local);

            if (pred_taken) {
                pred.next_pc = pc + static_cast<uint64_t>(static_cast<int64_t>(decoded.imm));
            }
            if (use_speculative_history) {
                speculative_local_history_table_[lht_index] =
                    pushHistory(local_history_before, pred_taken, kLocalHistoryMask);
                speculative_ghr_ = pushHistory(speculative_ghr_, pred_taken, kGhrMask);
            }
            return pred;
        }
        case Opcode::JAL: {
            pred.next_pc = pc + static_cast<uint64_t>(static_cast<int64_t>(decoded.imm));
            return pred;
        }
        case Opcode::JALR: {
            pred.btb_used = true;
            uint64_t target = 0;
            pred.btb_hit = btbLookup(pc, target);
            if (pred.btb_hit) {
                pred.next_pc = target;
            }
            return pred;
        }
        default:
            return pred;
    }
}

void BranchPredictor::update(uint64_t pc,
                             const DecodedInstruction& decoded,
                             bool actual_taken,
                             uint64_t actual_target,
                             const BranchMeta* meta) {
    switch (decoded.opcode) {
        case Opcode::BRANCH: {
            if (mode_ == Mode::Simple) {
                const size_t global_idx = globalIndex(pc, 0);
                counterUpdate(global_pht_[global_idx], actual_taken);
                return;
            }

            uint16_t ghr_for_index = committed_ghr_;
            uint16_t local_history_before =
                static_cast<uint16_t>(committed_local_history_table_[localHistoryIndex(pc)] & kLocalHistoryMask);
            uint16_t local_hist_index = static_cast<uint16_t>(localHistoryIndex(pc));
            bool local_pred_taken = false;
            bool global_pred_taken = false;

            if (meta && meta->valid && meta->is_conditional_branch) {
                ghr_for_index = static_cast<uint16_t>(meta->ghr_before & kGhrMask);
                local_history_before = static_cast<uint16_t>(meta->local_history_before & kLocalHistoryMask);
                local_hist_index = static_cast<uint16_t>(meta->local_history_index & static_cast<uint16_t>(kLocalHistoryEntries - 1));
                local_pred_taken = meta->local_pred_taken;
                global_pred_taken = meta->global_pred_taken;
            } else {
                const size_t local_idx_fallback = localPhtIndex(local_history_before);
                local_pred_taken = counterPredictTaken(local_pht_[local_idx_fallback]);
                const size_t global_idx_fallback = globalIndex(pc, ghr_for_index);
                global_pred_taken = counterPredictTaken(global_pht_[global_idx_fallback]);
            }

            const size_t local_idx = localPhtIndex(local_history_before);
            const bool use_short_global_history =
                (meta && meta->valid && meta->is_conditional_branch)
                    ? meta->global_use_short_history
                    : (decoded.imm < 0);
            const uint16_t ghr_for_global_index = use_short_global_history
                                                     ? static_cast<uint16_t>(ghr_for_index & kShortGhrMask)
                                                     : ghr_for_index;
            const size_t global_idx = globalIndex(pc, ghr_for_global_index);
            const size_t chooser_idx = chooserIndex(pc, ghr_for_index);

            counterUpdate(local_pht_[local_idx], actual_taken);
            counterUpdate(global_pht_[global_idx], actual_taken);

            if (mode_ == Mode::Tournament || mode_ == Mode::TournamentNoSpec) {
                if (local_pred_taken != global_pred_taken) {
                    if (global_pred_taken == actual_taken) {
                        counterUpdate(chooser_[chooser_idx], true);
                    } else if (local_pred_taken == actual_taken) {
                        counterUpdate(chooser_[chooser_idx], false);
                    }
                }
            }

            const uint16_t committed_local_history_base =
                (meta && meta->valid && meta->is_conditional_branch)
                    ? static_cast<uint16_t>(meta->local_history_before & kLocalHistoryMask)
                    : static_cast<uint16_t>(committed_local_history_table_[local_hist_index] & kLocalHistoryMask);
            committed_local_history_table_[local_hist_index] =
                pushHistory(committed_local_history_base, actual_taken, kLocalHistoryMask);
            committed_ghr_ = pushHistory(committed_ghr_, actual_taken, kGhrMask);
            return;
        }
        case Opcode::JALR:
            if (actual_taken) {
                btbUpdate(pc, actual_target);
            }
            return;
        case Opcode::JAL:
        default:
            return;
    }
}

void BranchPredictor::recover_after_branch_mispredict(uint64_t pc, bool actual_taken, const BranchMeta* meta) {
    (void)pc;
    if (mode_ == Mode::Simple) {
        return;
    }
    if (meta && meta->valid && meta->is_conditional_branch) {
        const uint16_t ghr_before = static_cast<uint16_t>(meta->ghr_before & kGhrMask);
        speculative_ghr_ = pushHistory(ghr_before, actual_taken, kGhrMask);
        speculative_local_history_table_ = committed_local_history_table_;
        return;
    }

    speculative_ghr_ = committed_ghr_;
    speculative_local_history_table_ = committed_local_history_table_;
}

void BranchPredictor::on_pipeline_flush() {
    if (mode_ == Mode::Simple) {
        return;
    }
    speculative_ghr_ = committed_ghr_;
    speculative_local_history_table_ = committed_local_history_table_;
}

bool BranchPredictor::usesTournamentPredictor() const {
    return mode_ == Mode::Tournament || mode_ == Mode::TournamentNoSpec;
}

std::string BranchPredictor::modeName() const {
    switch (mode_) {
        case Mode::Tournament:
            return "tournament";
        case Mode::Simple:
            return "simple";
        case Mode::Gshare:
            return "gshare";
        case Mode::TournamentNoSpec:
            return "tournament_nospec";
    }
    return "unknown";
}

} // namespace riscv
