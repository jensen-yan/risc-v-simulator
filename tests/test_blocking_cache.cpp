#include <gtest/gtest.h>

#include "core/memory.h"
#include "cpu/ooo/cache/blocking_cache.h"

#include <limits>
#include <memory>

namespace riscv {

namespace {

void drainMiss(BlockingCache& cache) {
    while (cache.hasMissInFlight()) {
        cache.tick();
    }
}

BlockingCacheConfig makeDefaultConfig() {
    BlockingCacheConfig cfg;
    cfg.size_bytes = 32 * 1024;
    cfg.line_size_bytes = 64;
    cfg.associativity = 4;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    return cfg;
}

} // namespace

TEST(BlockingCacheTest, ColdMissThenHit) {
    auto memory = std::make_shared<Memory>(4096);
    memory->writeWord(0x100, 0xDEADBEEF);

    BlockingCache cache(makeDefaultConfig());

    const auto first = cache.access(memory, 0x100, 4, CacheAccessType::Read);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);
    EXPECT_EQ(first.latency_cycles, 21);

    drainMiss(cache);

    const auto second = cache.access(memory, 0x100, 4, CacheAccessType::Read);
    EXPECT_FALSE(second.blocked);
    EXPECT_TRUE(second.hit);
    EXPECT_EQ(second.latency_cycles, 1);
}

TEST(BlockingCacheTest, DirtyEvictionAfterCommittedStore) {
    BlockingCacheConfig cfg;
    cfg.size_bytes = 64;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;

    auto memory = std::make_shared<Memory>(1024);
    BlockingCache cache(cfg);

    cache.commitStore(memory, 0x0, 4, 0xAABBCCDD);

    const auto replace = cache.access(memory, 0x40, 4, CacheAccessType::Read);
    EXPECT_FALSE(replace.blocked);
    EXPECT_FALSE(replace.hit);
    EXPECT_TRUE(replace.dirty_eviction);
}

TEST(BlockingCacheTest, AccessIsBlockedWhileMissInFlight) {
    BlockingCacheConfig cfg;
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;

    auto memory = std::make_shared<Memory>(1024);
    BlockingCache cache(cfg);

    const auto miss = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(miss.blocked);
    EXPECT_FALSE(miss.hit);

    const auto blocked = cache.access(memory, 0x80, 4, CacheAccessType::Read);
    EXPECT_TRUE(blocked.blocked);
    EXPECT_EQ(blocked.latency_cycles, 0);

    drainMiss(cache);

    const auto next = cache.access(memory, 0x80, 4, CacheAccessType::Read);
    EXPECT_FALSE(next.blocked);
    EXPECT_FALSE(next.hit);
}

TEST(BlockingCacheTest, AccessAllowsMultipleOutstandingMissesUpToConfiguredLimit) {
    BlockingCacheConfig cfg;
    cfg.size_bytes = 256;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(1024);
    BlockingCache cache(cfg);

    const auto first = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);

    const auto second = cache.access(memory, 0x80, 4, CacheAccessType::Read);
    EXPECT_FALSE(second.blocked);
    EXPECT_FALSE(second.hit);

    const auto third = cache.access(memory, 0x100, 4, CacheAccessType::Read);
    EXPECT_TRUE(third.blocked);
    EXPECT_EQ(third.latency_cycles, 0);

    drainMiss(cache);

    const auto retry = cache.access(memory, 0x100, 4, CacheAccessType::Read);
    EXPECT_FALSE(retry.blocked);
    EXPECT_FALSE(retry.hit);
}

TEST(BlockingCacheTest, CommitStoreUpdatesCachedDataForLaterLoad) {
    auto memory = std::make_shared<Memory>(4096);
    memory->writeWord(0x120, 0x11111111);

    BlockingCache cache(makeDefaultConfig());

    uint64_t load_value = 0;
    const auto first_load = cache.load(memory, 0x120, 4, load_value);
    EXPECT_FALSE(first_load.blocked);
    EXPECT_FALSE(first_load.hit);
    EXPECT_EQ(load_value, 0x11111111u);
    drainMiss(cache);

    cache.commitStore(memory, 0x120, 4, 0x22222222);

    const auto second_load = cache.load(memory, 0x120, 4, load_value);
    EXPECT_FALSE(second_load.blocked);
    EXPECT_TRUE(second_load.hit);
    EXPECT_EQ(load_value, 0x22222222u);
    EXPECT_EQ(memory->readWord(0x120), 0x22222222u);
}

