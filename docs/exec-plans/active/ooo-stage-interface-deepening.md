# 深化 OOO Stage Interface

## 背景与目标

本轮 architecture review 发现，当前 OOO 流水线最明显的重构机会不是继续拆小文件，而是把几个浅 Module 变深。第一优先级是收窄 Stage 对 `CPUState` 的访问面，让 Stage 的 Interface 更明确，降低测试和后续 LSU / flush / recovery 重构的成本。

这份 ExecPlan 用来记录本轮 grill 讨论、候选重构点、已确认约束和后续动作。它默认是本地工作文档，除非明确需要共享或交接，否则不要求提交到 git。

## 当前已知信息

- `PipelineStage` 当前 Interface 是 `execute(CPUState&) / flush() / reset() / get_stage_name()`。
- `CPUState` 同时暴露 Memory、Decoder、ROB、RS、rename、StoreBuffer、BranchPredictor、cache、tracer、`ICpuInterface*`、profile map、perf counters 和执行单元数组。
- `tests/test_execute_stage.cpp` 目前只能验证构造、stage name、`flush/reset` 不崩溃，文件注释也指出 `CPUState` 结构复杂，暂时跳过复杂行为测试。
- `ExecuteStage` 和 `CommitStage` 已经包含大量跨领域逻辑，后续 flush/recovery 与 LSU memory-order 重构会受到当前宽 Interface 影响。
- 第一轮落地后，`PipelineStage` 只保留 `get_stage_name()` 观测面，`FetchStage` 改为通过 `FetchStage::Context` 执行，`OutOfOrderCPU::step()` 在调用取指阶段前创建 Fetch context。
- 第二轮继续按相同模式推进 `DecodeStage`，目标是形成 Fetch + Decode 两个相邻阶段的稳定样板，再进入更复杂的 Execute。
- 第三轮先对 `ExecuteStage` 做入口收口：新增 `ExecuteStage::Context`，让顶层调度/no-ready 路径先通过 Context；LSU/recovery 相关宽访问先显式标为 legacy internals，后续再拆深模块。
- 第四轮开始纵向深化 memory-order：新增 `ExecuteMemoryOrder` Module，先集中 addr-unknown store snapshot、bad pair block 和 memory-order violation recovery。

## 候选重构点

### 1. 收窄 OOO Stage 对 `CPUState` 的 Interface

状态：本轮优先 grill。

问题：`execute(CPUState&)` 让每个 Stage 能直接接触几乎整台 OOO CPU，Stage Module 的 Depth 很低，测试需要组装过多 Implementation 细节。

方向：先不推翻现有流水线，用薄 Adapter 或 stage-specific context 包住 `CPUState`，让 Fetch / Decode / Issue / Execute / Writeback / Commit 只看到自己需要的动作和数据。

预期收益：提升 Stage 测试的 Leverage 和维护 Locality，为后续 recovery 与 LSU 收口创造更窄的 Seam。

### 2. 收敛 OOO flush / recovery 规则

状态：已记录，作为候选 1 之后的自然后续。

问题：异常 flush、commit 后 flush、early control recovery、memory order violation recovery 分散在 `OutOfOrderCPU`、`ExecuteStage`、`CommitStage` 中，清理队列、ROB、RS、rename、StoreBuffer、cache、执行单元和 predictor 的规则重复且容易漂移。

方向：把 recovery / flush 规则集中到一个深 Module，各触发点只描述 flush reason、restart PC 和 younger work 范围。

预期收益：提高 correctness Locality，减少未来 flush 类 bug 的修复面。

### 3. 收口 LSU / memory-order 逻辑

状态：已记录，建议在候选 1 和 2 稳定后推进。

问题：load/store hazard、forwarding、addr-unknown speculation、replay、order violation recovery 和 profile 归因横跨 `ExecuteStage`、`ReorderBuffer`、`StoreBuffer`、`DynamicInst::MemoryInfo`。

方向：形成 `LSUMemoryOrder` 一类的深 Module，Stage 只通过高层动作和结果与它交互。

预期收益：支撑后续 LSU counters、L2 / prefetcher、4-issue 路线，提升 memory-order 规则的 Locality。

### 4. 拆薄 `CommitStage` 的退休语义和副作用

状态：已记录。

问题：`CommitStage` 同时负责 store/AMO commit、寄存器退休、CSR、trap、ECALL、MRET、FENCE.I、DiffTest、PipelineTracer、profile 和 flush orchestration。

方向：区分 retire semantics 和 commit effects，让 syscall/trap/privilege/difftest/tracer/profile sink 通过更窄的 Adapter 承接。

预期收益：让 commit 语义测试和副作用测试分开，降低改 trap/syscall/predictor profile 时互相影响的风险。

### 5. 拆窄 `ICpuInterface`

状态：已记录。

问题：`ICpuInterface` 同时承载执行控制、架构态访问、DiffTest、stats、pipeline tracer、retire limit、halt diagnostics，调用者被迫知道整颗 CPU 的形状。

方向：按调用者真实需求拆出更窄的 CPU observation/control Interface，保留顶层 facade 用于装配。

预期收益：SyscallHandler、DiffTest、Simulator 测试都可以 fake 更小 Adapter。

### 6. 分层 diagnostics / stats / guest output

状态：已记录。

问题：代码中仍有 guest output、simulator diagnostics、dump helper、stats rendering 混用，`tasks/logging-rfc.md` 的分层目标尚未完全落地。

方向：把 structured counters/events 和 human renderer / guest console 分开。

预期收益：benchmark pipeline、CI regression gate、HTML/CSV exporter 可以共享同一份结构化数据。

## 假设与开放问题

