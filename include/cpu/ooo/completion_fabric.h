#pragma once

#include "cpu/ooo/ooo_types.h"

#include <cstddef>
#include <queue>

namespace riscv {

class CompletionFabric {
public:
    explicit CompletionFabric(size_t completion_width = OOOPipelineConfig::COMPLETION_WIDTH);

    void beginCycle();
    bool trySubmit(const CompletionEvent& event);

    bool empty() const { return ready_events_.empty(); }
    size_t size() const { return ready_events_.size(); }
    size_t completionWidth() const { return completion_width_; }
    size_t usedCompletionSlots() const { return accepted_this_cycle_; }
    size_t availableCompletionSlots() const;

    CompletionEvent popReadyEvent();
    uint64_t clear();
    uint64_t flushYoungerThan(uint64_t instruction_id);

private:
    size_t completion_width_;
    size_t accepted_this_cycle_;
    std::queue<CompletionEvent> ready_events_;
};

} // namespace riscv
