#pragma once

#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace riscv {

class Memory;

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

    // 时序访问：用于仅需要命中/未命中延迟的路径（例如Store execute阶段）。
    CacheAccessResult access(std::shared_ptr<Memory> memory,
                             uint64_t address,
                             uint8_t size,
                             CacheAccessType access_type);
    // 时序+功能读取：用于Load路径，返回原始小端拼接值（未做符号扩展）。
    CacheAccessResult load(std::shared_ptr<Memory> memory,
                           uint64_t address,
                           uint8_t size,
                           uint64_t& value);
    // 时序+功能取指：通过I$返回压缩/非压缩原始指令字。
    CacheAccessResult fetchInstruction(std::shared_ptr<Memory> memory,
                                       uint64_t address,
                                       Instruction& instruction);
    // 非时序提交写：用于Store commit阶段，把值写入D$（并维护主存镜像）。
    void commitStore(std::shared_ptr<Memory> memory, uint64_t address, uint8_t size, uint64_t value);
    // 失效给定范围命中的cache line，用于处理cache外部写入导致的数据陈旧。
    void invalidateRange(uint64_t address, uint64_t size);

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
        std::vector<uint8_t> data;
    };

    using CacheSet = std::vector<CacheLine>;

    BlockingCacheConfig config_;
    size_t set_count_ = 0;
    std::vector<CacheSet> sets_;
    uint64_t lru_clock_ = 0;

    bool miss_in_flight_ = false;
    int miss_service_remaining_cycles_ = 0;

    bool isBypassAccess(const std::shared_ptr<Memory>& memory, uint64_t address, uint8_t size) const;
    CacheAccessResult ensureResident(std::shared_ptr<Memory> memory,
                                     uint64_t address,
                                     uint8_t size,
                                     CacheAccessType access_type,
                                     bool model_timing);

    std::vector<uint64_t> enumerateLineAddresses(uint64_t address, uint8_t size) const;
    size_t lineToSetIndex(uint64_t line_address) const;
    uint64_t lineToTag(uint64_t line_address) const;
    uint64_t lineToBaseAddress(uint64_t line_address) const;

    CacheLine* findLine(uint64_t line_address);
    CacheLine& allocateLine(uint64_t line_address, bool& dirty_eviction);
    void touchLine(CacheLine& line);

    static uint64_t readMemoryValue(const std::shared_ptr<Memory>& memory, uint64_t address, uint8_t size);
    static void writeMemoryValue(const std::shared_ptr<Memory>& memory, uint64_t address, uint8_t size, uint64_t value);
    void fillLineFromMemory(const std::shared_ptr<Memory>& memory, uint64_t line_address, CacheLine& line);
    void writebackLineToMemory(const std::shared_ptr<Memory>& memory, uint64_t line_address, const CacheLine& line);
    uint8_t readCachedByte(uint64_t address) const;
    void writeCachedByte(uint64_t address, uint8_t value, bool mark_dirty);
};

} // namespace riscv
