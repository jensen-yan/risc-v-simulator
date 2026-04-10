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

## Round 3 Same-Source Prefetch A/B Regression

Round 3 在同一源码状态下补齐了 OOO L1D next-line prefetch 的显式 A/B 开关，并用显式 `on/off` 命令重跑或复核了 task8 所需 workload。

本轮关键前提：

- `src/main.cpp` 已支持 `--l1d-next-line-prefetch=on|off`
- `tools/benchmarks/run_perf_suite.py` 与 `tools/benchmarks/run_memory_learning.sh` 已支持 `--ooo-l1d-prefetch auto|on|off`
- 因此本节所有 `off/on` 对比都来自同一源码状态，不再依赖“改代码切换 prefetch”

统一命令模板：

```bash
python3 tools/benchmarks/run_perf_suite.py \
  --manifest benchmarks/manifest/memory_first_baseline.json \
  --simulator build/risc-v-sim \
  --cpu-mode ooo \
  --memory-size 2164260864 \
  --timeout <sec> \
  --max-instructions 0 \
  --max-ooo-cycles <cycles> \
  --output-dir benchmarks/results/<dir> \
  --filter <workload> \
  --ooo-l1d-prefetch off|on
```

结果落点：

- `Dhrystone`: off [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round3-control-dhrystone/results.json)，on [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-dhrystone/results.json)
- `CoreMark`: off [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round3-control-coremark/results.json)，on [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-coremark/results.json)
- `stream_copy`: off [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round3-control-stream-copy/results.json)，on [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-stream-copy-prefetch/results.json)
- `stream_triad`: off [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round3-control-stream-triad/results.json)，on [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-stream-triad/results.json)
- `lsu_store_forward`: off [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round3-control-lsu-store-forward/results.json)，on [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-lsu-store-forward/results.json)
- `lsu_stride_walk`: off [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round3-control-lsu-stride-walk/results.json)，on [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-lsu-stride-walk/results.json)
- `lsu_mlp`: off [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round3-control-lsu-mlp/results.json)，on [results.json](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-lsu-mlp/results.json)

### A/B Summary

| Workload | Off | On | Core Delta | Round 3 Judgement |
|----------|-----|----|------------|-------------------|
| `Dhrystone` | `passed` | `passed` | `cycles 120358 -> 120308 (-0.04%)` | 基本无感，prefetch 不是主杠杆 |
| `CoreMark` | `passed` | `passed` | `cycles 2703943 -> 2703814 (-0.00%)` | 基本无感，prefetch 不是主杠杆 |
| `stream_copy` | `passed` | `passed` | `cycles 29757 -> 29170 (-1.97%)` | 明确正收益 |
| `stream_triad` | `passed` | `passed` | `cycles 47946 -> 46661 (-2.68%)` | 明确正收益 |
| `lsu_store_forward` | `passed` | `passed` | `cycles 1071047 -> 1070962 (-0.01%)` | 几乎无收益 |
| `lsu_stride_walk` | `failed` | `failed` | `cycles` 同为 `6000001`，但 `ipc 0.384775 -> 0.682826` | 同周期上限下显著增吞吐 |
| `lsu_mlp` | `failed` | `failed` | `cycles` 同为 `6000001`，`ipc 0.184618 -> 0.184760` | 基本无收益，且污染明显 |

注意：

- `lsu_stride_walk` 与 `lsu_mlp` 都在 `max_ooo_cycles=6000000` 处触顶，因此比较重点不是绝对 `cycles`，而是同上限下的 `ipc`、已退休工作量与 L1D 行为。
- `topdown_*_pct` 仍不参与本轮 gate；原因见上文“现存盲区与口径风险”。

### Per-Workload A/B Detail

### Dhrystone

- 状态：`passed -> passed`
- `cycles`: `120358 -> 120308`
- `ipc`: `1.606524 -> 1.607192`
- `pipeline_stalls`: `9007 -> 8909`
- `l1d_hits/misses`: `70958/37 -> 70963/29`
- `l1d_stall_cycles_load/store`: `476/653 -> 376/506`
- `l1d_prefetch_requests/issued/useful_hits/unused_evictions/dropped_already_resident`: `0/0/0/0/0 -> 29/15/8/0/14`
- 收益解释：
  next-line prefetch 能覆盖少量顺序 demand miss，`l1d_misses` 下降 `8`，`l1d_stall_cycles_load/store` 也同步下降。
- 副作用解释：
  整体收益极小，`cycles` 只改善 `0.04%`；说明 `Dhrystone` 的主瓶颈仍然是 control + ordering，而不是 L1D 顺序 miss。

### CoreMark

