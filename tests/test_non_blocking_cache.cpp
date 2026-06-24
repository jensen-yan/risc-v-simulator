#include <gtest/gtest.h>

#include "core/memory.h"
#include "cpu/ooo/cache/non_blocking_cache.h"
#include "cpu/ooo/memory_timing_backend.h"

#include <limits>
#include <memory>

namespace riscv {

namespace {

void drainMiss(NonBlockingCache& cache) {
    while (cache.hasMissInFlight()) {
        cache.tick();
    }
}

NonBlockingCacheConfig makeDefaultConfig() {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 32 * 1024;
    cfg.line_size_bytes = 64;
    cfg.associativity = 4;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    return cfg;
}

} // namespace

TEST(NonBlockingCacheTest, ColdMissThenHit) {
    auto memory = std::make_shared<Memory>(4096);
    memory->writeWord(0x100, 0xDEADBEEF);

    NonBlockingCache cache(makeDefaultConfig());

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

TEST(NonBlockingCacheTest, InjectedTimingBackendSuppliesMissLatency) {
    auto memory = std::make_shared<Memory>(4096);
    memory->writeWord(0x100, 0xDEADBEEF);

    auto cfg = makeDefaultConfig();
    cfg.miss_penalty = 20;
    auto timing_backend = std::make_shared<FixedLatencyMemoryTimingBackend>(7);
    NonBlockingCache cache(cfg, timing_backend);

    const auto first = cache.access(memory, 0x100, 4, CacheAccessType::Read);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);
    EXPECT_EQ(first.latency_cycles, 8);

    const auto& stats = timing_backend->getStats();
    EXPECT_EQ(stats.read_requests, 1u);
    EXPECT_EQ(stats.total_latency_cycles, 7u);
}

TEST(NonBlockingCacheTest, DirtyEvictionAfterCommittedStore) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 64;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;

    auto memory = std::make_shared<Memory>(1024);
    NonBlockingCache cache(cfg);

    cache.commitStore(memory, 0x0, 4, 0xAABBCCDD);

    const auto replace = cache.access(memory, 0x40, 4, CacheAccessType::Read);
    EXPECT_FALSE(replace.blocked);
    EXPECT_FALSE(replace.hit);
    EXPECT_TRUE(replace.dirty_eviction);
}

TEST(NonBlockingCacheTest, CommitStoreCanReserveLineWhenSetIsFullOfPendingFills) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 2;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(512);
    memory->writeWord(0x0, 0x11111111);
    memory->writeWord(0x40, 0x22222222);
    memory->writeWord(0x80, 0x33333333);

    NonBlockingCache cache(cfg);
    uint64_t value = 0;

    const auto first = cache.load(memory, 0x0, 4, value);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);

    const auto second = cache.load(memory, 0x40, 4, value);
    EXPECT_FALSE(second.blocked);
    EXPECT_FALSE(second.hit);
    EXPECT_EQ(cache.outstandingMissCount(), 2u);

    EXPECT_NO_THROW(cache.commitStore(memory, 0x80, 4, 0xAABBCCDD));
    EXPECT_EQ(memory->readWord(0x80), 0xAABBCCDDu);

    const auto load = cache.load(memory, 0x80, 4, value);
    EXPECT_FALSE(load.blocked);
    EXPECT_TRUE(load.hit);
    EXPECT_EQ(value, 0xAABBCCDDu);
}

TEST(NonBlockingCacheTest, AccessIsBlockedWhileMissInFlight) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;

    auto memory = std::make_shared<Memory>(1024);
    NonBlockingCache cache(cfg);

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

TEST(NonBlockingCacheTest, AccessAllowsMultipleOutstandingMissesUpToConfiguredLimit) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 256;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(1024);
    NonBlockingCache cache(cfg);

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

