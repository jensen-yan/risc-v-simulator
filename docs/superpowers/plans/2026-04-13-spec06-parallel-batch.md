# SPEC06 Parallel Batch Runner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为现有单切片 checkpoint runner 增加一个可并行执行的 SPEC06 批跑器，并产出批次汇总结果。

**Architecture:** 保持 `risc-v-sim` 单切片执行链路不变，在 `tools/benchmarks/` 新增独立 Python 批跑脚本。脚本负责解析 checkpoint list、解析 `.zstd` 路径、并发调用模拟器、归集各切片结果，并在批次根目录生成 CSV/JSON 汇总与轻量加权指标。

**Tech Stack:** Python 3、`subprocess`、`concurrent.futures`、`json/csv`、现有 `risc-v-sim --checkpoint` CLI

---

### Task 1: 写批跑器测试并锁定输入输出契约

**Files:**
- Create: `tests/test_checkpoint_batch_runner.py`
- Create: `tools/benchmarks/run_checkpoint_batch.py`

- [ ] **Step 1: 写失败测试，覆盖 list 解析、checkpoint 发现、并发汇总**

- [ ] **Step 2: 运行测试，确认当前因脚本缺失而失败**

- [ ] **Step 3: 实现最小脚本骨架与纯函数接口，让测试开始命中真实行为**

- [ ] **Step 4: 重新运行测试，确认解析/汇总行为通过**

### Task 2: 接入真实子进程调度与结果落盘

**Files:**
- Modify: `tools/benchmarks/run_checkpoint_batch.py`
- Test: `tests/test_checkpoint_batch_runner.py`

- [ ] **Step 1: 写失败测试，验证每切片目录包含 command/stdout/stderr 与批次汇总**

- [ ] **Step 2: 运行测试，确认在真实子进程调度前失败**

- [ ] **Step 3: 实现 `ThreadPoolExecutor + subprocess.run` 调度与结果采集**

- [ ] **Step 4: 重新运行测试，确认 fake simulator 场景通过**

### Task 3: 接入 cluster 权重汇总与 CLI 过滤能力

**Files:**
- Modify: `tools/benchmarks/run_checkpoint_batch.py`
- Test: `tests/test_checkpoint_batch_runner.py`

- [ ] **Step 1: 写失败测试，覆盖 `--specific-benchmarks` 与 `cluster_config` 权重回退逻辑**

- [ ] **Step 2: 运行测试，确认当前聚合字段缺失或数值不对**

- [ ] **Step 3: 实现 benchmark 过滤、cluster 权重解析与 `aggregate.json`**

- [ ] **Step 4: 重新运行测试，确认加权聚合通过**

### Task 4: 更新文档并做真实 smoke

**Files:**
- Modify: `README.md`

- [ ] **Step 1: 补 README 的 checkpoint batch 用法示例**

- [ ] **Step 2: 运行 Python 单测**

- [ ] **Step 3: 运行 3-5 个真实 SPEC06 切片 smoke**

- [ ] **Step 4: 如 smoke 通过，再提高并发度做更接近 0.3c 的批量验证**

### Task 5: 提交实现

**Files:**
- Modify: `docs/superpowers/specs/2026-04-13-spec06-parallel-batch-design.md`
- Modify: `docs/superpowers/plans/2026-04-13-spec06-parallel-batch.md`
- Modify: `tools/benchmarks/run_checkpoint_batch.py`
- Modify: `tests/test_checkpoint_batch_runner.py`
- Modify: `README.md`

- [ ] **Step 1: 复核工作区，仅保留本任务相关改动**

- [ ] **Step 2: 提交代码**

- [ ] **Step 3: 记录已跑过的验证命令与真实 smoke 结果**
