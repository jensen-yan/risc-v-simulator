# Memory-First Round 1 Audit Report

## Scope

本报告对应 RLCR Round 1 的主线收口：

- 固化首轮 canonical `memory-first` baseline
- 让 benchmark runner 为每条 record 落盘 `stats_path`
- 基于现有 OOO detailed stats 做一版 observability audit
- 用 `CoreMark`、`Dhrystone`、`stream_copy` 三类 workload 给出第一轮瓶颈解释与阶段排序

本轮不实现 `L2`、`prefetcher`、`4-issue` 或更深的 BPU，仅做 baseline 与观测面闭环。

## Canonical Baseline

首轮 canonical baseline 为 [benchmarks/manifest/memory_first_baseline.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/manifest/memory_first_baseline.json)。

它由两部分组成：

1. 直接继承 `memory_learning.json` 的基础学习集：
   - `dhrystone`
   - `memcpy`
   - `mm`
   - `spmv`
   - `coremark`
2. 固定追加的代表性 memory 微基准：
   - `lsu_store_forward`
   - `lsu_stride_walk`
   - `lsu_mlp`
   - `stream_copy`
   - `stream_triad`

统一入口：

```bash
./tools/benchmarks/run_memory_learning.sh --phase baseline --output-dir <dir>
```

## Validation Runs

本轮用于报告的代表 workload 与命令如下。

### Dhrystone

```bash
./tools/benchmarks/run_memory_learning.sh \
  --phase baseline \
  --filter "dhrystone" \
  --no-build-lsu \
  --max-ooo-cycles 300000 \
  --timeout 30 \
  --output-dir benchmarks/results/rlcr-round1-dhrystone
```

结果文件：

- [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round1-dhrystone/results.json)
- [stats](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round1-dhrystone/stats/ooo-001-riscv-tests-dhrystone.stats)

关键摘要：

- `status=passed`
- `instructions=193358`
- `cycles=120358`
- `ipc=1.606524`
- `branch_mispredicts=1103`
- `pipeline_stalls=9007`

### CoreMark

```bash
./tools/benchmarks/run_memory_learning.sh \
  --phase baseline \
  --filter "coremark" \
  --no-build-lsu \
  --max-ooo-cycles 15000000 \
  --timeout 600 \
  --output-dir benchmarks/results/rlcr-round1-coremark-full
```

结果文件：

- [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round1-coremark-full/results.json)
- [stats](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round1-coremark-full/stats/ooo-001-coremark-coremark.stats)

关键摘要：

- `status=passed`
- `instructions=3619736`
- `cycles=2703943`
- `ipc=1.338688`
- `branch_mispredicts=24560`
- `pipeline_stalls=1767528`

### stream_copy

```bash
./tools/benchmarks/run_memory_learning.sh \
  --phase baseline \
  --filter "stream_copy" \
  --no-build-lsu \
  --max-ooo-cycles 300000 \
  --timeout 30 \
  --output-dir benchmarks/results/rlcr-round1-stream-copy
```

结果文件：

- [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round1-stream-copy/results.json)
- [stats](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round1-stream-copy/stats/ooo-001-custom-lsu-stream_copy.stats)

关键摘要：

- `status=passed`
- `instructions=37902`
- `cycles=29757`
- `ipc=1.273717`
- `branch_mispredicts=176`
- `pipeline_stalls=27325`

## Observability Audit

结论先说：当前仓库的 OOO observability 并不缺原始计数器，真正缺的是把 `dumpDetailedStats()` 里的高价值派生字段稳定接到 benchmark 输出中。这个接线本轮已经通过 `stats_path + structured export` 补上了一层，但仍有少数统计口径风险需要标红。

### Bucket 1: 前端 / 分支

当前已经具备：

- `cpu.stall.fetch_buffer_full`
- `cpu.stall.execute_frontend_starved`
- `cpu.cache.l1i.{hits,misses,stall_cycles}`
- `cpu.predictor.control_incorrect`
- `cpu.predictor.jalr_mispredicts`
- `cpu.branch_profile.top[*]`
- `cpu.jalr_profile.top[*]`

