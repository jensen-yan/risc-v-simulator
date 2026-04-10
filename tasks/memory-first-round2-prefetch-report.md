# Memory-First Round 2 Prefetch Report

## Scope

本报告对应 round2 的正式分析收口，覆盖：

- task8: 基于已落地的 L1D next-line prefetch 原型完成 workload 回归
- task9: 判断 memory 路线是继续扩展、收敛还是停止
- task10: 给出 `4-issue` 是否升级的 analyze gate
- task11: 给出 BPU 是否升级的 analyze gate

本轮不再改 prefetch 算法本身，只分析当前实现：

- demand L1D miss 触发 next-line prefetch
- 统计字段已接入 `results.json/results.csv`
- `topdown_*_pct` 继续只作为趋势参考，不作为主判断依据

## Method

统一入口沿用 canonical baseline：

```bash
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "<name>" ...
```

对照口径分两类：

1. 有 round1 单项历史结果的 workload：
   - `dhrystone`
   - `coremark`
   - `stream_copy`
   使用 round1 baseline 对比 round2 prefetch-on 结果。
2. round1 未单独归档的 workload：
   - `stream_triad`
   - `lsu_stride_walk`
   - `lsu_mlp`
   - `lsu_store_forward`
   使用同一源码下的 `--ooo-l1d-prefetch off` 作为控制组，对比 round2 prefetch-on 结果。

说明：

- `lsu_stride_walk` 与 `lsu_mlp` 在当前实现下更适合作为 fixed-window stress sample，而不是“快速小测试”。
- 这两条都在 `20000000` cycle 固定窗口中比较 on/off，二者 `status=failed` 仅表示没有自然跑到 PASS marker，不影响窗口内的对照分析。
- 后续若新增用户手写小测试，应优先控制在 `<=10M` 指令量内，保证回归速度。

## Validation Commands

### Prefetch-On Results

```bash
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "dhrystone" --no-build-lsu --max-ooo-cycles 400000 --timeout 30 --ooo-l1d-prefetch on --output-dir benchmarks/results/rlcr-round2-dhrystone
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "coremark" --no-build-lsu --max-ooo-cycles 15000000 --timeout 600 --ooo-l1d-prefetch on --output-dir benchmarks/results/rlcr-round2-coremark
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "stream_copy" --no-build-lsu --max-ooo-cycles 300000 --timeout 30 --ooo-l1d-prefetch on --output-dir benchmarks/results/rlcr-round2-stream-copy
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "stream_triad" --no-build-lsu --max-ooo-cycles 300000 --timeout 30 --ooo-l1d-prefetch on --output-dir benchmarks/results/rlcr-round2-stream-triad
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "lsu_stride_walk" --no-build-lsu --max-ooo-cycles 20000000 --timeout 1200 --ooo-l1d-prefetch on --output-dir benchmarks/results/rlcr-round2-lsu-stride-walk
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "lsu_mlp" --no-build-lsu --max-ooo-cycles 20000000 --timeout 1200 --ooo-l1d-prefetch on --output-dir benchmarks/results/rlcr-round2-lsu-mlp
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "lsu_store_forward" --no-build-lsu --max-ooo-cycles 1500000 --timeout 120 --ooo-l1d-prefetch on --output-dir benchmarks/results/rlcr-round2-lsu-store-forward
```

### Control Results

```bash
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "dhrystone" --no-build-lsu --max-ooo-cycles 400000 --timeout 30 --ooo-l1d-prefetch off --output-dir benchmarks/results/rlcr-round3-control-dhrystone
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "coremark" --no-build-lsu --max-ooo-cycles 15000000 --timeout 600 --ooo-l1d-prefetch off --output-dir benchmarks/results/rlcr-round3-control-coremark
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "stream_copy" --no-build-lsu --max-ooo-cycles 300000 --timeout 30 --ooo-l1d-prefetch off --output-dir benchmarks/results/rlcr-round3-control-stream-copy
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "stream_triad" --no-build-lsu --max-ooo-cycles 300000 --timeout 30 --ooo-l1d-prefetch off --output-dir benchmarks/results/rlcr-round3-control-stream-triad
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "lsu_stride_walk" --no-build-lsu --max-ooo-cycles 20000000 --timeout 1200 --ooo-l1d-prefetch off --output-dir benchmarks/results/rlcr-round3-control-lsu-stride-walk
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "lsu_mlp" --no-build-lsu --max-ooo-cycles 20000000 --timeout 1200 --ooo-l1d-prefetch off --output-dir benchmarks/results/rlcr-round3-control-lsu-mlp
./tools/benchmarks/run_memory_learning.sh --phase baseline --filter "lsu_store_forward" --no-build-lsu --max-ooo-cycles 1500000 --timeout 120 --ooo-l1d-prefetch off --output-dir benchmarks/results/rlcr-round3-control-lsu-store-forward
```

