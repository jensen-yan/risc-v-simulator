# RISC-V Simulator 上下文

本文档记录架构工作中使用的项目领域语言。它是一份术语表，不是
`ARCHITECTURE.md` 的重复版本。

## 核心边界

**Simulator（模拟器）**：
顶层执行边界，负责程序加载、内存、CPU 选择、运行生命周期，以及外部可见的运行结果。
_避免使用_：main loop、driver、harness

**Instruction Window（指令窗口）**：
一个有边界的运行区间，包含独立的预热部分和测量部分，用于评估已加载的 checkpoint 或 workload slice。
_避免使用_：benchmark run、fixed cycle loop

**Checkpoint Runner（检查点运行器）**：
导入 checkpoint workload slice、运行指令窗口，并报告 slice 级别成功、失败原因、IPC 和元数据的组件。
_避免使用_：benchmark script、loader

**DiffTest（差分测试）**：
乱序 CPU 与参考顺序 CPU 在提交时进行的架构状态比较。
_避免使用_：unit test、trace checker、random validation

**Reference Execution Context（参考执行上下文）**：
与乱序运行并行维护的顺序 CPU 和内存状态，用于支撑 DiffTest。
_避免使用_：golden model、backup CPU

## 系统语义

**Address Translation（地址翻译）**：
系统级虚拟地址到物理地址的翻译边界，服务于当前特权状态下的取指、load 和 store 访问。
_避免使用_：page walker、address helper

**SV39 Page Walker（SV39 页表遍历器）**：
虚拟内存启用时，Address Translation 使用的 RISC-V Sv39 页表遍历逻辑。
_避免使用_：TLB、generic translation

**Privilege State（特权状态）**：
决定有效访问模式、地址翻译行为和特权返回行为的架构特权模式状态。
_避免使用_：mode flag、CSR dump

**Host Communication（主机通信）**：
tohost/fromhost 内存映射约定，用来让 guest 程序向模拟器 host 通知完成状态或其他状态。
_避免使用_：syscall、normal memory-mapped device

## 乱序流水线

**Out-of-Order Pipeline（乱序流水线）**：
CPU 模式，建模指令经过 fetch、decode、issue、execute、writeback 和 commit 的流动过程，包含推测执行和顺序退休。
_避免使用_：OOO blob、pipeline code

**Structural Constraint Simulator（结构约束模拟器）**：
乱序 CPU 的建模定位：捕捉能够解释性能行为的时序和资源约束，但不尝试复刻周期精确的 RTL。
_避免使用_：full RTL replica、teaching-only Tomasulo model、functional-only simulator

**Stage Context（阶段上下文）**：
面向特定流水级的适配器，只暴露该流水级在一次执行步骤中需要的状态和动作。
_避免使用_：把原始 `CPUState` 当作阶段接口传入

**Dispatch（派发到 Issue Queue）**：
指令完成重命名后进入 Reservation Station / Issue Queue 的后端边界；当前代码中这部分主要由 `DispatchStage` 完成。
_避免使用_：把 Dispatch 用来指代从 Issue Queue 选择 ready 指令并送入执行单元

**Issue / Ready Select（发射 / 就绪选择）**：
从 Reservation Station / Issue Queue 中选择操作数已就绪且资源可用的指令，并让它开始占用对应执行单元的边界；当前代码中这部分主要由 `ReservationStation::issue_ready_instructions` 和 `ExecuteStage` 完成。
_避免使用_：把进入 Reservation Station / Issue Queue 也叫 Issue

**Completion Fabric（完成网络）**：
乱序流水线的结果完成边界，仲裁已执行工作何时对唤醒、物理寄存器写回和 ROB 完成可见。
_避免使用_：把 Common Data Bus 当作长期领域边界

**Completion Backpressure（完成反压）**：
执行单元已经产生结果，但当前周期 Completion Fabric 无法再接收完成事件时，执行单元继续持有该结果的结构约束。
_避免使用_：把每个已完成结果都静默缓存在无界队列里

**OOO Recovery（乱序恢复）**：
redirect、trap、fence 或其他恢复原因之后，乱序流水线移除推测工作的共享规则。
_避免使用_：把 flush 清理规则分散到各个流水级中

## 执行阶段

