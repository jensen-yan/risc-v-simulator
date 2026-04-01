# 双发射进度

## 目标

- 实现 `2-wide fetch/decode/issue/dispatch/commit`
- 第一版保持 `writeback` 不限宽，优先保证正确性

## 关键决策

- 所有 stage 按 bundle 推进，不做零散的定点 `for` 补丁
- 同周期内严格保持程序顺序，尤其是 rename 和 commit
- serializing/control redirect 指令先阻断同周期后续槽，后续再优化

## 当前计划

- [x] 梳理现有 OOO 单宽约束与风险点
- [x] 确定分阶段实现方案
- [ ] 第一阶段：统一宽度配置 + fetch/decode 双宽
- [ ] 第二阶段：issue/rename 双宽
- [ ] 第三阶段：RS dispatch/execute/commit 双宽

## 当前状态

- 当前实现仍是单宽 front-end，后端只有部分执行单元是多路
- 第一阶段正在进行：先把前端真正扩成 2-wide