- 状态：`passed -> passed`
- `cycles`: `2703943 -> 2703814`
- `ipc`: `1.338688 -> 1.338752`
- `pipeline_stalls`: `1767528 -> 1767388`
- `l1d_hits/misses`: `689893/90 -> 689931/64`
- `l1d_stall_cycles_load/store`: `1167/1437 -> 907/992`
- `l1d_prefetch_requests/issued/useful_hits/unused_evictions/dropped_already_resident`: `0/0/0/0/0 -> 64/34/26/0/30`
- 收益解释：
  prefetch 的确减少了少量 L1D miss 与 stall，说明 `CoreMark` 中存在少量可预测的顺序访存窗口。
- 副作用解释：
  `cycles` 只改善 `129`，几乎不可见；对 `CoreMark` 的主导矛盾仍然是 dependency / ROB / control，不应把它当成 prefetch 成功样本。

### stream_copy

- 状态：`passed -> passed`
- `cycles`: `29757 -> 29170`
- `ipc`: `1.273717 -> 1.299349`
- `pipeline_stalls`: `27325 -> 25160`
- `l1d_hits/misses`: `9474/88 -> 9513/52`
- `l1d_stall_cycles_load/store`: `286/1624 -> 202/924`
- `l1d_prefetch_requests/issued/useful_hits/unused_evictions/dropped_already_resident`: `0/0/0/0/0 -> 52/40/36/0/12`
- 收益解释：
  这是最干净的正样本之一。顺序流式访问下，prefetch 把 `36` 个后续 demand 命中转化为 useful hit，并显著压低 store-side L1D stall。
- 副作用解释：
  当前没有看到明显污染，`unused_evictions=0`；但收益规模仍是个位百分比，不支持直接升级到更大 memory hierarchy 改造。

### stream_triad

- 状态：`passed -> passed`
- `cycles`: `47946 -> 46661`
- `ipc`: `1.677387 -> 1.723581`
- `pipeline_stalls`: `8183 -> 3557`
- `l1d_hits/misses`: `18080/153 -> 18152/84`
- `l1d_stall_cycles_load/store`: `313/2903 -> 210/1563`
- `l1d_prefetch_requests/issued/useful_hits/unused_evictions/dropped_already_resident`: `0/0/0/0/0 -> 84/72/69/0/12`
- 收益解释：
  `stream_triad` 对 next-line prefetch 更敏感，`useful_hits=69`，而且 `pipeline_stalls` 近乎腰斩，是当前最明确支持 memory 路线继续保留的 workload 之一。
- 副作用解释：
  本轮仍未看到明显污染，但其收益高度依赖规则访问模式，不能外推到一般 memory workload。

### lsu_store_forward

- 状态：`passed -> passed`
- `cycles`: `1071047 -> 1070962`
- `ipc`: `1.871763 -> 1.871911`
- `pipeline_stalls`: `2365 -> 1942`
- `l1d_hits/misses`: `201115/57 -> 201136/36`
- `l1d_stall_cycles_load/store`: `259/974 -> 228/603`
- `l1d_prefetch_requests/issued/useful_hits/unused_evictions/dropped_already_resident`: `0/0/0/0/0 -> 36/24/21/0/11`
- 收益解释：
  统计上存在少量 L1D 行为改善，说明该 workload 尾部或辅助路径里有一小段可预取访问。
- 副作用解释：
  主体性能几乎不变，说明 `lsu_store_forward` 的核心矛盾仍是 forwarding / overlap / tight loop，而不是 cache miss；它不构成扩大 prefetch 覆盖面的证据。

### lsu_stride_walk

- 状态：`failed -> failed`，两侧都在 `max_ooo_cycles=6000000` 截断
- `cycles`: `6000001 -> 6000001`
- `ipc`: `0.384775 -> 0.682826`
- `pipeline_stalls`: `18009141 -> 14286143`
- `l1d_hits/misses`: `30825/270094 -> 277639/246817`
- `l1d_stall_cycles_load/store`: `5360820/41060 -> 4915800/20540`
- `l1d_prefetch_requests/issued/useful_hits/unused_evictions/dropped_already_resident`: `0/0/0/0/0 -> 246817/246817/246814/2/0`
- 收益解释：
  这是 strongest positive sample。虽然 `cycles` 因 cap 相同而不可比，但同周期窗口内 `ipc` 提升约 `77%`，且几乎所有预取都转化为 useful hit，说明规则 stride 访问能显著受益。
- 副作用解释：
  即便收益很大，该 workload 仍然没有在当前上限内跑完，说明 next-line prefetch 并没有从根本上消除大规模 memory latency，只是把规则场景做得更好。

### lsu_mlp

- 状态：`failed -> failed`，两侧都在 `max_ooo_cycles=6000000` 截断
- `cycles`: `6000001 -> 6000001`
- `ipc`: `0.184618 -> 0.184760`
- `pipeline_stalls`: `20986234 -> 20994088`
- `l1d_hits/misses`: `30828/271294 -> 31853/270501`
- `l1d_stall_cycles_load/store`: `5384820/41060 -> 5389460/20560`
- `l1d_prefetch_requests/issued/useful_hits/unused_evictions/dropped_already_resident`: `0/0/0/0/0 -> 270501/270500/1025/269471/1`
- 收益解释：
  只有极小的 `ipc` 改善，说明 next-line prefetch 在高 MLP/弱空间局部性的场景下基本不提供真实帮助。
