# Prefetch Convergence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不引入 non-blocking L1D 和 4-issue 的前提下，为当前 blocking L1D next-line prefetch 增加最小可验证的 set-level gating，并补齐对应统计与验证链路。

**Architecture:** 当前问题集中在 `BlockingCache::maybeIssueNextLinePrefetch()` 对所有 demand miss 都无条件发起 next-line 预取，导致多流同 set 场景发生大量未使用预取 line 污染。实现采用“每个 set 限制未使用 prefetched line 数量”的局部 gating：如果目标 set 中仍有未被 demand 使用的 prefetched line，就拒绝再向该 set 安装新的 prefetched line，并导出新的 dropped 统计。该方案只修改 cache 层，不穿透 LSU、流水线或 CPU 接口。

**Tech Stack:** C++17、GoogleTest、现有 benchmark 运行脚本、OOO detailed stats 导出

---

### Task 1: 用单元测试锁定 set-level gating 行为

**Files:**
- Modify: `tests/test_blocking_cache.cpp`

- [x] **Step 1: 写失败测试，定义“同一 set 已有未使用预取 line 时，新预取必须被丢弃”**

```cpp
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
```

- [x] **Step 2: 运行单测，确认它先以“缺少统计字段或行为不符”的方式失败**

Run: `ctest --test-dir build --output-on-failure -R BlockingCacheTest.NextLinePrefetchIsThrottledWhenSetAlreadyHasUnusedPrefetch`

Expected:
- 编译或测试失败
- 失败点应是 `prefetch_dropped_set_throttle` 尚不存在，或 `prefetch_issued` 仍为 `2`

- [x] **Step 3: 写第二个失败测试，定义“被 demand 命中的 prefetched line 会解除 set gate”**

```cpp
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
```

- [x] **Step 4: 运行第二个单测，确认它先失败**

Run: `ctest --test-dir build --output-on-failure -R BlockingCacheTest.UsefulPrefetchHitClearsSetThrottleState`

Expected:
- 失败点应是 `prefetch_dropped_set_throttle` 缺失或第二次 prefetch 未按预期重新放行

- [ ] **Step 5: 提交测试阶段**

```bash
git add tests/test_blocking_cache.cpp
git commit -m "test: 补充 L1D prefetch set-level gating 行为测试"
```

### Task 2: 实现最小 gating 与统计导出

**Files:**
- Modify: `include/cpu/ooo/cache/blocking_cache.h`
- Modify: `src/cpu/ooo/cache/blocking_cache.cpp`
- Modify: `src/cpu/ooo/ooo_cpu.cpp`
- Modify: `tools/benchmarks/run_perf_suite.py`
- Modify: `benchmarks/README.md`

- [x] **Step 1: 在 cache stats 中新增 throttle 统计字段，并声明辅助函数**

```cpp
struct BlockingCacheStats {
    uint64_t prefetch_requests = 0;
    uint64_t prefetch_issued = 0;
    uint64_t prefetch_useful_hits = 0;
    uint64_t prefetch_unused_evictions = 0;
    uint64_t prefetch_dropped_already_resident = 0;
    uint64_t prefetch_dropped_set_throttle = 0;
};
```

```cpp
private:
    size_t countUnusedPrefetchedLinesInSet(size_t set_index) const;
```

- [x] **Step 2: 实现“每个 set 最多保留一个未使用 prefetched line”的 gating**

```cpp
size_t BlockingCache::countUnusedPrefetchedLinesInSet(size_t set_index) const {
    size_t count = 0;
    for (const auto& line : sets_[set_index]) {
        if (line.valid && line.prefetched) {
            ++count;
        }
    }
    return count;
}

void BlockingCache::maybeIssueNextLinePrefetch(const std::shared_ptr<Memory>& memory,
                                               uint64_t demand_line_address) {
    if (!config_.enable_next_line_prefetch || !memory) {
        return;
    }

    stats_.prefetch_requests++;
    const uint64_t next_line_address = demand_line_address + 1;
    const uint64_t next_line_base = lineToBaseAddress(next_line_address);
    if (next_line_base >= memory->getSize() || isBypassAccess(memory, next_line_base, 1)) {
        return;
    }
    if (findLine(next_line_address) != nullptr) {
        stats_.prefetch_dropped_already_resident++;
        return;
    }

    const size_t target_set = lineToSetIndex(next_line_address);
    if (countUnusedPrefetchedLinesInSet(target_set) >= 1) {
        stats_.prefetch_dropped_set_throttle++;
        return;
    }

    bool dirty_eviction = false;
    CacheLine& prefetched_line =
        installLine(memory, next_line_address, dirty_eviction, /*mark_prefetched=*/true);
    touchLine(prefetched_line);
    stats_.prefetch_issued++;
}
```

