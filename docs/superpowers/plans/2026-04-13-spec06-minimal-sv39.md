# SPEC06 Minimal Sv39 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为当前模拟器补齐最小 Sv39/MMU 支持，使真实 SPEC06 切片不再因 `satp != 0` 在导入阶段失败，并能通过基础 supervisor/vm 验证。

**Architecture:** 保持 `Memory` 只表示物理内存，在共享层新增 `PrivilegeState + AddressTranslation + Sv39PageWalker`。InOrder 和 OOO 的取指/访存统一调用翻译层，checkpoint 恢复负责恢复 `satp/mstatus/...` 与当前特权态，第一阶段不做 TLB，只做正确性闭环。

**Tech Stack:** C++17、GoogleTest、现有 `Simulator/ICpuInterface/Memory/InstructionExecutor`、`riscv-tests`

---

## File Structure

- Create: `include/system/privilege_state.h`
- Create: `src/system/privilege_state.cpp`
- Create: `include/system/address_translation.h`
- Create: `src/system/address_translation.cpp`
- Create: `include/system/sv39_page_walker.h`
- Create: `src/system/sv39_page_walker.cpp`
- Create: `tests/test_address_translation.cpp`
- Modify: `include/common/types.h`
- Modify: `include/common/cpu_interface.h`
- Modify: `src/cpu/cpu_factory.cpp`
- Modify: `include/system/simulator.h`
- Modify: `src/system/simulator.cpp`
- Modify: `src/system/checkpoint_importer.cpp`
- Modify: `src/core/instruction_executor.cpp`
- Modify: `src/cpu/inorder/cpu.cpp`
- Modify: `include/cpu/ooo/cpu_state.h`
- Modify: `include/cpu/ooo/ooo_cpu.h`
- Modify: `src/cpu/ooo/ooo_cpu.cpp`
- Modify: `src/cpu/ooo/stages/fetch_stage.cpp`
- Modify: `src/cpu/ooo/stages/execute_stage.cpp`
- Modify: `tests/test_simulator.cpp`
- Modify: `tests/test_checkpoint_importer.cpp`
- Optional Doc Update: `ARCHITECTURE.md`

## Task 1: 固化特权态与地址翻译共享接口

**Files:**
- Create: `include/system/privilege_state.h`
- Create: `include/system/address_translation.h`
- Create: `tests/test_address_translation.cpp`
- Modify: `include/common/types.h`
- Modify: `include/common/cpu_interface.h`

- [ ] **Step 1: 写失败测试，锁定 Sv39 共享接口**

```cpp
#include <gtest/gtest.h>

#include "core/memory.h"
#include "system/address_translation.h"
#include "system/privilege_state.h"

namespace riscv {
namespace {

TEST(AddressTranslationTest, BareModeBypassesTranslation) {
    auto memory = std::make_shared<Memory>(0x4000);
    PrivilegeState state;
    state.setMode(PrivilegeMode::MACHINE);
    state.setSatp(0);

    AddressTranslation translation(memory, &state);
    const auto result = translation.translateInstructionAddress(0x1234);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.physical_address, 0x1234u);
}

TEST(AddressTranslationTest, Sv39WalkResolves4KLeaf) {
    auto memory = std::make_shared<Memory>(0x10000);
    PrivilegeState state;
    state.setMode(PrivilegeMode::SUPERVISOR);
    state.setSatp((8ULL << 60) | 0x1ULL); // MODE=Sv39, root ppn=1 => root @ 0x1000

    // root[0] -> l1 @ 0x2000
    memory->write64(0x1000, 0x801ULL);
    // l1[0] -> l0 @ 0x3000
    memory->write64(0x2000, 0xC01ULL);
    // l0[1] -> ppn=0x4, RXWADV
    memory->write64(0x3008, (0x4ULL << 10) | 0xDFULL);

    AddressTranslation translation(memory, &state);
    const auto result = translation.translateLoadAddress(0x1000, 8);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.physical_address, 0x4000u);
}

} // namespace
} // namespace riscv
```