TEST(BlockingCacheTest, InvalidateRangeRefreshesExternalMemoryWrite) {
    auto memory = std::make_shared<Memory>(4096);
    constexpr uint64_t kAddr = 0x180;
    memory->write64(kAddr, 0x40);

    BlockingCache cache(makeDefaultConfig());

    uint64_t load_value = 0;
    const auto first_load = cache.load(memory, kAddr, 8, load_value);
    EXPECT_FALSE(first_load.blocked);
    EXPECT_FALSE(first_load.hit);
    EXPECT_EQ(load_value, 0x40u);
    drainMiss(cache);

    // 模拟cache外部写回（如tohost syscall处理逻辑）。
    memory->write64(kAddr, 0x30);

    const auto stale_load = cache.load(memory, kAddr, 8, load_value);
    EXPECT_FALSE(stale_load.blocked);
    EXPECT_TRUE(stale_load.hit);
    EXPECT_EQ(load_value, 0x40u);

    cache.invalidateRange(kAddr, 8);
    drainMiss(cache);

    const auto refreshed_load = cache.load(memory, kAddr, 8, load_value);
    EXPECT_FALSE(refreshed_load.blocked);
    EXPECT_FALSE(refreshed_load.hit);
    EXPECT_EQ(load_value, 0x30u);
}

TEST(BlockingCacheTest, FetchInstructionCrossLineReportsSecondHalfMiss) {
    BlockingCacheConfig cfg;
    cfg.size_bytes = 8;
    cfg.line_size_bytes = 2;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;

    auto memory = std::make_shared<Memory>(256);
    memory->writeHalfWord(0x2, 0x0013); // addi x0, x0, 0 (低16位)
    memory->writeHalfWord(0x4, 0x0000); // 高16位

    BlockingCache cache(cfg);
    Instruction instruction = 0;

    const auto first = cache.fetchInstruction(memory, 0x2, instruction);
    EXPECT_TRUE(first.blocked);
    drainMiss(cache);

    const auto second = cache.fetchInstruction(memory, 0x2, instruction);
    EXPECT_FALSE(second.blocked);
    EXPECT_FALSE(second.hit);
    EXPECT_EQ(second.latency_cycles, 21);
    EXPECT_EQ(instruction, 0x00000013u);
}

TEST(BlockingCacheTest, FetchInstructionSameLineMissCanRecoverSecondHalf) {
    BlockingCacheConfig cfg;
    cfg.size_bytes = 32;
    cfg.line_size_bytes = 8;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;

    auto memory = std::make_shared<Memory>(256);
    memory->writeHalfWord(0x2, 0x0013); // addi x0, x0, 0 (低16位)
    memory->writeHalfWord(0x4, 0xABCD); // 高16位，和低16位同属一个cache line

    BlockingCache cache(cfg);
    Instruction instruction = 0;

    const auto first = cache.fetchInstruction(memory, 0x2, instruction);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);
    EXPECT_EQ(first.latency_cycles, 21);
    EXPECT_TRUE(cache.hasMissInFlight());
    EXPECT_EQ(instruction, 0xABCD0013u);
}

TEST(BlockingCacheTest, AccessWrappingAddressSpaceThrowsSimulatorException) {
    auto memory = std::make_shared<Memory>(4096);
    BlockingCache cache(makeDefaultConfig());

    EXPECT_THROW(
        static_cast<void>(cache.access(memory, std::numeric_limits<uint64_t>::max(), 8, CacheAccessType::Write)),
        SimulatorException);
}

TEST(BlockingCacheTest, NextLinePrefetchTurnsFollowingLineIntoUsefulHit) {
    BlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.enable_next_line_prefetch = true;

    auto memory = std::make_shared<Memory>(256);
    memory->writeWord(0x0, 0x11111111);
    memory->writeWord(0x40, 0x22222222);

    BlockingCache cache(cfg);

    const auto first = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);
    drainMiss(cache);

    const auto second = cache.access(memory, 0x40, 4, CacheAccessType::Read);
    EXPECT_FALSE(second.blocked);
    EXPECT_TRUE(second.hit);

    const auto& stats = cache.getStats();
    EXPECT_EQ(stats.prefetch_requests, 1u);
    EXPECT_EQ(stats.prefetch_issued, 1u);
    EXPECT_EQ(stats.prefetch_useful_hits, 1u);
    EXPECT_EQ(stats.prefetch_unused_evictions, 0u);
    EXPECT_EQ(stats.prefetch_dropped_already_resident, 0u);
}

