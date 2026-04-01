# LSU / Memory 路线

## 目标

- 先补齐 LSU / cache 学习用 workload 矩阵，再做结构优化
- 路线顺序固定为：`LSU 微基准 -> 轻量内存基准 -> L2 / prefetcher -> 4-issue`

## 关键决策

- 先做可解释的裸机微基准，不直接上 SPEC + SimPoint
- 优先覆盖 forwarding、dependent load、stride、MLP、STREAM-style triad
- 复用现有裸机运行时与 perf suite，不新增并行复杂基础设施

## 当前计划

- [x] 建立 LSU / memory 路线文档
- [x] 第一阶段：新增自定义 LSU 微基准与构建脚本
- [x] 第二阶段：接入默认 benchmark manifest 与结果汇总
- [ ] 第三阶段：补 LSU 相关计数器与观测面
- [ ] 第四阶段：实现 L2 / prefetcher
- [ ] 第五阶段：评估是否继续扩到 4-issue

## 当前状态

- 已确定先不继续深挖 BPU
- 现有 benchmark 基础设施可复用 `riscv-tests/benchmarks/common` 与 `tools/benchmarks/run_perf_suite.py`
- 第一批自定义微基准已覆盖：`lsu_store_forward`、`lsu_pointer_chase`、`lsu_stride_walk`、`lsu_mlp`、`stream_copy`、`stream_triad`
- `tools/benchmarks/build_lsu_microbench.sh` 已提供统一构建入口
- `benchmarks/manifest/default.json` 已接入可选 LSU 微基准
- 新增 `benchmarks/manifest/memory_learning.json`，便于单独跑 LSU / memory 学习路线
- 第一批 LSU 计数器已落地：load replay 根因、forwarding 细分、store buffer 占用
- 下一步优先看 `lsu_store_forward / stream_copy / stream_triad` 的新统计，再决定先做 LSU 机制还是转向 L2 / prefetcher