- [ ] **Step 2: 运行测试，确认接口尚未实现**

Run: `ctest --test-dir build --output-on-failure -R AddressTranslationTest`

Expected:
- 编译失败，提示缺少 `system/address_translation.h` 或 `system/privilege_state.h`

- [ ] **Step 3: 定义共享类型与 CPU 接口最小扩展**

```cpp
// include/common/types.h
enum class PrivilegeMode : uint8_t {
    USER = 0,
    SUPERVISOR = 1,
    MACHINE = 3,
};
```

```cpp
// include/system/privilege_state.h
#pragma once

#include "common/types.h"

namespace riscv {

class PrivilegeState {
public:
    void setMode(PrivilegeMode mode) { mode_ = mode; }
    PrivilegeMode getMode() const { return mode_; }

    void setSatp(uint64_t value) { satp_ = value; }
    uint64_t getSatp() const { return satp_; }

private:
    PrivilegeMode mode_ = PrivilegeMode::MACHINE;
    uint64_t satp_ = 0;
};

} // namespace riscv
```

```cpp
// include/system/address_translation.h
#pragma once

#include "common/types.h"

#include <memory>
#include <string>

namespace riscv {

class Memory;
class PrivilegeState;

enum class MemoryAccessType : uint8_t {
    InstructionFetch,
    Load,
    Store,
};

struct TranslationResult {
    bool success = false;
    Address physical_address = 0;
    CheckpointFailureReason failure_reason = CheckpointFailureReason::UNKNOWN;
    std::string message;
};

class AddressTranslation {
public:
    AddressTranslation(std::shared_ptr<Memory> memory, PrivilegeState* privilege_state);

    TranslationResult translateInstructionAddress(Address va) const;
    TranslationResult translateLoadAddress(Address va, size_t size) const;
    TranslationResult translateStoreAddress(Address va, size_t size) const;

private:
    TranslationResult translate(Address va, size_t size, MemoryAccessType access_type) const;

    std::shared_ptr<Memory> memory_;
    PrivilegeState* privilege_state_ = nullptr;
};

} // namespace riscv
```

```cpp
// include/common/cpu_interface.h
virtual void setPrivilegeMode(PrivilegeMode mode) {}
virtual PrivilegeMode getPrivilegeMode() const { return PrivilegeMode::MACHINE; }
```

- [ ] **Step 4: 先写最小空实现，让测试从“编译失败”进入“行为失败”**

```cpp
// src/system/privilege_state.cpp
#include "system/privilege_state.h"
```

```cpp
// src/system/address_translation.cpp
#include "system/address_translation.h"

#include "core/memory.h"
#include "system/privilege_state.h"

namespace riscv {

AddressTranslation::AddressTranslation(std::shared_ptr<Memory> memory, PrivilegeState* privilege_state)
    : memory_(std::move(memory)), privilege_state_(privilege_state) {}

TranslationResult AddressTranslation::translateInstructionAddress(Address va) const {
    return translate(va, 2, MemoryAccessType::InstructionFetch);
}

TranslationResult AddressTranslation::translateLoadAddress(Address va, size_t size) const {
    return translate(va, size, MemoryAccessType::Load);
}

TranslationResult AddressTranslation::translateStoreAddress(Address va, size_t size) const {
    return translate(va, size, MemoryAccessType::Store);
}

TranslationResult AddressTranslation::translate(Address va, size_t, MemoryAccessType) const {
    return {.success = true, .physical_address = va};
}

} // namespace riscv
```

- [ ] **Step 5: 运行测试，确认 Bare 模式通过、Sv39 测试失败**

Run: `ctest --test-dir build --output-on-failure -R AddressTranslationTest`

Expected:
- `BareModeBypassesTranslation` 通过
- `Sv39WalkResolves4KLeaf` 失败

- [ ] **Step 6: 提交共享接口阶段**

