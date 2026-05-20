#include <gtest/gtest.h>

#include "cpu/ooo/cache/non_blocking_cache.h"
#include "cpu/ooo/execute_dcache_access.h"
#include "core/memory.h"

#include <memory>

namespace riscv {

TEST(ExecuteDCacheAccessTest, RecordsReadHitCounters) {
    CPUState state;
    CacheAccessResult result;
    result.hit = true;
    result.latency_cycles = 1;

    ExecuteDCacheAccess::recordResult(state, CacheAccessType::Read, result);

    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_ACCESSES), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_READ_ACCESSES), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_HITS), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_MISSES), 0u);
}

TEST(ExecuteDCacheAccessTest, NoCacheCompletesImmediately) {
    CPUState state;
    ExecutionUnit unit;
    unit.dcache.waiting = true;

    const bool ready = ExecuteDCacheAccess::startOrWait(
        unit, state, CacheAccessType::Write, PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE);

    EXPECT_TRUE(ready);
    EXPECT_FALSE(unit.dcache.waiting);
    EXPECT_FALSE(unit.dcache.request_sent);
}

TEST(ExecuteDCacheAccessTest, CacheMissMarksRequestSentAndCountsLatency) {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    NonBlockingCacheConfig config;
    config.size_bytes = 64;
    config.line_size_bytes = 16;
    config.associativity = 1;
    config.hit_latency = 1;
    config.miss_penalty = 4;
    config.max_outstanding_misses = 1;
    state.l1d_cache = std::make_unique<NonBlockingCache>(config);
    ExecutionUnit unit;
    unit.load_address = 0x100;
    unit.load_size = 4;

    const bool ready = ExecuteDCacheAccess::startOrWait(
        unit, state, CacheAccessType::Write, PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE);

    EXPECT_FALSE(ready);
    EXPECT_TRUE(unit.dcache.request_sent);
    EXPECT_TRUE(unit.dcache.waiting);
    EXPECT_EQ(unit.remaining_cycles, 4);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_MISSES), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE), 4u);
}

TEST(ExecuteDCacheAccessTest, CacheBlockedKeepsRequestUnsentAndCountsOneStall) {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    NonBlockingCacheConfig config;
    config.size_bytes = 64;
    config.line_size_bytes = 16;
    config.associativity = 1;
    config.hit_latency = 1;
    config.miss_penalty = 4;
    config.max_outstanding_misses = 1;
    state.l1d_cache = std::make_unique<NonBlockingCache>(config);
    static_cast<void>(state.l1d_cache->access(state.memory, 0x100, 4, CacheAccessType::Read));
    ExecutionUnit unit;
    unit.load_address = 0x200;
    unit.load_size = 4;

    const bool ready = ExecuteDCacheAccess::startOrWait(
        unit, state, CacheAccessType::Write, PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE);

    EXPECT_FALSE(ready);
    EXPECT_FALSE(unit.dcache.request_sent);
    EXPECT_TRUE(unit.dcache.waiting);
    EXPECT_EQ(unit.remaining_cycles, 1);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_BLOCKED_BY_OUTSTANDING_LIMIT), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_HIT_BLOCKED_BY_OUTSTANDING_LIMIT), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_PENDING_FILL_MERGES), 0u);
}

TEST(ExecuteDCacheAccessTest, PendingFillMergeSendsRequestAndCountsLatency) {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    NonBlockingCacheConfig config;
    config.size_bytes = 64;
    config.line_size_bytes = 16;
    config.associativity = 1;
    config.hit_latency = 1;
    config.miss_penalty = 4;
    config.max_outstanding_misses = 2;
    state.l1d_cache = std::make_unique<NonBlockingCache>(config);
    static_cast<void>(state.l1d_cache->access(state.memory, 0x100, 4, CacheAccessType::Read));
    ExecutionUnit unit;
    unit.load_address = 0x104;
    unit.load_size = 4;

    const bool ready = ExecuteDCacheAccess::startOrWait(
        unit, state, CacheAccessType::Write, PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE);

    EXPECT_FALSE(ready);
    EXPECT_TRUE(unit.dcache.request_sent);
    EXPECT_TRUE(unit.dcache.waiting);
    EXPECT_EQ(unit.remaining_cycles, 4);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE), 4u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_BLOCKED_BY_OUTSTANDING_LIMIT), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_HIT_BLOCKED_BY_OUTSTANDING_LIMIT), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_PENDING_FILL_MERGES), 1u);
}

TEST(ExecuteDCacheAccessTest, AllowsHitWhileMissIsInFlight) {
    CPUState state;
    state.memory = std::make_shared<Memory>(4096);
    NonBlockingCacheConfig config;
    config.size_bytes = 64;
    config.line_size_bytes = 16;
    config.associativity = 1;
    config.hit_latency = 1;
    config.miss_penalty = 4;
    config.max_outstanding_misses = 1;
    state.l1d_cache = std::make_unique<NonBlockingCache>(config);
    static_cast<void>(state.l1d_cache->access(state.memory, 0x100, 4, CacheAccessType::Read));
    while (state.l1d_cache->hasMissInFlight()) {
        state.l1d_cache->tick();
    }
    static_cast<void>(state.l1d_cache->access(state.memory, 0x110, 4, CacheAccessType::Read));
    ExecutionUnit unit;
    unit.load_address = 0x100;
    unit.load_size = 4;

    const bool ready = ExecuteDCacheAccess::startOrWait(
        unit, state, CacheAccessType::Write, PerfCounterId::CACHE_L1D_STALL_CYCLES_STORE);

    EXPECT_TRUE(ready);
    EXPECT_TRUE(unit.dcache.request_sent);
    EXPECT_FALSE(unit.dcache.waiting);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_HITS), 1u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_BLOCKED_BY_OUTSTANDING_LIMIT), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_HIT_BLOCKED_BY_OUTSTANDING_LIMIT), 0u);
    EXPECT_EQ(state.perf_counters.value(PerfCounterId::CACHE_L1D_PENDING_FILL_MERGES), 0u);
}

} // namespace riscv