## Delta Summary

### dhrystone

对照来源：round1 baseline

| field | base | new | delta |
|---|---:|---:|---:|
| cycles | 120358 | 120308 | -50 |
| ipc | 1.606524 | 1.607192 | +0.000668 |
| l1d_misses | 37 | 29 | -8 |
| l1d_stall_cycles_load | 476 | 376 | -100 |
| l1d_stall_cycles_store | 653 | 506 | -147 |
| l1d_prefetch_requests | n/a | 29 | n/a |
| l1d_prefetch_issued | n/a | 15 | n/a |
| l1d_prefetch_useful_hits | n/a | 8 | n/a |
| l1d_prefetch_unused_evictions | n/a | 0 | n/a |
| l1d_prefetch_dropped_already_resident | n/a | 14 | n/a |

结论：

- 收益极小，周期只下降了 `50`，更像“轻微帮助”而不是主导收益。
- 分支代价几乎不变：`predictor_control_incorrect=1125`，`predictor_jalr_mispredicts=22`，说明 prefetch 没有改变控制流主瓶颈。
- 该 workload 继续支持“BPU 仍是独立问题，但不是当前主线”的判断。

结果文件：

- [round1 result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round1-dhrystone/results.json)
- [round2 result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-dhrystone/results.json)
- [round2 stats](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-dhrystone/stats/ooo-001-riscv-tests-dhrystone.stats)

### coremark

对照来源：round1 baseline

| field | base | new | delta |
|---|---:|---:|---:|
| cycles | 2703943 | 2703814 | -129 |
| ipc | 1.338688 | 1.338752 | +0.000064 |
| l1d_misses | 90 | 64 | -26 |
| l1d_stall_cycles_load | 1167 | 907 | -260 |
| l1d_stall_cycles_store | 1437 | 992 | -445 |
| l1d_prefetch_requests | n/a | 64 | n/a |
| l1d_prefetch_issued | n/a | 34 | n/a |
| l1d_prefetch_useful_hits | n/a | 26 | n/a |
| l1d_prefetch_unused_evictions | n/a | 0 | n/a |
| l1d_prefetch_dropped_already_resident | n/a | 30 | n/a |

结论：

- 预取确实减少了 L1D miss 和 load/store stall，但整机收益极小，`cycles` 只下降 `129`。
- issue/commit/ROB 基本不变：
  - `issue_utilized_slots: 3914189 -> 3914044`
  - `commit_utilized_slots: 3619736 -> 3619736`
  - `rob_occupancy_avg: 59.305072 -> 59.292687`
- 分支相关字段也基本不变：
  - `predictor_control_incorrect: 24646 -> 24641`
  - `predictor_jalr_mispredicts: 86 -> 86`
- 这说明 `coremark` 当前仍然不是“做了 prefetch 就明显转好”的 workload，`4-issue` 与更深的 BPU 都没有因为本轮结果而被升级。

结果文件：

- [round1 result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round1-coremark-full/results.json)
- [round2 result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-coremark/results.json)
- [round2 stats](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-coremark/stats/ooo-001-coremark-coremark.stats)

### stream_copy

对照来源：round1 baseline

| field | base | new | delta |
|---|---:|---:|---:|
| cycles | 29757 | 29170 | -587 |
| ipc | 1.273717 | 1.299349 | +0.025632 |
| l1d_misses | 88 | 52 | -36 |
| l1d_stall_cycles_load | 286 | 202 | -84 |
| l1d_stall_cycles_store | 1624 | 924 | -700 |
| l1d_prefetch_requests | n/a | 52 | n/a |
| l1d_prefetch_issued | n/a | 40 | n/a |
| l1d_prefetch_useful_hits | n/a | 36 | n/a |
| l1d_prefetch_unused_evictions | n/a | 0 | n/a |
| l1d_prefetch_dropped_already_resident | n/a | 12 | n/a |

结论：

