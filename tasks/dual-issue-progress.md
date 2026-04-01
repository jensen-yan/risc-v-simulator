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

## 当前状态

- 双发射主链已打通，完整 CoreMark 可自然结束并输出 stats
- 已修复 host-comm MMIO 乱序导致的 DiffTest/CoreMark 收尾回归
- `RS` 双选、窗口扩容已验证有效，当前配置为 `ROB=96`、`RS=48`、`fetch buffer=24`
- CoreMark steady-state IPC: `1.403 -> 1.472`
- 完整 CoreMark IPC: `1.216 -> 1.2474`
- 下一步优先压控制流 flush 和 load replay