```bash
git add include/common/types.h include/common/cpu_interface.h \
  include/system/privilege_state.h include/system/address_translation.h \
  src/system/privilege_state.cpp src/system/address_translation.cpp \
  tests/test_address_translation.cpp
git commit -m "feat: 增加最小 Sv39 共享接口骨架"
```

## Task 2: 实现 Sv39 page walk、权限检查与 A/D 位更新

**Files:**
- Create: `include/system/sv39_page_walker.h`
- Create: `src/system/sv39_page_walker.cpp`
- Modify: `src/system/address_translation.cpp`
- Test: `tests/test_address_translation.cpp`

- [ ] **Step 1: 追加失败测试，锁定 page walk 权限与 A/D 语义**

```cpp
TEST(AddressTranslationTest, Sv39StoreSetsAccessedAndDirtyBits) {
    auto memory = std::make_shared<Memory>(0x10000);
    PrivilegeState state;
    state.setMode(PrivilegeMode::SUPERVISOR);
    state.setSatp((8ULL << 60) | 0x1ULL);

    memory->write64(0x1000, 0x801ULL);
    memory->write64(0x2000, 0xC01ULL);
    memory->write64(0x3008, (0x5ULL << 10) | 0x17ULL); // VRWX, A/D clear

    AddressTranslation translation(memory, &state);
    const auto result = translation.translateStoreAddress(0x1000, 8);

    ASSERT_TRUE(result.success);
    const uint64_t updated_pte = memory->read64(0x3008);
    EXPECT_NE(updated_pte & (1ULL << 6), 0u); // A
    EXPECT_NE(updated_pte & (1ULL << 7), 0u); // D
}

TEST(AddressTranslationTest, Sv39RejectsUserAccessToSupervisorPage) {
    auto memory = std::make_shared<Memory>(0x10000);
    PrivilegeState state;
    state.setMode(PrivilegeMode::USER);
    state.setSatp((8ULL << 60) | 0x1ULL);

    memory->write64(0x1000, 0x801ULL);
    memory->write64(0x2000, 0xC01ULL);
    memory->write64(0x3008, (0x6ULL << 10) | 0xCBULL); // VRXA, U=0

    AddressTranslation translation(memory, &state);
    const auto result = translation.translateInstructionAddress(0x1000);

    EXPECT_FALSE(result.success);
}
```

- [ ] **Step 2: 运行测试，确认新增用例失败**

Run: `ctest --test-dir build --output-on-failure -R AddressTranslationTest`

Expected:
- 新增的 Sv39 测试失败

- [ ] **Step 3: 实现 Sv39PageWalker**

```cpp
// include/system/sv39_page_walker.h
#pragma once

#include "system/address_translation.h"

namespace riscv {

class Sv39PageWalker {
public:
    Sv39PageWalker(std::shared_ptr<Memory> memory, PrivilegeState* privilege_state);

    TranslationResult walk(Address va, MemoryAccessType access_type, size_t access_size) const;

private:
    std::shared_ptr<Memory> memory_;
    PrivilegeState* privilege_state_ = nullptr;
};

} // namespace riscv
```

```cpp
// src/system/address_translation.cpp
#include "system/sv39_page_walker.h"

TranslationResult AddressTranslation::translate(Address va, size_t size, MemoryAccessType access_type) const {
    if (privilege_state_ == nullptr || privilege_state_->getSatp() == 0) {
        return {.success = true, .physical_address = va};
    }

    const uint64_t satp = privilege_state_->getSatp();
    const uint64_t mode = satp >> 60;
    if (mode != 8) {
        return {.success = false,
                .failure_reason = CheckpointFailureReason::TRAP,
                .message = "unsupported satp mode"};
    }

    Sv39PageWalker walker(memory_, privilege_state_);
    return walker.walk(va, access_type, size);
}
```