TEST(BlockingCacheTest, NextLinePrefetchDropsRequestWhenTargetAlreadyResident) {
    BlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 256;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.enable_next_line_prefetch = true;

    auto memory = std::make_shared<Memory>(256);
    memory->writeWord(0x40, 0x22222222);
    memory->writeWord(0x80, 0x33333333);

    BlockingCache cache(cfg);

    const auto warm = cache.access(memory, 0x80, 4, CacheAccessType::Read);
    EXPECT_FALSE(warm.blocked);
    EXPECT_FALSE(warm.hit);
    drainMiss(cache);

    cache.resetStats();

    const auto demand = cache.access(memory, 0x40, 4, CacheAccessType::Read);
    EXPECT_FALSE(demand.blocked);
    EXPECT_FALSE(demand.hit);
    drainMiss(cache);

    const auto& stats = cache.getStats();
    EXPECT_EQ(stats.prefetch_requests, 1u);
    EXPECT_EQ(stats.prefetch_issued, 0u);
    EXPECT_EQ(stats.prefetch_useful_hits, 0u);
    EXPECT_EQ(stats.prefetch_unused_evictions, 0u);
    EXPECT_EQ(stats.prefetch_dropped_already_resident, 1u);
}

TEST(BlockingCacheTest, NextLinePrefetchTracksUnusedEviction) {
    BlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.enable_next_line_prefetch = true;

    auto memory = std::make_shared<Memory>(256);
    memory->writeWord(0x0, 0x11111111);
    memory->writeWord(0x40, 0x22222222);
    memory->writeWord(0xC0, 0x44444444);

    BlockingCache cache(cfg);

    const auto first = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);
    drainMiss(cache);

    const auto second = cache.access(memory, 0xC0, 4, CacheAccessType::Read);
    EXPECT_FALSE(second.blocked);
    EXPECT_FALSE(second.hit);
    drainMiss(cache);

    const auto& stats = cache.getStats();
    EXPECT_EQ(stats.prefetch_requests, 2u);
    EXPECT_EQ(stats.prefetch_issued, 1u);
    EXPECT_EQ(stats.prefetch_useful_hits, 0u);
    EXPECT_EQ(stats.prefetch_unused_evictions, 1u);
    EXPECT_EQ(stats.prefetch_dropped_already_resident, 0u);
}

TEST(BlockingCacheTest, NextLinePrefetchIsThrottledWhenSetAlreadyHasUnusedPrefetch) {
    BlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 64 * 3;
    cfg.line_size_bytes = 64;
    cfg.associativity = 3;
    cfg.enable_next_line_prefetch = true;

    auto memory = std::make_shared<Memory>(512);
    memory->writeWord(0x0, 0x11111111);
    memory->writeWord(0x80, 0x33333333);

    BlockingCache cache(cfg);

    const auto first = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);
    drainMiss(cache);

    const auto second = cache.access(memory, 0x80, 4, CacheAccessType::Read);
    EXPECT_FALSE(second.blocked);
    EXPECT_FALSE(second.hit);
    drainMiss(cache);

    const auto& stats = cache.getStats();
    EXPECT_EQ(stats.prefetch_requests, 2u);
    EXPECT_EQ(stats.prefetch_issued, 1u);
    EXPECT_EQ(stats.prefetch_dropped_set_throttle, 1u);
    EXPECT_EQ(stats.prefetch_unused_evictions, 0u);
}

TEST(BlockingCacheTest, UsefulPrefetchHitClearsSetThrottleState) {
    BlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 64 * 3;
    cfg.line_size_bytes = 64;
    cfg.associativity = 3;
    cfg.enable_next_line_prefetch = true;

    auto memory = std::make_shared<Memory>(512);
    memory->writeWord(0x0, 0x11111111);
    memory->writeWord(0x40, 0x22222222);
    memory->writeWord(0x80, 0x33333333);
    memory->writeWord(0xC0, 0x44444444);

    BlockingCache cache(cfg);

    const auto first = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);
    drainMiss(cache);

    const auto useful = cache.access(memory, 0x40, 4, CacheAccessType::Read);
    EXPECT_FALSE(useful.blocked);
    EXPECT_TRUE(useful.hit);

    const auto second = cache.access(memory, 0x80, 4, CacheAccessType::Read);
    EXPECT_FALSE(second.blocked);
    EXPECT_FALSE(second.hit);
    drainMiss(cache);

    const auto& stats = cache.getStats();
    EXPECT_EQ(stats.prefetch_requests, 2u);
    EXPECT_EQ(stats.prefetch_issued, 2u);
    EXPECT_EQ(stats.prefetch_useful_hits, 1u);
    EXPECT_EQ(stats.prefetch_dropped_set_throttle, 0u);
}

} // namespace riscv