这意味着本轮已经能区分：

- 取指缓存迟滞
- fetch buffer 背压
- execute 侧拿不到可发射工作
- 条件分支 / `jalr` 的错误预测热点

### Bucket 2: issue / commit / ROB

当前已经具备：

- `cpu.issue.{slots,utilized_slots,issued}`
- `cpu.execute.{dispatch_slots,dispatch_utilized_slots,dispatched}`
- `cpu.commit.{slots,utilized_slots,retired}`
- `cpu.rob.occupancy_avg`
- `cpu.store_buffer.occupancy_avg`
- `cpu.stall.decode_rob_full`
- `cpu.stall.execute_dependency_blocked`
- `cpu.stall.execute_resource_blocked`

这意味着本轮已经能区分：

- 宽度是否被真正用起来
- ROB 是否长期接近饱和
- 停顿更接近依赖链、资源争用还是前端供给不足

### Bucket 3: memory / cache / LSU

当前已经具备：

- `cpu.cache.l1d.{hits,misses,stall_cycles_load,stall_cycles_store}`
- `cpu.memory.load_replays.*`
- `cpu.loads_blocked_by_store`
- `cpu.load_profile.top[*]`
- `cpu.store_profile.top[*]`
- `cpu.store_buffer.occupancy_avg`

这意味着本轮已经能区分：

- L1D miss 是 load 侧还是 store 侧更痛
- replay 是 host comm、ROB older-store overlap、地址未知还是 store buffer overlap
- 热点 load/store 的 forwarding、blocking、order violation 模式

### Bucket 4: 现存盲区与口径风险

当前仍需注意这些问题：

