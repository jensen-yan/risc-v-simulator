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
- 已新增 `lsu_pair_alias`，用于验证同一 load PC 面对 `safe pair / bad pair` 时的 pair-specific 阻断不会误伤安全路径
- `cpu.memory.load_replays.*`、`loads_forwarded.*`、`cpu.load_profile.top` 已可用于定位 LSU 热点
- `cpu.store_profile.top` 已可把 forwarding / overlap blocking 归因到具体 store PC
- 已验证“源操作数 ready 时提前物化地址”能显著减少 `rob_store_addr_unknown` 型 replay
- `run_memory_learning.sh` 已提供 `lsu-foundation / stream / full` 三种固定入口
- 新增 `lsu_overlap_mix`，专门覆盖 `partial forward` 与 `overlap replay` 两类 LSU 行为
- 已实现“内存值 + store buffer 字节 merge”的部分重叠 load 解析，`lsu_overlap_mix` 上 `store_buffer_overlap replay` 已降到 `0`
- 已开始把“地址和值都 ready 的 store”提前发布到 store buffer，目标是继续压 `rob_store_overlap`
- 已补 `addr_unknown` 轻量推测与恢复骨架：dispatch 时可越过地址未知的更老 store，违例在 store resolve 后全流水恢复
- `lsu_overlap_mix` 上已实际观察到 `loads_speculated_addr_unknown` 与 `order_violation_recoveries`，说明这条链路不再只是静态统计
- 已补齐 `load/store profile` 的 addr-unknown 失败归因，可直接看到“哪个 load 推测失败、哪个 store 触发恢复”
- 已补 `load PC -> store PC` 粒度的坏 pair 阻断，避免一次违例把整个 load PC 全局关死
- 已修正 addr-unknown 决策只看第一条未知 store 的缺口：现在会遍历所有更老未知地址 store，避免坏 pair 被前面的安全 store 掩盖
- 已把坏 pair 阻断前移到 RS->execute 之间，避免明知不能越过的 load 先占一次 LOAD 单元和 dispatch 槽
- 下一步优先看 `rob_store_overlap` 是否已明显转成 forwarding；若边际收益变小，再切到 `L2 / prefetcher`
