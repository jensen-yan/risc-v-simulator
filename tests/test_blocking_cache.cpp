#include <gtest/gtest.h>

#include "core/memory.h"
#include "cpu/ooo/cache/blocking_cache.h"

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

} // namespace riscv