**Execute Memory Order（执行阶段内存顺序）**：
执行侧规则，用于决定 load 何时可以越过地址尚未解析的更老 store，以及如何从检测到的顺序违规中恢复。
_避免使用_：把地址未知推测和恢复规则分散写在 `ExecuteStage` 内部

**Execute Control Recovery（执行阶段控制流恢复）**：
执行侧的早期恢复路径，用于已经解析的 branch/JALR 预测错误。
_避免使用_：把早期控制流恢复埋在通用执行单元完成逻辑里

**Execute DCache Access（执行阶段 DCache 访问）**：
执行侧 load/store 的 D$ 时序握手，包括请求启动、阻塞处理、延迟统计和 cache 计数器更新。
_避免使用_：在 load/store 执行分支里重复 D$ 请求记账逻辑

**Execute Load Path（执行阶段 Load 路径）**：
执行侧 load 从冒险检查到结果发布的路径，包括 replay 决策、store-to-load forwarding、内存读取、值格式化和异常发布。
_避免使用_：把 load hazard、access、value 和 completion 拆成互相重复的领域术语

**Execute Store Access（执行阶段 Store 访问）**：
执行侧 store 完成路径，包括 host-comm 串行化、D$ 写时序、inflight 移动，以及内存顺序违规恢复触发。
_避免使用_：把 store cache 时序和恢复触发决策直接混进 `ExecuteStage`

**Execute Memory Inflight（执行阶段内存未完成队列）**：
执行侧队列，保存已经发出 cache 请求、且不再占用执行单元或 reservation-station 项的 load/store D$ miss。
_避免使用_：把 inflight 队列移动、等待推进和完成清理分散在 `ExecuteStage` 各处

**Execute Host-Comm Access（执行阶段主机通信访问）**：
tohost/fromhost 内存映射访问的执行侧串行化规则；这类访问必须等到指令到达 ROB head 后才能触碰主机通信状态。
_避免使用_：把 host-comm MMIO 顺序检查藏在各个 load/store 完成分支里

## 提交阶段

**Commit Memory Effects（提交阶段内存效果）**：
store、浮点 store 或 AMO 指令退休时应用的架构内存和 LR/SC reservation 更新。
_避免使用_：把 store/AMO 的内存副作用混进无关的提交记账逻辑中

**Commit Register Effects（提交阶段寄存器效果）**：
指令退休时应用的架构寄存器、浮点 flag 和 rename-map 更新。
_避免使用_：把整数/浮点写回和 rename commit 规则散落在 commit loop 里

**Commit Control Flow Effects（提交阶段控制流效果）**：
branch、JAL 或 JALR 退休时应用的预测器、profile、计数器更新，以及 redirect 决策。
_避免使用_：把预测器训练、profile 统计和 commit-loop flush 编排混在一个代码块里

**Commit System Effects（提交阶段系统效果）**：
串行化指令退休时应用的 CSR、trap、syscall、MRET 和 FENCE.I 效果。
_避免使用_：把特权/系统指令语义保留为 commit loop 中的零散分支

**Commit Retire Effects（提交阶段退休效果）**：
指令成功退休后的通用记账工作，包括 store-buffer 退休、rename checkpoint 清理，以及 load/store profile 更新。
_避免使用_：把已退休工作记账混进主 commit loop

## 内存顺序术语

**Addr-Unknown Store（地址未知 Store）**：
当更年轻的 load 被考虑 dispatch 或 execute 时，尚未知道有效地址的更老 store。
_避免使用_：unresolved store、pending store address

**Bad Addr-Unknown Pair（坏地址未知对）**：
被记录下来的 load/store PC 对；对于这对 PC，曾经允许 load 越过地址未知 store，并因此导致内存顺序违规。
_避免使用_：blacklist entry

## 术语边界

- 用 **Structural Constraint Simulator** 表示预期的 OOO 建模精度；不要描述成完整 RTL 复刻或纯功能模拟器。
- 用 **Completion Fabric** 表示结果完成边界；Common Data Bus 只用于指代遗留实现或迁移细节。
- 用 **Completion Backpressure** 表示完成带宽导致已完成执行单元停住；不要把它建模成永远可用的结果队列。
- 用 **Address Translation** 表示 fetch/load/store 的地址翻译边界；**SV39 Page Walker** 只表示边界内部的 Sv39 页表遍历。
- 用 **DiffTest** 表示提交时参考比较，不用于普通单元测试或 benchmark 结果检查。