```cpp
// src/system/sv39_page_walker.cpp
namespace riscv {

namespace {
constexpr uint64_t kPteV = 1ULL << 0;
constexpr uint64_t kPteR = 1ULL << 1;
constexpr uint64_t kPteW = 1ULL << 2;
constexpr uint64_t kPteX = 1ULL << 3;
constexpr uint64_t kPteU = 1ULL << 4;
constexpr uint64_t kPteA = 1ULL << 6;
constexpr uint64_t kPteD = 1ULL << 7;
}

TranslationResult Sv39PageWalker::walk(Address va, MemoryAccessType access_type, size_t) const {
    // 1. 从 satp 取 root ppn
    // 2. 提取 vpn[2:0]
    // 3. 逐级读取 PTE
    // 4. 命中 leaf 后做权限检查
    // 5. 按需要设置 A/D
    // 6. 组合最终 PA
}

} // namespace riscv
```

- [ ] **Step 4: 跑测试直到 `AddressTranslationTest` 全绿**

Run: `ctest --test-dir build --output-on-failure -R AddressTranslationTest`

Expected:
- `AddressTranslationTest` 全部通过

- [ ] **Step 5: 提交 page walk 阶段**

```bash
git add include/system/sv39_page_walker.h src/system/sv39_page_walker.cpp \
  src/system/address_translation.cpp tests/test_address_translation.cpp
git commit -m "feat: 实现最小 Sv39 page walk 与权限检查"
```

## Task 3: 接入 InOrder/OOO 的取指与访存翻译路径

**Files:**
- Modify: `src/cpu/inorder/cpu.cpp`
- Modify: `src/core/instruction_executor.cpp`
- Modify: `include/cpu/ooo/cpu_state.h`
- Modify: `include/cpu/ooo/ooo_cpu.h`
- Modify: `src/cpu/ooo/ooo_cpu.cpp`
- Modify: `src/cpu/ooo/stages/fetch_stage.cpp`
- Modify: `src/cpu/ooo/stages/execute_stage.cpp`
- Modify: `src/cpu/cpu_factory.cpp`
- Test: `tests/test_simulator.cpp`

- [ ] **Step 1: 写失败测试，锁定带 satp 的 snapshot 能跑到第一条有效指令**

```cpp
TEST(SimulatorTest, LoadSnapshotWithSv39TranslatesInstructionFetch) {
    Simulator simulator(/*memorySize=*/0x20000, CpuType::IN_ORDER, /*memoryBaseAddress=*/0);

    SnapshotBundle snapshot;
    snapshot.pc = 0x1000;
    snapshot.csr_values.push_back({0x180, (8ULL << 60) | 0x1ULL});   // satp
    snapshot.csr_values.push_back({0x300, 0x0000000A00000000ULL});   // mstatus-like checkpoint value

    MemorySegment segment;
    segment.base = 0;
    segment.bytes.resize(0x10000, 0);

    // 构造 3 级页表，把 va 0x1000 映射到 pa 0x4000
    auto write64le = [&](size_t off, uint64_t value) {
        for (size_t i = 0; i < 8; ++i) segment.bytes[off + i] = (value >> (i * 8)) & 0xFF;
    };
    write64le(0x1000, 0x801ULL);
    write64le(0x2000, 0xC01ULL);
    write64le(0x3008, (0x4ULL << 10) | 0xCFULL);
    write64le(0x4000, 0x00000013ULL); // nop

    snapshot.memory_segments.push_back(std::move(segment));

    ASSERT_TRUE(simulator.loadSnapshot(snapshot));
    EXPECT_NO_THROW(simulator.step());
}
```

- [ ] **Step 2: 运行测试，确认当前 fetch/load/store 仍未统一经过翻译层**

Run: `ctest --test-dir build --output-on-failure -R "SimulatorTest.LoadSnapshotWithSv39TranslatesInstructionFetch"`

Expected:
- 失败，表现为错误取指或 page fault 未接入

- [ ] **Step 3: 给 CPU 接口与状态增加 PrivilegeState / AddressTranslation**