1. `cpu.topdown.cycles.*` 不能当严格周期语义使用。
   - [src/cpu/ooo/ooo_cpu.cpp](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/src/cpu/ooo/ooo_cpu.cpp#L448) 把 `cpu.topdown.cycles.executing` 直接设成 `cpu.execute.dispatched`
   - 但 [tests/test_ooo_cpu.cpp](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/tests/test_ooo_cpu.cpp#L461) 明确验证过 execute 一拍可发两条
   - 因此 `executing_pct` 大于 `100%` 不是 benchmark 异常，而是当前 topdown-lite 的 slot/cycle 口径混用
2. 目前缺少显式的 MLP、平均 miss latency、branch redirect penalty 周期等直接派生指标。
   - 这些并不阻止首轮 memory-first 判断
   - 但会限制后续对 `L2` 和更深 BPU 的精确收益拆分
3. 当前高价值 profile 已导出到 runner，但仍是文本行。
   - 已可用于报告和回归
   - 后续若要做自动排名或 regression gate，最好再做一层结构化解析

## Workload Findings

### Dhrystone

主结论：`Dhrystone` 当前不是典型的 cache miss 主导，更像是控制流和 store-load ordering 行为夹杂的轻量前端压力。

证据：

- L1 miss 很低：`l1i_misses=69`，`l1d_misses=37`
- 但 `execute_frontend_starved=3927`，高于 `execute_dependency_blocked=775` 和 `execute_resource_blocked=970`
- `predictor_control_incorrect=1125`，与 `branch_mispredicts=1103` 同向
- `load_replays=513` 中 `rob_store_overlap=499`
- `load_profile_top0` 与 `store_profile_top0` 都指向同一类 store-forward / overlap 模式

解释：

- 这个 workload 的主要问题不是“缺 L2”
- 也不是“必须先做 4 发射”
- 更像是控制流热点和较固定的 store-load 交错模式共同塑造了瓶颈

### CoreMark

主结论：`CoreMark` 当前主要体现为依赖链与 ROB 压力，伴随明显的 control miss 和 older-store overlap replay；L1 miss 本身并不重。

证据：

- `decode_rob_full=405796`
- `execute_dependency_blocked=388861`
- `fetch_buffer_full=323487`
- `rob_occupancy_avg=59.305072`
- `branch_mispredicts=24560`
- `predictor_control_incorrect=24646`
- `load_replays=3687`，其中 `rob_store_overlap=3649`
- L1 miss 很低：`l1i_misses=133`，`l1d_misses=90`

解释：

- `CoreMark` 并没有给出“现阶段最该补 L2”的证据
- 但它明确说明现有机器存在真实的 control 和 dependency 压力
- 由于 `memory-first` 路线尚未完成，而且当前缺少更直接的 MLP / miss-latency 派生统计，本轮仍不应把 `4-issue` 升级成 coding 任务

### stream_copy

主结论：`stream_copy` 是当前最直接支持 memory-first 的 workload，表现出高 ROB 占用、明显 dependency stall，以及 store 侧 L1D stall。

证据：

- `decode_rob_full=8163`
- `execute_dependency_blocked=8291`
- `fetch_buffer_full=7944`
- `rob_occupancy_avg=83.627180`
- `l1d_misses=88`
- `l1d_stall_cycles_store=1624`，显著高于 `l1d_stall_cycles_load=286`
- `load_replays=14` 很低，说明主要矛盾不是 replay 风暴

解释：

- 这类顺序 streaming 访存更适合先试最小 prefetcher，而不是直接引入新的层级缓存
- 因为它能更直接验证“顺序 miss 能否被提前覆盖”，且实现面更小、归因更清晰

## Priority Ranking

本轮阶段排序如下：

1. `prefetcher`
2. `L2`
3. `4-issue`
4. 更深的 `BPU`

理由如下。

### 1. prefetcher 优先

- 满足 `memory-first` 路线
- 可以直接挂在现有 `BlockingCache` / L1D 路径上
- 对 `stream_copy`、`stream_triad`、`lsu_stride_walk` 这类顺序或规则访存工作负载更容易形成可解释收益
- 比引入完整 `L2` 层级更小、更容易回退

建议的最小原型仍然是：

- `L1D demand miss` 触发的 next-line prefetcher

### 2. L2 暂列第二

- 当前 `CoreMark` 与 `Dhrystone` 的 L1 miss 数量都不高
- 现有证据还不足以说明“多加一级 cache”会比最小 prefetcher 更划算
- `L2` 改动面对时序、替换、统计和验证面的侵入更大

### 3. 4-issue 继续 analyze-only

- `CoreMark` 的确存在 `ROB` 满与 dependency stall
- 但这并不自动推出“扩宽一定更值”
- 本轮仍未完成 memory 主线，也还没有把 issue/dispatch/commit 利用率做成正式 workload 对比结论
- 同时 `topdown` 百分比存在口径风险，不适合作为扩大宽度的直接论据

### 4. 更深 BPU 继续靠后

- `Dhrystone` 与 `CoreMark` 都有明显 control miss
- 但当前更缺的是一轮完整的 memory 原型验证与 structured counter mapping
- 在 memory 主线还没收口前，把 BPU 拉成下一 coding 主线会导致归因重新发散

## Stop / Go For Next Round

结论：`GO`, 但只允许进入 memory 最小原型，不允许进入 `4-issue` coding。

下一轮建议：

1. 以 next-line `L1D` prefetcher 作为唯一 memory 原型
2. 同步补最小统计：
   - `prefetch_requests`
   - `prefetch_issued`
   - `prefetch_useful_hits`
   - `prefetch_unused_evictions`
   - `prefetch_dropped_already_resident`
3. 回归 workload 至少覆盖：
   - `CoreMark`
   - `Dhrystone`
   - `stream_copy`
   - `stream_triad`
   - `lsu_stride_walk`
   - `lsu_mlp`
   - `lsu_store_forward`

## Round 1 Outcome

Round 1 已满足本轮主线的两个最小目标：

- AC-1: baseline 与 `stats_path` 闭环已经建立
- AC-2: observability audit 已有统一落点，并能对三类 workload 给出第一轮解释

仍未进入本轮范围的内容：

- 新增 counter 的机制扩展
- prefetcher / `L2` 原型实现
- `4-issue` coding
- 更深 BPU 设计