TEST(NonBlockingCacheTest, HitAccessCanProceedWhileMissIsInFlight) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    cfg.max_outstanding_misses = 1;

    auto memory = std::make_shared<Memory>(1024);
    NonBlockingCache cache(cfg);

    const auto installed = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(installed.blocked);
    EXPECT_FALSE(installed.hit);
    drainMiss(cache);

    const auto miss = cache.access(memory, 0x40, 4, CacheAccessType::Read);
    EXPECT_FALSE(miss.blocked);
    EXPECT_FALSE(miss.hit);

    const auto blocked_hit = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(blocked_hit.blocked);
    EXPECT_TRUE(blocked_hit.hit);

    const auto blocked_miss = cache.access(memory, 0x80, 4, CacheAccessType::Read);
    EXPECT_TRUE(blocked_miss.blocked);
    EXPECT_FALSE(blocked_miss.blocked_hit);
}

TEST(NonBlockingCacheTest, PendingMissLineMergesIntoExistingFillWithoutCountingAsHit) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(1024);
    NonBlockingCache cache(cfg);

    const auto miss = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(miss.blocked);
    EXPECT_FALSE(miss.hit);

    const auto same_line_while_pending = cache.access(memory, 0x4, 4, CacheAccessType::Read);
    EXPECT_FALSE(same_line_while_pending.blocked);
    EXPECT_FALSE(same_line_while_pending.hit);
    EXPECT_TRUE(same_line_while_pending.merged_pending_fill);
    EXPECT_EQ(same_line_while_pending.latency_cycles, miss.latency_cycles);

    drainMiss(cache);

    const auto same_line_after_fill = cache.access(memory, 0x4, 4, CacheAccessType::Read);
    EXPECT_FALSE(same_line_after_fill.blocked);
    EXPECT_TRUE(same_line_after_fill.hit);
}

TEST(NonBlockingCacheTest, PendingFillLineIsNotSelectedAsReplacementVictim) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 64;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(1024);
    NonBlockingCache cache(cfg);

    const auto miss = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(miss.blocked);
    EXPECT_FALSE(miss.hit);

    const auto same_set_miss = cache.access(memory, 0x40, 4, CacheAccessType::Read);
    EXPECT_TRUE(same_set_miss.blocked);
    EXPECT_TRUE(same_set_miss.blocked_by_outstanding_limit);

    drainMiss(cache);

    const auto original_line = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(original_line.blocked);
    EXPECT_TRUE(original_line.hit);
}

TEST(NonBlockingCacheTest, PendingFillReadsUseMshrPayloadUntilLineBecomesReady) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(1024);
    memory->writeWord(0x0, 0x11111111);
    NonBlockingCache cache(cfg);

    uint64_t value = 0;
    const auto miss = cache.load(memory, 0x0, 4, value);
    EXPECT_FALSE(miss.blocked);
    EXPECT_FALSE(miss.hit);
    EXPECT_EQ(value, 0x11111111u);

    memory->writeWord(0x0, 0x22222222);

    const auto merged = cache.load(memory, 0x0, 4, value);
    EXPECT_FALSE(merged.blocked);
    EXPECT_TRUE(merged.merged_pending_fill);
    EXPECT_EQ(value, 0x11111111u);

    drainMiss(cache);

    const auto hit = cache.load(memory, 0x0, 4, value);
    EXPECT_FALSE(hit.blocked);
    EXPECT_TRUE(hit.hit);
    EXPECT_EQ(value, 0x11111111u);
}

TEST(NonBlockingCacheTest, CommitStoreToPendingLineIsNotOverwrittenByFillCompletion) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(1024);
    memory->writeWord(0x0, 0x11111111);
    NonBlockingCache cache(cfg);

    const auto miss = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(miss.blocked);
    EXPECT_FALSE(miss.hit);
    EXPECT_TRUE(cache.hasMissInFlight());

    cache.commitStore(memory, 0x0, 4, 0xAABBCCDD);
    EXPECT_FALSE(cache.hasMissInFlight());

    uint64_t value = 0;
    const auto load = cache.load(memory, 0x0, 4, value);
    EXPECT_FALSE(load.blocked);
    EXPECT_TRUE(load.hit);
    EXPECT_EQ(value, 0xAABBCCDDu);
}

