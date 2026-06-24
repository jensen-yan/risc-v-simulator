#include "cpu/ooo/memory_timing_backend.h"

#include <algorithm>
#include <stdexcept>

namespace riscv {

FixedLatencyMemoryTimingBackend::FixedLatencyMemoryTimingBackend(int latency_cycles)
    : latency_cycles_(latency_cycles) {
    if (latency_cycles_ < 0) {
        throw std::invalid_argument("memory timing latency cannot be negative");
    }
}

int FixedLatencyMemoryTimingBackend::accessLatencyCycles(const MemoryTimingRequest& request) {
    switch (request.kind) {
        case MemoryTimingRequestKind::DemandRead:
            ++stats_.read_requests;
            break;
        case MemoryTimingRequestKind::PrefetchRead:
            ++stats_.prefetch_requests;
            break;
        case MemoryTimingRequestKind::Writeback:
            ++stats_.writeback_requests;
            break;
    }

    const auto latency = static_cast<uint64_t>(latency_cycles_);
    stats_.total_latency_cycles += latency;
    stats_.max_latency_cycles = std::max(stats_.max_latency_cycles, latency);
    return latency_cycles_;
}

void FixedLatencyMemoryTimingBackend::resetStats() {
    stats_ = MemoryTimingStats{};
}

} // namespace riscv
