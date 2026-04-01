# Memory / Midcore 进度

## 目标

- 先把 `LSU 微基准 -> STREAM-style kernel -> 轻量 memory benchmark` 跑顺
- 在此基础上再做 `LSU 机制 -> L2 / prefetcher -> 4-issue`

## 关键决策

- 先补 workload 和观测面，不直接跳到 `L2` 或 `4-issue`
- 优先盯 `load/store replay`、`forwarding`、`MLP`、`stride/stream` 这四类行为
- 每一阶段都保留一个固定入口脚本，避免后续切换方向时丢失可复现基线

## 当前计划

- [x] 第一阶段：自定义 LSU 微基准 + STREAM-style kernel
- [x] 第二阶段：补第一批 LSU replay / forwarding 统计
- [x] 第三阶段：补 committed load hotspot profile
- [x] 第四阶段：固化 memory 学习路线的运行入口
- [ ] 第五阶段：继续补 store / overlap 画像与 LSU 机制优化
- [ ] 第六阶段：实现 L2 / prefetcher
- [ ] 第七阶段：评估是否扩到 4-issue

## 当前状态

- 现有自定义基准已覆盖：`lsu_store_forward`、`lsu_pointer_chase`、`lsu_stride_walk`、`lsu_mlp`、`stream_copy`、`stream_triad`、`lsu_overlap_mix`
- `cpu.memory.load_replays.*`、`loads_forwarded.*`、`cpu.load_profile.top` 已可用于定位 LSU 热点
- `cpu.store_profile.top` 已可把 forwarding / overlap blocking 归因到具体 store PC
- 已验证“源操作数 ready 时提前物化地址”能显著减少 `rob_store_addr_unknown` 型 replay
- `run_memory_learning.sh` 已提供 `lsu-foundation / stream / full` 三种固定入口
- 新增 `lsu_overlap_mix`，专门覆盖 `partial forward` 与 `overlap replay` 两类 LSU 行为
- 下一步优先看 `lsu_overlap_mix / stream_triad` 的 store hotspot 统计，再决定先补 store overlap 机制还是进入 L2 / prefetcher
