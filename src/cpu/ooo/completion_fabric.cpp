#include "cpu/ooo/completion_fabric.h"

#include "cpu/ooo/dynamic_inst.h"

#include <utility>

namespace riscv {

CompletionFabric::CompletionFabric(size_t completion_width)
    : completion_width_(completion_width), accepted_this_cycle_(0) {}

void CompletionFabric::beginCycle() {
    accepted_this_cycle_ = 0;
}

bool CompletionFabric::trySubmit(const CompletionEvent& event) {
    if (!event.valid || !event.instruction) {
        return true;
    }

    if (accepted_this_cycle_ >= completion_width_) {
        return false;
    }

    ready_events_.push(event);
    ++accepted_this_cycle_;
    return true;
}

size_t CompletionFabric::availableCompletionSlots() const {
    if (accepted_this_cycle_ >= completion_width_) {
        return 0;
    }
    return completion_width_ - accepted_this_cycle_;
}

CompletionEvent CompletionFabric::popReadyEvent() {
    if (ready_events_.empty()) {
        return CompletionEvent{};
    }

    auto event = ready_events_.front();
    ready_events_.pop();
    return event;
}

uint64_t CompletionFabric::clear() {
    uint64_t dropped = 0;
    while (!ready_events_.empty()) {
        ready_events_.pop();
        ++dropped;
    }
    accepted_this_cycle_ = 0;
    return dropped;
}

uint64_t CompletionFabric::flushYoungerThan(uint64_t instruction_id) {
    if (ready_events_.empty()) {
        return 0;
    }

    std::queue<CompletionEvent> kept_events;
    uint64_t flushed = 0;
    while (!ready_events_.empty()) {
        auto event = ready_events_.front();
        ready_events_.pop();
        if (event.valid && event.instruction &&
            event.instruction->get_instruction_id() > instruction_id) {
            ++flushed;
            continue;
        }
        kept_events.push(std::move(event));
    }
    ready_events_ = std::move(kept_events);
    return flushed;
}

} // namespace riscv