- [x] **Step 3: 导出新统计到 OOO stats 与 benchmark 结果**

```cpp
stats.push_back({"cpu.cache.l1d.prefetch_dropped_set_throttle",
                 prefetch_stats.prefetch_dropped_set_throttle,
                 "Prefetch requests dropped because the target set still holds unused prefetched lines"});
```

```python
"l1d_prefetch_dropped_set_throttle": "cpu.cache.l1d.prefetch_dropped_set_throttle",
```

- [x] **Step 4: 更新 benchmark 文档中的结果字段说明**

```md
- `l1d_prefetch_dropped_set_throttle`
```

- [x] **Step 5: 运行 BlockingCache 聚焦测试，确认 green**

Run: `ctest --test-dir build --output-on-failure -R BlockingCacheTest`

Expected:
- 新增两个测试通过
- 已有 prefetch 相关测试仍通过

- [ ] **Step 6: 提交实现阶段**

```bash
git add include/cpu/ooo/cache/blocking_cache.h \
        src/cpu/ooo/cache/blocking_cache.cpp \
        src/cpu/ooo/ooo_cpu.cpp \
        tools/benchmarks/run_perf_suite.py \
        benchmarks/README.md \
        tests/test_blocking_cache.cpp
git commit -m "feat: 为 L1D next-line prefetch 增加 set-level gating"
```

### Task 3: 运行回归并检查当前轮次是否符合 prefetch 收敛门槛

**Files:**
- Modify: `docs/superpowers/plans/2026-04-10-prefetch-convergence.md`（勾选执行状态）
- Optional Modify: `tasks/memory-first-round2-prefetch-report.md`（仅当需要追加阶段性注记时）

- [x] **Step 1: 跑聚焦单元测试与 OOO cache 测试**

Run: `ctest --test-dir build --output-on-failure -R "BlockingCacheTest|OutOfOrderCacheTest"`

Expected:
- cache 行为测试全部通过
- 新统计不会破坏现有 OOO stats 可观测性

- [x] **Step 2: 跑最小 benchmark A/B，先只覆盖一个正样本和一个负样本**

Run:

```bash
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "stream_copy" --no-build-lsu --max-ooo-cycles 300000 --timeout 30 --ooo-l1d-prefetch on --output-dir benchmarks/results/prefetch-convergence-stream-copy
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "lsu_mlp" --no-build-lsu --max-ooo-cycles 300000 --timeout 120 --ooo-l1d-prefetch on --output-dir benchmarks/results/prefetch-convergence-lsu-mlp
```

Expected:
- `stream_copy` 不出现明显退化
- `lsu_mlp` 的 `l1d_prefetch_dropped_set_throttle` 非零，且 `unused_evictions` 相比旧策略方向性下降

- [x] **Step 3: 若上述方向正确，再补一个轻量综合样本**

Run:

```bash
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "dhrystone" --no-build-lsu --max-ooo-cycles 400000 --timeout 30 --ooo-l1d-prefetch on --output-dir benchmarks/results/prefetch-convergence-dhrystone
```

Expected:
- 不出现明显系统性回退

- [x] **Step 4: 记录本轮结果与残余风险**

```md
- 本轮只完成 set-level gating，不宣称已经完成 prefetch 收敛
- 若 `lsu_mlp` 仍明显失控，则下一步优先补更强 gating / throttling
- 若本轮方向成立，再准备服务器 SPEC06 外环 gate
```

执行结果摘记：

- `stream_copy`（300k cycles 窗口）：
  `on` 相比 `off` 从 `29757 -> 29173` cycles，`ipc 1.273717 -> 1.299215`，正收益保留。
- `lsu_mlp`（300k cycles 窗口）：
  `prefetch_requests=11405`，但只 `issued=1107`，其中 `useful_hits=1103`，`unused_evictions=3`，`dropped_set_throttle=7766`。
  说明 set-level gating 明显压住了原先“几乎发多少浪费多少”的污染模式。
- `dhrystone`（400k cycles 窗口）：
  `on` 相比 `off` 从 `120358 -> 120308` cycles，未出现回退。
- 残余风险：
  `lsu_mlp` 虽然预取质量大幅改善，但 `l1d_stall_cycles_load` 仍未同步转成显著下降，说明下一步仍要看更强 gating 或与 blocking miss 模型相关的限制。

- [ ] **Step 5: 提交验证阶段**

```bash
git add benchmarks/README.md tools/benchmarks/run_perf_suite.py tests/test_blocking_cache.cpp
git commit -m "test: 验证 L1D prefetch set-level gating 的聚焦回归"
```
