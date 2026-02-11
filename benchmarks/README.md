# Benchmark Workflow（性能探索）

本目录用于统一管理性能探索基准：
- 现有 `riscv-tests/benchmarks` 子项
- `CoreMark`
- `Embench-IoT`

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

## 3.1) 手工导入 Embench-IoT 产物（可选）

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

## 输出格式

`results.csv` / `results.json` 包含字段：
- `suite`
- `benchmark`
- `mode`
- `status`
- `instructions`
- `cycles`
- `ipc`
- `branch_mispredicts`
- `pipeline_stalls`

## 说明

- `CoreMark` 在本工程中用 `mcycle` 作为计时 tick（用于相对趋势比较）。
- 若要做严格可发表的绝对分数，请固定模拟器频率模型与编译参数，并记录完整环境。
