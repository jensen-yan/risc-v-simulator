#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace riscv {

enum class MemoryTimingRequestKind : uint8_t {
    DemandRead,
    PrefetchRead,
    Writeback
};

struct MemoryTimingRequest {
    MemoryTimingRequestKind kind = MemoryTimingRequestKind::DemandRead;
    uint64_t address = 0;
    size_t size = 0;
};

struct MemoryTimingStats {
    uint64_t read_requests = 0;
    uint64_t prefetch_requests = 0;
    uint64_t writeback_requests = 0;
    uint64_t total_latency_cycles = 0;
    uint64_t max_latency_cycles = 0;
};

class MemoryTimingBackend {
public:
    virtual ~MemoryTimingBackend() = default;

    virtual int accessLatencyCycles(const MemoryTimingRequest& request) = 0;
    virtual void resetStats() = 0;
    virtual const MemoryTimingStats& getStats() const = 0;
};

class FixedLatencyMemoryTimingBackend final : public MemoryTimingBackend {
public:
    explicit FixedLatencyMemoryTimingBackend(int latency_cycles);

    int accessLatencyCycles(const MemoryTimingRequest& request) override;
    void resetStats() override;
    const MemoryTimingStats& getStats() const override { return stats_; }

private:
    int latency_cycles_ = 0;
    MemoryTimingStats stats_{};
};

#ifdef RISCV_SIM_ENABLE_DRAMSIM3
std::shared_ptr<MemoryTimingBackend> createDRAMSim3MemoryTimingBackend(
    const std::string& config_path,
    const std::string& output_dir);
#endif

} // namespace riscv