- 这是当前最清楚的正收益样本之一，`cycles` 下降约 `1.97%`。
- `40` 次 issued 预取带来 `36` 次 useful hit，且 `unused_evictions=0`，说明这个原型对顺序流式访问是有效的。
- 这条结果继续支撑“prefetcher 仍然值得保留在 memory 路线里”。

结果文件：

- [round1 result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round1-stream-copy/results.json)
- [round2 result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-stream-copy/results.json)
- [round2 stats](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-stream-copy/stats/ooo-001-custom-lsu-stream_copy.stats)

### stream_triad

对照来源：same-source prefetch off

| field | base | new | delta |
|---|---:|---:|---:|
| cycles | 47946 | 46661 | -1285 |
| ipc | 1.677387 | 1.723581 | +0.046194 |
| l1d_misses | 153 | 84 | -69 |
| l1d_stall_cycles_load | 313 | 210 | -103 |
| l1d_stall_cycles_store | 2903 | 1563 | -1340 |
| l1d_prefetch_requests | 0 | 84 | 84 |
| l1d_prefetch_issued | 0 | 72 | 72 |
| l1d_prefetch_useful_hits | 0 | 69 | 69 |
| l1d_prefetch_unused_evictions | 0 | 0 | 0 |
| l1d_prefetch_dropped_already_resident | 0 | 12 | 12 |

结论：

- 和 `stream_copy` 一样，`stream_triad` 也是清晰的正收益样本，`cycles` 下降约 `2.68%`。
- `69/72` 的 useful/issued 比例很高，且没有 unused eviction。
- 这说明“顺序 next-line”这个假设在 streaming 类 workload 上成立。

结果文件：

- [control result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round3-control-stream-triad/results.json)
- [round2 result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-stream-triad/results.json)

### lsu_stride_walk

对照来源：same-source prefetch off

| field | base | new | delta |
|---|---:|---:|---:|
| cycles | 20000001 | 20000001 | 0 |
| ipc | 0.369980 | 0.671520 | +0.301540 |
| l1d_misses | 906457 | 830150 | -76307 |
| l1d_stall_cycles_load | 18088080 | 16582460 | -1505620 |
| l1d_stall_cycles_store | 41060 | 20540 | -20520 |
| l1d_prefetch_requests | 0 | 830150 | 830150 |
| l1d_prefetch_issued | 0 | 830150 | 830150 |
| l1d_prefetch_useful_hits | 0 | 830147 | 830147 |
| l1d_prefetch_unused_evictions | 0 | 2 | 2 |
| l1d_prefetch_dropped_already_resident | 0 | 0 | 0 |

结论：

- 在 20M 固定窗口里，`stride_walk` 是当前最强的正收益样本。
- 虽然 `cycles` 因窗口固定而不变，但 `ipc` 从 `0.369980` 升到 `0.671520`，L1D load stall 明显下降。
- `830147/830150` 的 useful/issued 比例接近完美，`unused_evictions=2` 几乎可以忽略。
- 这表明当前 prefetcher 对“规则 stride、下一行可预测”的访问模式非常合适。

结果文件：

- [control result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round3-control-lsu-stride-walk/results.json)
- [round2 result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-lsu-stride-walk/results.json)

### lsu_mlp

对照来源：same-source prefetch off

| field | base | new | delta |
|---|---:|---:|---:|
| cycles | 20000001 | 20000001 | 0 |
| ipc | 0.170744 | 0.170786 | +0.000042 |
| l1d_misses | 907657 | 906865 | -792 |
| l1d_stall_cycles_load | 18112080 | 18116740 | +4660 |
| l1d_stall_cycles_store | 41060 | 20560 | -20500 |
| l1d_prefetch_requests | 0 | 906865 | 906865 |
| l1d_prefetch_issued | 0 | 906864 | 906864 |
| l1d_prefetch_useful_hits | 0 | 1025 | 1025 |
| l1d_prefetch_unused_evictions | 0 | 905835 | 905835 |
| l1d_prefetch_dropped_already_resident | 0 | 1 | 1 |

结论：

- 这是当前最明显的负面样本。
- 在 20M 固定窗口里，`ipc` 基本不变，但发出了 `906864` 次预取，只换来 `1025` 次 useful hit，同时产生 `905835` 次 unused eviction。
- 这说明当前 next-line 策略在高 MLP / 弱空间局部性场景中几乎完全失效，已经不能简单称为“普适收益”。

结果文件：

