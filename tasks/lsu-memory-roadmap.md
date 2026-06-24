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
- 第一批 LSU 计数器已落地：load replay 根因、forwarding 细分、store forwarding buffer 占用
- LSU 结构命名开始对齐现代 CPU：`StoreQueue` 记录 store 生命周期，`StoreForwardingBuffer` 只表示 ready-store forwarding view
- 已开始 internal STA/STD split：`StoreAddress` 可先解析地址并写入 `StoreQueue`，`StoreData` 也可先捕获数据；当前仍共享同一条 ROB/RS entry，后续才演进到 true STA/STD 双 uop
- LSU load 侧骨架开始落地：`LoadQueue` 记录 load 分配、地址 ready、issue、replay、完成、提交和 flush 生命周期；当前先对齐结构边界，还不替代 `ExecuteMemoryOrder`
- store address resolve 后的 younger executed load 检查已迁到 LQ/SQ 边界：`StoreQueue` 提供 resolved-store view，`LoadQueue` 查 speculative overlapping executed load，`ExecuteMemoryOrder` 保留训练和恢复入口
- 下一步优先在这个边界上引入更像 StoreSet/MDP 的预测与 replay 策略
