# 双发射进度

## 目标

- 实现 `2-wide fetch/decode/issue/dispatch/commit`
- 第一版保持 `writeback` 不限宽，优先保证正确性

## 关键决策

- 所有 stage 按 bundle 推进，不做零散的定点 `for` 补丁
- 同周期内严格保持程序顺序，尤其是 rename 和 commit
- serializing/control redirect 指令先阻断同周期后续槽，后续再优化
- host-comm MMIO 访问先按 ROB 顺序串行化，优先保证 CoreMark/tohost 握手正确

## 当前计划

- [x] 梳理现有 OOO 单宽约束与风险点
- [x] 确定分阶段实现方案
- [x] 第一阶段：统一宽度配置 + fetch/decode 双宽
- [x] 第二阶段：issue/rename 双宽
- [x] 第三阶段：RS dispatch/execute/commit 双宽
- [x] 第四阶段：优化 RS 双选与窗口容量
- [ ] 第五阶段：继续压控制流 flush 与 load replay
- [x] 第六阶段：补 JALR 分类日志，再决定 RAS / indirect predictor 优先级
- [x] 第七阶段：落 speculative RAS，并验证 return-like JALR 收益
- [x] 第八阶段：为 backward branch 引入轻量 loop predictor 并验证 CoreMark 收益

## 当前状态

- 双发射主链已打通，完整 CoreMark 可自然结束并输出 stats
- 已修复 host-comm MMIO 乱序导致的 DiffTest/CoreMark 收尾回归
- `RS` 双选、窗口扩容已验证有效，当前配置为 `ROB=96`、`RS=48`、`fetch buffer=24`
- 已补上 trap-like SYSTEM/FENCE.I 的 issue 序列化，避免 dual-issue 在 commit 前越过 EBREAK/ECALL 一类边界继续执行 younger 指令
- execute 阶段的条件分支早恢复已打通，rename checkpoint 与 younger-only flush 已接上
- 完整 CoreMark IPC: `1.2474 -> 1.2721`
- `ROB flushed entries.branch_mispredict: 836133 -> 785194`
- 当前条件分支准确率有回落：`560722/22445 -> 549992/33175`
- 已补 JALR 分类统计与热点画像；CoreMark 中 `3705` 次 JALR miss 里有 `3665` 次是 return-like
- JALR 根因也已明确：`fallthrough=46`，`wrong_target=3659`，下一步优先级偏向 RAS，而不是先做通用 indirect predictor
- 已落 speculative RAS：fetch/predict 维护投机返回栈，flush/recover 回到 committed 栈，commit 继续做最终训练
- steady-state CoreMark：`JALR miss 120 -> 3`，`IPC 1.5014 -> 1.5025`
- 200k Dhrystone：`JALR miss 6174 -> 21`，`IPC 1.3839 -> 1.6121`
- 已为 backward conditional branch 增加高置信度 loop predictor，专门覆盖固定 trip-count 循环出口
- steady-state CoreMark：`branch mispredict 2405 -> 2046`，`IPC 1.5025 -> 1.5290`
- 完整 CoreMark：`branch mispredict 33939 -> 24171`，`IPC 1.2757 -> 1.3081`
- 分支侧下一步优先看 loop override 的误用热点，再决定是否需要更细的 enable/disable 条件
