#include <gtest/gtest.h>

#include "cpu/ooo/cache/blocking_cache.h"

namespace riscv {

namespace {

void drainMiss(BlockingCache& cache) {
    while (cache.hasMissInFlight()) {
        cache.tick();
    }
}

} // namespace

TEST(BlockingCacheTest, ColdMissThenHit) {
    BlockingCacheConfig cfg;
    cfg.size_bytes = 32 * 1024;
    cfg.line_size_bytes = 64;
    cfg.associativity = 4;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;

    BlockingCache cache(cfg);

    const auto first = cache.access(0x100, 4, CacheAccessType::Read);
    EXPECT_FALSE(first.blocked);
    EXPECT_FALSE(first.hit);
    EXPECT_EQ(first.latency_cycles, 21);

    drainMiss(cache);

    const auto second = cache.access(0x100, 4, CacheAccessType::Read);
    EXPECT_FALSE(second.blocked);
    EXPECT_TRUE(second.hit);
    EXPECT_EQ(second.latency_cycles, 1);
}

TEST(BlockingCacheTest, DirtyEvictionOnConflictReplacement) {
    BlockingCacheConfig cfg;
    cfg.size_bytes = 64;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;

    BlockingCache cache(cfg);

    const auto write_first = cache.access(0x0, 4, CacheAccessType::Write);
    EXPECT_FALSE(write_first.hit);
    EXPECT_FALSE(write_first.dirty_eviction);
    drainMiss(cache);

    const auto write_second = cache.access(0x40, 4, CacheAccessType::Write);
    EXPECT_FALSE(write_second.blocked);
    EXPECT_FALSE(write_second.hit);
    EXPECT_TRUE(write_second.dirty_eviction);
}

TEST(BlockingCacheTest, AccessIsBlockedWhileMissInFlight) {
    BlockingCacheConfig cfg;
    cfg.size_bytes = 128;
    cfg.line_size_bytes = 64;
    cfg.associativity = 1;
    cfg.hit_latency = 1;
    cfg.miss_penalty = 20;

    BlockingCache cache(cfg);

    const auto miss = cache.access(0x0, 4, CacheAccessType::Read);
    EXPECT_FALSE(miss.blocked);
    EXPECT_FALSE(miss.hit);

    const auto blocked = cache.access(0x80, 4, CacheAccessType::Read);
    EXPECT_TRUE(blocked.blocked);
    EXPECT_EQ(blocked.latency_cycles, 0);

    drainMiss(cache);

    const auto next = cache.access(0x80, 4, CacheAccessType::Read);
    EXPECT_FALSE(next.blocked);
    EXPECT_FALSE(next.hit);
}

} // namespace riscv