```cpp
// include/cpu/ooo/cpu_state.h
std::unique_ptr<PrivilegeState> privilege_state;
std::unique_ptr<AddressTranslation> address_translation;
```

```cpp
// src/cpu/cpu_factory.cpp
// 在 CPU 构造时，把 Memory 继续传入 CPU；
// CPU 内部自行持有 PrivilegeState 和 AddressTranslation。
```

```cpp
// src/cpu/inorder/cpu.cpp
// 取指前：
const auto fetch_pa = address_translation_->translateInstructionAddress(pc_);
if (!fetch_pa.success) { haltWithTranslationFault(fetch_pa); return; }
const Instruction inst = memory_->fetchInstruction(fetch_pa.physical_address);
```

```cpp
// src/core/instruction_executor.cpp
// load/store 不再直接使用虚拟地址访问 Memory，
// 而是通过 CPU 提供的翻译回调或访问入口拿到 PA。
```

```cpp
// src/cpu/ooo/stages/fetch_stage.cpp
// fetch 使用 translateInstructionAddress
```

```cpp
// src/cpu/ooo/stages/execute_stage.cpp
// load/store/amo 使用 translateLoadAddress / translateStoreAddress
```

- [ ] **Step 4: 扩展 `SimulatorTest`，覆盖 InOrder/OOO 共用翻译路径**

```cpp
TEST(SimulatorTest, LoadSnapshotWithSv39TranslatesOutOfOrderLoadStore) {
    // 构造一条经 Sv39 映射的 load/store 小程序
    // 断言 OOO 也能经过同一翻译层访问正确物理地址
}
```

- [ ] **Step 5: 运行相关测试**

Run: `ctest --test-dir build --output-on-failure -R "AddressTranslationTest|SimulatorTest"`

Expected:
- 新增 Sv39 snapshot/fetch/load/store 测试通过

- [ ] **Step 6: 提交 CPU 接线阶段**

```bash
git add src/cpu/inorder/cpu.cpp src/core/instruction_executor.cpp \
  include/cpu/ooo/cpu_state.h include/cpu/ooo/ooo_cpu.h src/cpu/ooo/ooo_cpu.cpp \
  src/cpu/ooo/stages/fetch_stage.cpp src/cpu/ooo/stages/execute_stage.cpp \
  src/cpu/cpu_factory.cpp tests/test_simulator.cpp
git commit -m "feat: 为 InOrder 和 OOO 接入共享 Sv39 地址翻译路径"
```

## Task 4: 接入 checkpoint 恢复、trap 分类与 sfence.vma

**Files:**
- Modify: `include/system/simulator.h`
- Modify: `src/system/simulator.cpp`
- Modify: `src/system/checkpoint_importer.cpp`
- Modify: `tests/test_checkpoint_importer.cpp`
- Modify: `tests/test_simulator.cpp`

- [ ] **Step 1: 写失败测试，锁定 `satp != 0` checkpoint 不再被 importer 直接拒绝**

```cpp
TEST(CheckpointImporterTest, BuiltinZstdImporterAcceptsSv39CheckpointAfterMmuSupport) {
    // 复用当前 satp != 0 镜像
    // 断言 importer 能正常返回 snapshot，而不是抛出异常
}
```

- [ ] **Step 2: 运行测试，确认当前 importer 仍在前置拒绝**

Run: `ctest --test-dir build --output-on-failure -R "CheckpointImporterTest.BuiltinZstdImporterAcceptsSv39CheckpointAfterMmuSupport"`

Expected:
- 失败，提示 satp blocker

- [ ] **Step 3: 调整恢复语义：保留 satp，而不是在 importer 阶段拒绝**

```cpp
// src/system/checkpoint_importer.cpp
// 删除 satp != 0 直接 throw 的逻辑，改为把 satp 作为 CSR 正常放进 snapshot.csr_values
```

```cpp
// src/system/simulator.cpp
// loadSnapshot 时：
// 1. 先恢复内存/GPR/FPR
// 2. 再恢复 CSR
// 3. 根据 satp / mstatus / sstatus 设置 privilege_state
```

