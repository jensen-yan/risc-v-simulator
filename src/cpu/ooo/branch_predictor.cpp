#include "cpu/ooo/branch_predictor.h"

#include <algorithm>

namespace riscv {

BranchPredictor::BranchPredictor() {
    reset();
}

void BranchPredictor::reset() {
    for (auto& entry : btb_) {
        entry = BtbEntry{};
    }
    bht_.fill(kBhtInit);
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

bool BranchPredictor::bhtPredict(uint64_t pc) const {
    const uint8_t c = bht_[bhtIndex(pc)] & 0x3U;
    return c >= 2;
}

void BranchPredictor::bhtUpdate(uint64_t pc, bool taken) {
    uint8_t& c = bht_[bhtIndex(pc)];
    c &= 0x3U;
    if (taken) {
        c = static_cast<uint8_t>(std::min<uint8_t>(3, static_cast<uint8_t>(c + 1)));
    } else {
        c = static_cast<uint8_t>(std::max<uint8_t>(0, static_cast<uint8_t>(c - 1)));
    }
}

BranchPredictor::Prediction BranchPredictor::predict(uint64_t pc,
                                                     const DecodedInstruction& decoded,
                                                     uint64_t fallthrough) const {
    Prediction pred{};
    pred.next_pc = fallthrough;

    switch (decoded.opcode) {
        case Opcode::BRANCH: {
            pred.bht_used = true;
            pred.bht_pred_taken = bhtPredict(pc);
            if (pred.bht_pred_taken) {
                pred.next_pc = pc + static_cast<uint64_t>(static_cast<int64_t>(decoded.imm));
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
                            uint64_t actual_target) {
    switch (decoded.opcode) {
        case Opcode::BRANCH:
            bhtUpdate(pc, actual_taken);
            return;
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

} // namespace riscv