- 已确认：第一轮以架构收口优先，接受性能和功能无变化。
- 已确认：第一刀先做 `FetchStage`，作为低风险 tracer-bullet。
- 已确认：第一轮先按 Stage 拆 Interface；等后续能力复杂后，再考虑按能力拆，例如 recovery、memory-order、commit effects。
- 已确认：`PipelineStage::flush/reset` 先删除或弱化，不在 Stage Interface 中保留形式化空方法；flush 统一留给后续 recovery Module。
- 已确认：第一轮完成标准为现有测试通过，并新增至少一个 Stage 行为测试，证明新 Interface 能绕开完整 `OutOfOrderCPU` 组装。
- 开放问题：`FetchStage` 的 context 第一版应暴露字段引用，还是方法级动作？倾向先用薄 wrapper 控制访问面，避免过早抽象过深。

## 计划步骤

1. Grill 候选 1：确认目标、约束、第一刀范围和测试策略。
2. 确认第一轮不直接做 1+2+3，只让候选 1 的 Interface 为 2/3 留出自然 Seam。
3. 设计最小可落地的 Stage context / Adapter 形状。
4. 选择一个 Stage 做 tracer-bullet 试切，优先考虑 `ExecuteStage` 或 `FetchStage`。
5. 补对应行为测试，验证新 Interface 能让测试穿过同一个 Seam。
6. 现有 `ctest` 通过后，再判断是否继续推进 flush/recovery 或 LSU memory-order。

## 验证标准

- 新 Interface 不扩大 Stage 的调用面。
- 至少一个 Stage 的行为测试不再依赖完整 `OutOfOrderCPU` 组装。
- 现有 OOO 行为测试与单测通过。
- 后续 flush/recovery、LSU memory-order 可以明确挂到更窄的 Seam 上。

## 进展

- [x] 2026-05-19 完成只读 architecture review，并按优先级记录 6 个候选。
- [x] 2026-05-19 Grill 候选 1，确认第一轮范围：FetchStage、按 Stage 拆、删除 Stage lifecycle 空接口、功能性能不变。
- [x] 2026-05-19 形成第一轮 `FetchStage` Interface 并落地：新增 `FetchStage::Context`，移除共同基类中的 `execute/flush/reset`。
- [x] 2026-05-19 新增 `FetchStageContextTest`，验证 Fetch 行为可通过窄 context 覆盖，不需要完整 `OutOfOrderCPU`。
- [x] 2026-05-19 验证通过：`cmake --build build -j`、`ctest --test-dir build -R "FetchStage|ExecuteStage" --output-on-failure`、`ctest --test-dir build --output-on-failure`，全量 302/302 通过。
- [x] 2026-05-19 第二轮推进 `DecodeStage::Context`，让 Decode 不再直接接收整份 `CPUState&`，并补不依赖完整 `OutOfOrderCPU` 的 Decode 行为测试。
- [x] 2026-05-19 验证通过：`cmake --build build -j`、`ctest --test-dir build -R "DecodeStage|FetchStage" --output-on-failure`、`ctest --test-dir build --output-on-failure`，全量 304/304 通过。
- [x] 2026-05-19 第三轮推进 `ExecuteStage::Context` 入口收口，先覆盖 dispatch/no-ready 路径，并明确 LSU/recovery 仍待深模块化。
- [x] 2026-05-19 验证 ExecuteStage 第三轮：`cmake --build build -j`、`ctest --test-dir build -R "ExecuteStage|OutOfOrderCPUTest" --output-on-failure`、`ctest --test-dir build --output-on-failure`，全量 305/305 通过。
- [x] 2026-05-19 第四轮新增 `ExecuteMemoryOrder` Module，从 `ExecuteStage` 抽出 addr-unknown speculation guard 与 memory-order violation recovery，并新增模块级测试。
- [x] 2026-05-19 验证 ExecuteMemoryOrder 第四轮：`cmake --build build -j`、聚焦 memory-order 相关 `ctest`、`ctest --test-dir build --output-on-failure`，全量 308/308 通过。
- [x] 2026-05-19 创建 `CONTEXT.md`，记录 Stage Context、Execute Memory Order、Addr-Unknown Store、Bad Addr-Unknown Pair 等领域术语。
- [x] 2026-05-19 第五轮继续深化 `ExecuteMemoryOrder`：迁移 load replay reason / replay bucket 归因，减少 `ExecuteStage` 对 memory-order 统计细节的所有权；`cmake --build build -j`、聚焦 ExecuteMemoryOrder/ExecuteStage/memory-order 测试、全量 `ctest` 309/309 通过。
- [x] 2026-05-19 第六轮推进 `IssueStage::Context`，收口发射阶段的 ROB/rename/RS/StoreBuffer 入口，并新增不依赖完整 `OutOfOrderCPU` 的 Issue 行为测试；`cmake --build build -j`、聚焦 Issue/OOO 测试、全量 `ctest` 311/311 通过。
- [x] 2026-05-19 第七轮推进 `CommitStage::Context`，先收口提交阶段执行入口，保留 retire effects / flush / trap / DiffTest 为显式 legacy 内部区，并新增 CommitStage context 行为测试；`cmake --build build -j`、聚焦 Commit/OOO 测试、全量 `ctest` 313/313 通过。
- [x] 2026-05-19 第八轮推进 `WritebackStage::Context`，收口 CDB、RS operand wakeup、rename physical writeback 和 ROB complete 入口，并新增 WritebackStage context 行为测试；`cmake --build build -j`、聚焦 Writeback/OOO 测试、全量 `ctest` 315/315 通过。
- [x] 2026-05-19 收口检查：`rg "void execute\\(CPUState&|execute\\(cpu_state_\\)" include/cpu/ooo src/cpu/ooo tests -n` 无剩余命中；`ARCHITECTURE.md` 已补充 OOO Stage Context 边界说明。