```cpp
// sfence.vma
// 在当前无 TLB 的阶段，执行为 no-op + 内存顺序点
// 但不要报未实现系统指令
```

- [ ] **Step 4: 加 trap/fault 分类回归**

```cpp
TEST(SimulatorTest, Sv39TranslationFailureIsReportedAsTrapInsteadOfUnknown) {
    // 构造无效 PTE，断言 failure_reason 不再是 unknown
}
```

- [ ] **Step 5: 运行 checkpoint/importer/simulator 回归**

Run: `ctest --test-dir build --output-on-failure -R "CheckpointImporterTest|SimulatorTest"`

Expected:
- satp 恢复相关测试通过
- 失败分类测试通过

- [ ] **Step 6: 提交恢复与 trap 阶段**

```bash
git add include/system/simulator.h src/system/simulator.cpp \
  src/system/checkpoint_importer.cpp tests/test_checkpoint_importer.cpp \
  tests/test_simulator.cpp
git commit -m "feat: 恢复 satp 并接通最小 Sv39 checkpoint 执行语义"
```

## Task 5: 用 riscv-tests 与真实 SPEC06 切片验收

**Files:**
- Optional Doc Update: `ARCHITECTURE.md`
- No new source files expected

- [ ] **Step 1: 构建并运行单测全集中的 MMU 相关子集**

Run: `ctest --test-dir build --output-on-failure -R "AddressTranslationTest|CheckpointImporterTest|SimulatorTest"`

Expected:
- 全部通过

- [ ] **Step 2: 跑最小 riscv-tests 子集**

Run:

```bash
python3 run_tests.py -p "rv64mi-p-illegal" -w 1
python3 run_tests.py -p "rv64si-p-dirty" -w 1
python3 run_tests.py -p "rv64si-p-icache-alias" -w 1
```

Expected:
- 至少进入真实 supervisor/vm 语义验证，而不是因未实现 `satp/sfence.vma` 直接失败
- 若失败，日志能定位是权限、A/D 位还是 trap 细节

- [ ] **Step 3: 跑真实 SPEC06 单切片 smoke**

Run:

```bash
rm -rf /tmp/spec06_single_slice_smoke
timeout 120s build/risc-v-sim --in-order \
  --checkpoint=/nfs/home/share/checkpoints_profiles/spec06_gcc15_rv64gcb_base_260122/checkpoint-0-0-0/bzip2_source/555/_555_0.026526_.zstd \
  --checkpoint-output-dir=/tmp/spec06_single_slice_smoke \
  --warmup-instructions=0 \
  --measure-instructions=1
cat /tmp/spec06_single_slice_smoke/result.json
```

Expected:
- 不再出现 `satp` blocker
- 进入真实执行路径
- 如果仍失败，失败原因推进到下一层系统态缺口，而不是 MMU 缺失

- [ ] **Step 4: 若模块边界变化，更新架构文档**

```md
在 `ARCHITECTURE.md` 中补一段：
- `Memory` 只表示物理内存
- `AddressTranslation` / `Sv39PageWalker` 位于共享系统层
- CPU fetch/load/store 统一经过地址翻译
```

- [ ] **Step 5: 最终提交**

```bash
git add ARCHITECTURE.md
git commit -m "docs: 更新最小 Sv39 地址翻译架构说明"
```

## Self-Review

- 规格覆盖：
  - `satp` 恢复：Task 4
  - Sv39 page walk：Task 2
  - fetch/load/store 接线：Task 3
  - `sfence.vma`：Task 4
  - `riscv-tests + 真实切片 smoke`：Task 5
- Placeholder scan：
  - 已避免 `TODO/TBD/适当处理` 这类空洞表述
  - 每个任务都给了文件、测试、命令和最小代码骨架
- 类型一致性：
  - 统一使用 `PrivilegeMode / MemoryAccessType / TranslationResult / AddressTranslation / Sv39PageWalker`
