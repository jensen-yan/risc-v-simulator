#include "cpu/ooo/memory_timing_backend.h"

#include "common/types.h"
#include "dramsim3.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace riscv {

namespace {

class DRAMSim3MemoryTimingBackend final : public MemoryTimingBackend {
public:
    DRAMSim3MemoryTimingBackend(const std::string& config_path,
                                const std::string& output_dir)
        : output_dir_(output_dir.empty() ? "dramsim3-output" : output_dir) {
        if (config_path.empty()) {
            throw std::invalid_argument("DRAMSim3 backend requires --dramsim3-ini");
        }
        if (!std::filesystem::exists(config_path)) {
            throw std::invalid_argument("DRAMSim3 config does not exist: " + config_path);
        }
        std::filesystem::create_directories(output_dir_);

        memory_system_.reset(dramsim3::GetMemorySystem(
            config_path,
            output_dir_,
            [this](uint64_t address) { recordCompletion(address, /*is_write=*/false); },
            [this](uint64_t address) { recordCompletion(address, /*is_write=*/true); }));
        if (!memory_system_) {
            throw std::runtime_error("failed to create DRAMSim3 memory system");
        }
    }

    int accessLatencyCycles(const MemoryTimingRequest& request) override {
        const bool is_write = request.kind == MemoryTimingRequestKind::Writeback;
        recordRequest(request.kind);

        const uint64_t start_cycle = elapsed_cycles_;
        while (!memory_system_->WillAcceptTransaction(request.address, is_write)) {
            tickMemorySystem();
        }
        if (!memory_system_->AddTransaction(request.address, is_write)) {
            throw SimulatorException("DRAMSim3 rejected a transaction after WillAcceptTransaction");
        }

        while (!consumeCompletion(request.address, is_write)) {
            tickMemorySystem();
        }

        const uint64_t latency = elapsed_cycles_ - start_cycle;
        if (latency > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            throw SimulatorException("DRAMSim3 latency exceeds int range");
        }
        stats_.total_latency_cycles += latency;
        stats_.max_latency_cycles = std::max(stats_.max_latency_cycles, latency);
        return static_cast<int>(latency);
    }

    void resetStats() override {
        stats_ = MemoryTimingStats{};
        if (memory_system_) {
            memory_system_->ResetStats();
        }
    }

    const MemoryTimingStats& getStats() const override { return stats_; }

private:
    void tickMemorySystem() {
        memory_system_->ClockTick();
        ++elapsed_cycles_;
    }

    void recordRequest(MemoryTimingRequestKind kind) {
        switch (kind) {
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
    }

    void recordCompletion(uint64_t address, bool is_write) {
        auto& completions = is_write ? completed_writes_ : completed_reads_;
        ++completions[address];
    }

    bool consumeCompletion(uint64_t address, bool is_write) {
        auto& completions = is_write ? completed_writes_ : completed_reads_;
        auto it = completions.find(address);
        if (it == completions.end()) {
            return false;
        }
        if (--it->second == 0) {
            completions.erase(it);
        }
        return true;
    }

    std::string output_dir_;
    std::unique_ptr<dramsim3::MemorySystem> memory_system_;
    MemoryTimingStats stats_{};
    uint64_t elapsed_cycles_ = 0;
    std::unordered_map<uint64_t, uint64_t> completed_reads_;
    std::unordered_map<uint64_t, uint64_t> completed_writes_;
};

} // namespace

std::shared_ptr<MemoryTimingBackend> createDRAMSim3MemoryTimingBackend(
    const std::string& config_path,
    const std::string& output_dir) {
    return std::make_shared<DRAMSim3MemoryTimingBackend>(config_path, output_dir);
}

} // namespace riscv
