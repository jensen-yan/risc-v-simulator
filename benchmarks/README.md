# Benchmark Workflow（性能探索）

本目录用于统一管理性能探索基准：
- 现有 `riscv-tests/benchmarks` 子项
- `CoreMark`
- `Embench-IoT`
- 自定义 `LSU / memory` 微基准

## 目录约定

- `benchmarks/manifest/default.json`：默认基准清单
- `benchmarks/external/`：外部源码（不提交）
- `benchmarks/build/`：外部基准编译产物（不提交）
- `benchmarks/results/`：跑分结果（不提交）

## 1) 拉取外部基准源码

```bash
./tools/benchmarks/fetch_external_benchmarks.sh
```

只拉取 CoreMark:

```bash
./tools/benchmarks/fetch_external_benchmarks.sh --coremark-only
```

## 2) 构建 CoreMark（裸机）

CoreMark 构建复用了本仓库已验证的 `riscv-tests/benchmarks/common` 运行时（`crt.S/syscalls.c/test.ld`）。

```bash
./tools/benchmarks/build_coremark.sh
```

产物默认输出到：
- `benchmarks/build/coremark/coremark.riscv`
- `benchmarks/build/coremark/coremark.riscv.dump`

## 3) 构建 Embench-IoT（推荐）

```bash
./tools/benchmarks/build_embench_iot.sh
```

默认会使用 Embench 官方 `build_all.py`，并把成功产物整理到 `benchmarks/build/embench-iot/*.elf`。

## 3.1) 构建自定义 LSU 微基准

第一阶段新增了一组面向 LSU / memory hierarchy 学习的裸机微基准，包括：
- `lsu_store_forward`
- `lsu_pointer_chase`
- `lsu_stride_walk`
- `lsu_mlp`
- `lsu_overlap_mix`
- `lsu_pair_alias`
- `stream_copy`
- `stream_triad`

构建命令：

```bash
./tools/benchmarks/build_lsu_microbench.sh
```

产物默认输出到：
- `benchmarks/build/lsu/*.riscv`
- `benchmarks/build/lsu/*.riscv.dump`

## 3.2) 手工导入 Embench-IoT 产物（可选）

如果你有自己维护的 Embench 构建流程，可直接导入：

```bash
./tools/benchmarks/import_embench_binaries.sh --src <你的_embench_构建输出目录>
```

## 4) 统一执行并导出统计

```bash
python3 ./tools/benchmarks/run_perf_suite.py \
  --manifest benchmarks/manifest/default.json \
  --cpu-mode both \
  --max-instructions 50000000 \
  --max-ooo-cycles 500000 \
  --output-dir benchmarks/results/latest
```

常用参数：
- `--cpu-mode in-order|ooo|both`
- `--filter "*coremark*"`（按名称过滤）
- `--max-instructions N` / `--max-ooo-cycles N`（0 表示不限）
- `--strict-missing`（缺失必选项时失败）

LSU / memory 学习路线可改用：

```bash
python3 ./tools/benchmarks/run_perf_suite.py \
  --manifest benchmarks/manifest/memory_learning.json \
  --cpu-mode ooo \
  --max-ooo-cycles 300000 \
  --output-dir benchmarks/results/memory-learning
```

如果只是沿 `LSU 微基准 -> STREAM-style kernel -> 轻量 memory benchmark` 这条路线学习，建议直接用：

```bash
./tools/benchmarks/run_memory_learning.sh --phase lsu-foundation
```

如果要固定第一轮 `memory-first` canonical baseline，建议直接用：

```bash
./tools/benchmarks/run_memory_learning.sh --phase baseline
```

如果要在同一源码状态下对 OOO L1D next-line prefetch 做 A/B，对上面的命令追加：

```bash
./tools/benchmarks/run_memory_learning.sh --phase baseline --ooo-l1d-prefetch off
./tools/benchmarks/run_memory_learning.sh --phase baseline --ooo-l1d-prefetch on
```

等价地，也可以在 `run_perf_suite.py` 上直接传：

```bash
python3 tools/benchmarks/run_perf_suite.py ... --ooo-l1d-prefetch off|on
```

这条 baseline 是首轮唯一的 canonical 入口，覆盖：
- `dhrystone`
- `memcpy`
- `mm`
- `spmv`
- `coremark`（若已构建）
- `lsu_store_forward`
- `lsu_stride_walk`
- `lsu_mlp`
- `stream_copy`
- `stream_triad`

其中前 5 项与 `benchmarks/manifest/memory_learning.json` 保持一致，后 5 项是固定追加的代表性 LSU / STREAM 微基准，便于后续做 memory-first 对照分析。

默认结果目录为：
- `benchmarks/results/memory-first-baseline`

只看 STREAM-style kernel：

```bash
./tools/benchmarks/run_memory_learning.sh --phase stream
```

如果要把 `coremark` 也一起纳入 memory 学习集，可改用：

```bash
./tools/benchmarks/run_memory_learning.sh --phase full
```

## 输出格式

`results.csv` / `results.json` 包含字段：
- `suite`
- `benchmark`
- `file`
- `mode`
- `status`
- `return_code`
- `elapsed_sec`
- `instructions`
- `cycles`
- `ipc`
- `branch_mispredicts`
- `pipeline_stalls`
- `stats_path`
- `topdown_executing_pct`
- `topdown_frontend_bound_pct`
- `topdown_backend_bound_pct`
- `issue_slots`
- `issue_utilized_slots`
- `commit_slots`
- `commit_utilized_slots`
- `rob_occupancy_avg`
- `store_buffer_occupancy_avg`
- `l1i_hits`
- `l1i_misses`
- `l1i_stall_cycles`
- `l1d_hits`
- `l1d_misses`
- `l1d_stall_cycles_load`
- `l1d_stall_cycles_store`
- `l1d_prefetch_requests`
- `l1d_prefetch_issued`
- `l1d_prefetch_useful_hits`
- `l1d_prefetch_unused_evictions`
- `l1d_prefetch_dropped_already_resident`
- `load_replays`
- `load_replays_rob_store_addr_unknown`
- `load_replays_rob_store_overlap`
- `load_replays_store_buffer_overlap`
- `loads_blocked_by_store`
- `predictor_control_incorrect`
- `predictor_jalr_mispredicts`
- `branch_profile_top0`
- `jalr_profile_top0`
- `load_profile_top0`
- `store_profile_top0`

## 说明

- `CoreMark` 在本工程中用 `mcycle` 作为计时 tick（用于相对趋势比较）。
- 自定义 LSU 微基准会输出 `=== TEST RESULT: PASS ===`，可直接纳入 `run_perf_suite.py`。
- `run_memory_learning.sh` 会默认先构建 `benchmarks/custom/lsu/*.c`，再执行对应 manifest。
- `stats_path` 指向每个 benchmark/mode 自动落盘的详细 stats 文件；OOO 模式会通过模拟器的 `--stats-file=` 导出 detailed stats，便于后续报告回溯。
- `--ooo-l1d-prefetch auto|on|off` 只影响 OOO L1D next-line prefetcher；`auto` 表示沿用模拟器默认值。
- 若要做严格可发表的绝对分数，请固定模拟器频率模型与编译参数，并记录完整环境。