- 副作用解释：
  这是 strongest negative sample：`prefetch_issued=270500`，但 `useful_hits` 只有 `1025`，同时 `unused_evictions=269471`，说明污染极重，不适合按“默认更激进”方向继续扩展。

## Task 9 Decision

Round 3 对 memory 路线的正式结论是：`收敛`。

原因：

1. 不应 `停止`
   - `stream_copy`、`stream_triad`、`lsu_stride_walk` 都给出了真实而可解释的正收益
   - 特别是 `lsu_stride_walk` 在同周期上限下显示出明显吞吐提升，说明“规则访存可被 next-line prefetch 覆盖”这一方向成立
2. 也不应直接 `继续扩展`
   - `Dhrystone`、`CoreMark`、`lsu_store_forward` 的收益都接近于零
   - `lsu_mlp` 出现了极重污染：`prefetch_unused_evictions=269471`
3. 因此更合理的结论不是“更大规模地加 memory 结构”，而是：
   - 保留 next-line prefetch 作为已验证的顺序/规则访存优化
   - 暂不把它外推成更激进、更泛化的预取策略
   - 也暂不据此升级到 `L2` 或更复杂的 memory hierarchy 改造

## Task 10 Gate: 4-Issue

结论：`4-issue` 继续延期，不升级为下一阶段 coding 主线。

依据：

- memory-bound workload 远未稳定打满当前 2-issue 宽度
  - `lsu_stride_walk`：`issue_utilized_slots / issue_slots = 19.24% -> 34.14%`
  - `lsu_mlp`：`9.24% -> 9.25%`
  - `stream_copy`：`67.60% -> 68.88%`
- 这些 workload 同时具有很高的 ROB 占用
  - `rob_occupancy_avg`: `stream_copy 83.63 -> 83.26`
  - `lsu_store_forward 95.72 -> 95.70`
  - `lsu_stride_walk 95.99 -> 95.97`
  - `lsu_mlp 95.98 -> 95.97`
- 这说明关键矛盾是“窗口里堆满了等待 memory / replay / latency 的工作”，不是“当前宽度不够发”
- 即使在 `CoreMark` 上，`issue_util` 也只有约 `72.38%`，而 `commit_util` 约 `66.94%`，仍不足以支持“先扩宽再看”的判断

因此：

- `4-issue` 继续保持 analyze-only
- 在 memory 路线没有更稳的普适收益之前，不进入实现阶段

## Task 11 Gate: BPU

结论：BPU 深化继续延期，不升级为下一阶段主线。

依据：

- 当前 memory 关键 workload 的 branch 代价并不主导
  - `stream_copy`: `branch_mispredicts 176 -> 172`
  - `stream_triad`: `174 -> 172`
  - `lsu_stride_walk`: `61 -> 71`
  - `lsu_mlp`: `103 -> 103`
- control-heavy workload 的确存在明显 branch/JALR 代价
  - `Dhrystone`: `branch_mispredicts=1103`，`predictor_control_incorrect=1125`，`predictor_jalr_mispredicts=22`
  - `CoreMark`: `branch_mispredicts=24560`，`predictor_control_incorrect=24646`，`predictor_jalr_mispredicts=86`
- 但本轮 A/B 数据同时表明：
  - `Dhrystone`、`CoreMark` 对 prefetch 几乎无感
  - branch/JALR 指标在 `off/on` 间几乎不变
  - 当前对下一阶段影响更大的，仍然是 memory workload 的 latency / miss / pollution 行为，而不是 BPU 参数空间

因此：

- BPU 深化仍然保留为后续 analyze 方向
- 只有当 memory 路线进一步稳定，且 `Dhrystone` / `CoreMark` / 控制流密集 workload 仍显示显著 control penalty 时，再单独拉起

## Round 3 Outcome

Round 3 的最终判断如下：

1. next-line L1D prefetch 已经通过同源码 `off/on` A/B 验证，不再是“只在单边结果里看起来有效”的原型
2. memory 路线当前最合理的状态是 `收敛`
3. `4-issue` 继续延期
4. BPU 深化继续延期

也就是说，本轮不是把 memory-first 扩大成“继续堆结构”的理由，而是把它收口成：

- 顺序/规则访存上，next-line prefetch 是成立的
- 高 MLP / 弱局部性场景下，它存在明显局限和污染
- 后续若要继续做 memory 方向，应该优先考虑更有选择性的策略，而不是直接并行打开 `L2`、`4-issue` 与 BPU 多条主线