TEST(NonBlockingCacheTest, DirtyVictimWritebackIsDeferredUntilMshrFillCompletes) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 64;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(1024);
    NonBlockingCache cache(cfg);

    cache.commitStore(memory, 0x0, 4, 0xAABBCCDD);
    memory->writeWord(0x0, 0x11111111);

    const auto miss = cache.access(memory, 0x40, 4, CacheAccessType::Read);
    EXPECT_FALSE(miss.blocked);
    EXPECT_FALSE(miss.hit);
    EXPECT_TRUE(miss.dirty_eviction);
    EXPECT_TRUE(cache.hasMissInFlight());
    EXPECT_EQ(memory->readWord(0x0), 0x11111111u);

    drainMiss(cache);

    EXPECT_EQ(memory->readWord(0x0), 0xAABBCCDDu);
}

TEST(NonBlockingCacheTest, CancelPendingFillWritesBackDeferredDirtyVictim) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 64;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(1024);
    NonBlockingCache cache(cfg);

    cache.commitStore(memory, 0x0, 4, 0xAABBCCDD);
    memory->writeWord(0x0, 0x11111111);

    const auto miss = cache.access(memory, 0x40, 4, CacheAccessType::Read);
    EXPECT_FALSE(miss.blocked);
    EXPECT_TRUE(miss.dirty_eviction);
    EXPECT_EQ(memory->readWord(0x0), 0x11111111u);

    cache.invalidateRange(0x40, 4);

    EXPECT_FALSE(cache.hasMissInFlight());
    EXPECT_EQ(memory->readWord(0x0), 0xAABBCCDDu);
}