- [control result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round3-control-lsu-mlp/results.json)
- [round2 result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-lsu-mlp/results.json)

### lsu_store_forward

对照来源：same-source prefetch off

| field | base | new | delta |
|---|---:|---:|---:|
| cycles | 1071047 | 1070962 | -85 |
| ipc | 1.871763 | 1.871911 | +0.000148 |
| l1d_misses | 57 | 36 | -21 |
| l1d_stall_cycles_load | 259 | 228 | -31 |
| l1d_stall_cycles_store | 974 | 603 | -371 |
| l1d_prefetch_requests | 0 | 36 | 36 |
| l1d_prefetch_issued | 0 | 24 | 24 |
| l1d_prefetch_useful_hits | 0 | 21 | 21 |
| l1d_prefetch_unused_evictions | 0 | 0 | 0 |
| l1d_prefetch_dropped_already_resident | 0 | 11 | 11 |

结论：

- 有轻微正收益，但幅度很小，不是决定性样本。
- 这条 workload 的主要价值在于说明 prefetch 没明显破坏 forwarding 类微基准。

结果文件：

- [control result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round3-control-lsu-store-forward/results.json)
- [round2 result](/Users/yanyue/workspace/claude-test/claude-first/risc-v-simulator/benchmarks/results/rlcr-round2-lsu-store-forward/results.json)

## Task9 Decision

结论：`收敛`

理由：

- 不能选“继续扩展”，因为 `lsu_mlp` 明确暴露了严重副作用：`906864` 次 issued 预取只产生 `1025` 次 useful hit，却带来 `905835` 次 unused eviction。
- 不能选“停止”，因为 `stream_copy`、`stream_triad`、`lsu_stride_walk` 又给出了非常清晰的正收益，且污染很低。
- 因此最合理的下一步不是盲目增加更复杂的预取器，也不是完全放弃 memory 路线，而是收敛当前 prefetch 策略：
  - 保留 next-line 原型作为已验证的 stride/streaming 方案
  - 下一步优先补最小 gating/throttling，而不是直接扩功能
  - 让策略在明显不适合的 `MLP` 场景尽量少发无效预取

## Task10 Gate

结论：`4-issue` 继续延期，保持 analyze-only

支撑依据：

- `coremark` 的 issue/commit/ROB 几乎不受当前 prefetch 影响：
  - `issue_utilized_slots: 3914189 -> 3914044`
  - `commit_utilized_slots: 3619736 -> 3619736`
  - `rob_occupancy_avg: 59.305072 -> 59.292687`
- `dhrystone` 也没有出现“memory 一解开就明显转成宽度受限”的信号：
  - `issue_utilized_slots: 221580 -> 221494`
  - `commit_utilized_slots: 193358 -> 193358`
- `stream_copy/stream_triad/stride_walk` 的确受益明显，但它们本身就是 memory 模式强相关样本，不能直接外推成“现在最值得上 4-issue”。
- 当前 round2 更像是证明“顺序 prefetch 对 streaming/stride 有价值，但在高 MLP 场景需要收敛”；这仍属于 memory 路线内部问题，没有达到把主线切到宽度扩张的条件。

## Task11 Gate

结论：BPU 继续延期，保持 analyze-only

支撑依据：

- `dhrystone` 的控制流代价基本没变：
  - `predictor_control_incorrect: 1125 -> 1125`
  - `predictor_jalr_mispredicts: 22 -> 22`
- `coremark` 的分支/JALR 代价也几乎不变：
  - `predictor_control_incorrect: 24646 -> 24641`
  - `predictor_jalr_mispredicts: 86 -> 86`
- round2 的 prefetch 结果没有改变 branch/JALR 根因，因此它没有提供“现在应该把 BPU 升级为下一主线”的新证据。
- 在 memory 路线尚未收敛前，把 BPU 拉成下一 coding 主线只会重新引入多主线发散。

## Overall Assessment

本轮 next-line prefetcher 的真实状态不是“全面成功”，也不是“完全失败”，而是：

- 对 `stream_copy`、`stream_triad`、`lsu_stride_walk` 这类顺序/规则访问非常有效
- 对 `dhrystone`、`coremark`、`lsu_store_forward` 只有边际收益
- 对 `lsu_mlp` 明显不合适，副作用非常大

因此，memory 路线当前最合理的下一步不是切去 `4-issue` 或 BPU，而是先在 prefetch 路线内部做收敛。
