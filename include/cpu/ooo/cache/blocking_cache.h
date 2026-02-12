#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace riscv {

enum class CacheWritePolicy : uint8_t {
    WriteBackWriteAllocate
};

struct BlockingCacheConfig {
    size_t size_bytes = 32 * 1024;
    size_t line_size_bytes = 64;
    size_t associativity = 4;
    int hit_latency = 1;
    int miss_penalty = 20;
    CacheWritePolicy write_policy = CacheWritePolicy::WriteBackWriteAllocate;
};

enum class CacheAccessType : uint8_t {
    Read,
    Write
};

struct CacheAccessResult {
    bool hit = false;
    int latency_cycles = 0;
    bool blocked = false;
    bool dirty_eviction = false;
};

class BlockingCache {
public:
    explicit BlockingCache(const BlockingCacheConfig& config);

    CacheAccessResult access(uint64_t address, uint8_t size, CacheAccessType access_type);
    void tick();
    void flushInFlight();
    void reset();

    bool hasMissInFlight() const { return miss_in_flight_; }
    int missServiceRemainingCycles() const { return miss_service_remaining_cycles_; }
    const BlockingCacheConfig& getConfig() const { return config_; }

private:
    struct CacheLine {
        bool valid = false;
        bool dirty = false;
        uint64_t tag = 0;
        uint64_t lru_stamp = 0;
    };

    using CacheSet = std::vector<CacheLine>;

    BlockingCacheConfig config_;
    size_t set_count_ = 0;
    std::vector<CacheSet> sets_;
    uint64_t lru_clock_ = 0;

    bool miss_in_flight_ = false;
    int miss_service_remaining_cycles_ = 0;

    std::vector<uint64_t> enumerateLineAddresses(uint64_t address, uint8_t size) const;
    size_t lineToSetIndex(uint64_t line_address) const;
    uint64_t lineToTag(uint64_t line_address) const;

    CacheLine* findLine(uint64_t line_address);
    CacheLine& allocateLine(uint64_t line_address, bool& dirty_eviction);
    void touchLine(CacheLine& line);
};

} // namespace riscv