TEST(NonBlockingCacheTest, CommitStoreUpdatesCachedDataForLaterLoad) {
    auto memory = std::make_shared<Memory>(4096);
    memory->writeWord(0x120, 0x11111111);

    NonBlockingCache cache(makeDefaultConfig());

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

TEST(NonBlockingCacheTest, InvalidateRangeRefreshesExternalMemoryWrite) {
    auto memory = std::make_shared<Memory>(4096);
    constexpr uint64_t kAddr = 0x180;
    memory->write64(kAddr, 0x40);

    NonBlockingCache cache(makeDefaultConfig());

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

TEST(NonBlockingCacheTest, FetchInstructionCrossLineReportsSecondHalfMiss) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 8;
    cfg.line_size_bytes = 2;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;

    auto memory = std::make_shared<Memory>(256);
    memory->writeHalfWord(0x2, 0x0013); // addi x0, x0, 0 (低16位)
    memory->writeHalfWord(0x4, 0x0000); // 高16位

    NonBlockingCache cache(cfg);
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

TEST(NonBlockingCacheTest, FetchInstructionSameLineMissCanRecoverSecondHalf) {
    NonBlockingCacheConfig cfg;
    cfg.size_bytes = 32;
    cfg.line_size_bytes = 8;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;

    auto memory = std::make_shared<Memory>(256);
    memory->writeHalfWord(0x2, 0x0013); // addi x0, x0, 0 (低16位)
    memory->writeHalfWord(0x4, 0xABCD); // 高16位，和低16位同属一个cache line

    NonBlockingCache cache(cfg);
    Instruction instruction = 0;

    const auto first = cache.fetchInstruction(memory, 0x2, instruction);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);
    EXPECT_EQ(first.latency_cycles, 21);
    EXPECT_TRUE(cache.hasMissInFlight());
    EXPECT_EQ(instruction, 0xABCD0013u);
}

TEST(NonBlockingCacheTest, AccessWrappingAddressSpaceThrowsSimulatorException) {
    auto memory = std::make_shared<Memory>(4096);
    NonBlockingCache cache(makeDefaultConfig());

    EXPECT_THROW(
        static_cast<void>(cache.access(memory, std::numeric_limits<uint64_t>::max(), 8, CacheAccessType::Write)),
        SimulatorException);
}

TEST(NonBlockingCacheTest, NextLinePrefetchTurnsFollowingLineIntoUsefulHit) {
    NonBlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.enable_next_line_prefetch = true;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(256);
    memory->writeWord(0x0, 0x11111111);
    memory->writeWord(0x40, 0x22222222);

    NonBlockingCache cache(cfg);

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

TEST(NonBlockingCacheTest, NextLinePrefetchIsTimedAndDemandCanMergeIntoIt) {
    NonBlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.enable_next_line_prefetch = true;
    cfg.max_outstanding_misses = 2;
    cfg.max_outstanding_prefetches = 1;

    auto memory = std::make_shared<Memory>(256);
    memory->writeWord(0x0, 0x11111111);
    memory->writeWord(0x40, 0x22222222);

    NonBlockingCache cache(cfg);

    const auto first = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);
    EXPECT_EQ(cache.outstandingMissCount(), 1u);
    EXPECT_TRUE(cache.hasMissInFlight());

    const auto prefetched_pending = cache.access(memory, 0x40, 4, CacheAccessType::Read);
    EXPECT_FALSE(prefetched_pending.blocked);
    EXPECT_FALSE(prefetched_pending.hit);
    EXPECT_TRUE(prefetched_pending.merged_pending_fill);
    EXPECT_EQ(cache.outstandingMissCount(), 2u);

    drainMiss(cache);

    const auto& stats = cache.getStats();
    EXPECT_EQ(stats.prefetch_requests, 1u);
    EXPECT_EQ(stats.prefetch_issued, 1u);
    EXPECT_EQ(stats.prefetch_useful_hits, 1u);
}

TEST(NonBlockingCacheTest, PendingPrefetchDoesNotOccupyCacheWayUntilFillCompletes) {
    NonBlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 2;
    cfg.enable_next_line_prefetch = true;
    cfg.max_outstanding_misses = 2;
    cfg.max_outstanding_prefetches = 1;

    auto memory = std::make_shared<Memory>(512);
    memory->writeWord(0x0, 0x11111111);
    memory->writeWord(0x40, 0x22222222);
    memory->writeWord(0x80, 0x33333333);

    NonBlockingCache cache(cfg);

    const auto first = cache.access(memory, 0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);
    EXPECT_EQ(cache.getStats().prefetch_issued, 1u);

    // 0x40 是尚未完成的next-line prefetch；它只占MSHR，不应提前占掉同set的另一个cache way。
    const auto demand_same_set = cache.access(memory, 0x80, 4, CacheAccessType::Read);
    EXPECT_FALSE(demand_same_set.blocked);
    EXPECT_FALSE(demand_same_set.hit);
    EXPECT_FALSE(demand_same_set.merged_pending_fill);
    EXPECT_EQ(cache.outstandingMissCount(), 2u);

    const auto& stats = cache.getStats();
    EXPECT_EQ(stats.prefetch_issued, 1u);
    EXPECT_EQ(stats.prefetch_unused_evictions, 0u);
}

TEST(NonBlockingCacheTest, NextLinePrefetchDropsRequestWhenTargetAlreadyResident) {
    NonBlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 256;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.enable_next_line_prefetch = true;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(256);
    memory->writeWord(0x40, 0x22222222);
    memory->writeWord(0x80, 0x33333333);

    NonBlockingCache cache(cfg);

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

TEST(NonBlockingCacheTest, NextLinePrefetchTracksUnusedEviction) {
    NonBlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.enable_next_line_prefetch = true;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(256);
    memory->writeWord(0x0, 0x11111111);
    memory->writeWord(0x40, 0x22222222);
    memory->writeWord(0xC0, 0x44444444);

    NonBlockingCache cache(cfg);

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

TEST(NonBlockingCacheTest, NextLinePrefetchIsThrottledWhenSetAlreadyHasUnusedPrefetch) {
    NonBlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 64 * 3;
    cfg.line_size_bytes = 64;
    cfg.associativity = 3;
    cfg.enable_next_line_prefetch = true;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(512);
    memory->writeWord(0x0, 0x11111111);
    memory->writeWord(0x80, 0x33333333);

    NonBlockingCache cache(cfg);

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

TEST(NonBlockingCacheTest, UsefulPrefetchHitClearsSetThrottleState) {
    NonBlockingCacheConfig cfg = makeDefaultConfig();
    cfg.size_bytes = 64 * 3;
    cfg.line_size_bytes = 64;
    cfg.associativity = 3;
    cfg.enable_next_line_prefetch = true;
    cfg.max_outstanding_misses = 2;

    auto memory = std::make_shared<Memory>(512);
    memory->writeWord(0x0, 0x11111111);
    memory->writeWord(0x40, 0x22222222);
    memory->writeWord(0x80, 0x33333333);
    memory->writeWord(0xC0, 0x44444444);

    NonBlockingCache cache(cfg);

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
