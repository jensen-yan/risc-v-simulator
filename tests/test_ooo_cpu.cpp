#include <gtest/gtest.h>
#include "cpu/ooo/ooo_cpu.h"
#include "core/memory.h"
#include <memory>
#include <sstream>
#include "common/debug_types.h"

namespace riscv {

class OutOfOrderCPUTest : public ::testing::Test {
protected:
    std::shared_ptr<Memory> memory;
    std::unique_ptr<OutOfOrderCPU> cpu;
    
    void SetUp() override {
        memory = std::make_shared<Memory>(8192);  // 8KB内存
        cpu = std::make_unique<OutOfOrderCPU>(memory);

        // 启用调试输出
        auto& debugManager = DebugManager::getInstance();

        // 设置回调函数
        debugManager.setCallback([](const DebugInfo& info) {
            std::cout << DebugFormatter::format(info) << std::endl;
        });
        
        // 启用相关分类
        debugManager.enableCategory("RS");
        debugManager.enableCategory("SYSTEM");
        debugManager.enableCategory("COMMIT");
        debugManager.enableCategory("ISSUE");
        debugManager.enableCategory("WRITEBACK");
        debugManager.enableCategory("RENAME");
        debugManager.enableCategory("ROB");
        debugManager.enableCategory("DECODE");
    }
    
    void TearDown() override {
        cpu.reset();
        memory.reset();
    }
    
    // 辅助函数：向内存写入指令
    void writeInstruction(uint32_t address, uint32_t instruction) {
        memory->writeWord(address, instruction);
    }
    
    // 辅助函数：创建R型指令
    uint32_t createRTypeInstruction(uint8_t funct7, uint8_t rs2, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode) {
        return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
    }

    // 辅助函数：创建AMO指令
    uint32_t createAMOTypeInstruction(uint8_t funct5, uint8_t rs2, uint8_t rs1, uint8_t funct3, uint8_t rd) {
        return (funct5 << 27) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | 0x2F;
    }
    
    // 辅助函数：创建I型指令
    uint32_t createITypeInstruction(int16_t imm, uint8_t rs1, uint8_t funct3, uint8_t rd, uint8_t opcode) {
        return (imm << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
    }

    // 辅助函数：创建S型指令
    uint32_t createSTypeInstruction(int16_t imm, uint8_t rs2, uint8_t rs1, uint8_t funct3, uint8_t opcode) {
        const uint32_t imm12 = static_cast<uint16_t>(imm) & 0x0FFFu;
        const uint32_t imm_low = imm12 & 0x1Fu;
        const uint32_t imm_high = (imm12 >> 5) & 0x7Fu;
        return (imm_high << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (imm_low << 7) | opcode;
    }

    uint32_t createBTypeInstruction(int16_t imm, uint8_t rs2, uint8_t rs1, uint8_t funct3) {
        const uint32_t imm13 = static_cast<uint16_t>(imm) & 0x1FFFu;
        const uint32_t bit12 = (imm13 >> 12) & 0x1u;
        const uint32_t bit11 = (imm13 >> 11) & 0x1u;
        const uint32_t bits10_5 = (imm13 >> 5) & 0x3Fu;
        const uint32_t bits4_1 = (imm13 >> 1) & 0xFu;
        return (bit12 << 31) | (bits10_5 << 25) | (rs2 << 20) | (rs1 << 15) |
               (funct3 << 12) | (bits4_1 << 8) | (bit11 << 7) | 0x63;
    }

    // 辅助函数：创建SYSTEM指令（CSR/ECALL/EBREAK等）
    uint32_t createSystemInstruction(uint16_t imm12, uint8_t rs1, uint8_t funct3, uint8_t rd) {
        return (static_cast<uint32_t>(imm12) << 20) |
               (static_cast<uint32_t>(rs1) << 15) |
               (static_cast<uint32_t>(funct3) << 12) |
               (static_cast<uint32_t>(rd) << 7) | 0x73;
    }
    
    // 辅助函数：创建ECALL指令
    uint32_t createECallInstruction() {
        return 0x00000073;  // ECALL指令的机器码
    }

    uint32_t createEBreakInstruction() {
        return 0x00100073;  // EBREAK指令的机器码
    }
};

// 测试1：基本CPU初始化
TEST_F(OutOfOrderCPUTest, BasicInitialization) {
    EXPECT_FALSE(cpu->isHalted()) << "CPU初始化时不应该停机";
    EXPECT_EQ(cpu->getPC(), 0) << "初始PC应该为0";
    EXPECT_EQ(cpu->getInstructionCount(), 0) << "初始指令计数应该为0";
    EXPECT_EQ(cpu->getCycleCount(), 0) << "初始周期计数应该为0";
    
    // 检查寄存器初始状态
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(cpu->getRegister(i), 0) << "寄存器 x" << i << " 初始值应该为0";
    }
}

// 测试2：简单指令执行
TEST_F(OutOfOrderCPUTest, SimpleInstructionExecution) {
    // 写入简单的ADD指令: add x1, x0, x0 (x1 = x0 + x0 = 0)
    uint32_t add_inst = createRTypeInstruction(0x00, 0, 0, 0x0, 1, 0x33);
    writeInstruction(0x0, add_inst);
    
    // 写入ECALL指令结束程序
    writeInstruction(0x4, createECallInstruction());
    
    // 设置PC并执行
    cpu->setPC(0x0);
    
    // 执行几个周期
    for (int i = 0; i < 200 && !cpu->isHalted(); ++i) {
        cpu->step();
    }
    
    // 验证结果
    EXPECT_TRUE(cpu->isHalted()) << "程序应该已经停机";
    EXPECT_GT(cpu->getCycleCount(), 0) << "应该执行了一些周期";
}

// 测试3：寄存器操作
TEST_F(OutOfOrderCPUTest, RegisterOperations) {
    // 测试寄存器读写
    cpu->setRegister(1, 0x12345678);
    EXPECT_EQ(cpu->getRegister(1), 0x12345678) << "寄存器写入读取应该一致";
    
    // 测试x0寄存器始终为0
    cpu->setRegister(0, 0xFFFFFFFF);
    EXPECT_EQ(cpu->getRegister(0), 0) << "x0寄存器应该始终为0";
    
    // 测试浮点寄存器
    cpu->setFPRegister(0, 0x12345678);
    EXPECT_EQ(cpu->getFPRegister(0), 0x12345678) << "浮点寄存器写入读取应该一致";
    
    // 测试浮点寄存器浮点数访问
    cpu->setFPRegisterFloat(1, 3.14159f);
    EXPECT_NEAR(cpu->getFPRegisterFloat(1), 3.14159f, 0.00001f) << "浮点寄存器浮点数访问应该正确";
}

// 测试4：立即数指令
TEST_F(OutOfOrderCPUTest, ImmediateInstructions) {
    // ADDI x1, x0, 42  (x1 = x0 + 42 = 42)
    uint32_t addi_inst = createITypeInstruction(42, 0, 0x0, 1, 0x13);
    writeInstruction(0x0, addi_inst);
    
    // ECALL
    writeInstruction(0x4, createECallInstruction());
    
    cpu->setPC(0x0);
    
    // 执行直到停机
    for (int i = 0; i < 200 && !cpu->isHalted(); ++i) {
        cpu->step();
    }
    
    // 验证结果
    EXPECT_TRUE(cpu->isHalted()) << "程序应该已经停机";
    EXPECT_EQ(cpu->getRegister(1), 42) << "x1寄存器应该包含42";
}

// 测试5：多指令序列
TEST_F(OutOfOrderCPUTest, MultipleInstructions) {
    // 指令序列:
    // ADDI x1, x0, 10   // x1 = 10
    // ADDI x2, x0, 20   // x2 = 20  
    // ADD x3, x1, x2    // x3 = x1 + x2 = 30
    // ECALL
    
    writeInstruction(0x0, createITypeInstruction(10, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createITypeInstruction(20, 0, 0x0, 2, 0x13));
    writeInstruction(0x8, createRTypeInstruction(0x00, 2, 1, 0x0, 3, 0x33));
    writeInstruction(0xC, createECallInstruction());
    
    cpu->setPC(0x0);
    
    // 执行直到停机
    for (int i = 0; i < 50 && !cpu->isHalted(); ++i) {
        cpu->step();
    }
    
    // 验证结果
    EXPECT_TRUE(cpu->isHalted()) << "程序应该已经停机";
    EXPECT_EQ(cpu->getRegister(1), 10) << "x1应该等于10";
    EXPECT_EQ(cpu->getRegister(2), 20) << "x2应该等于20";
    EXPECT_EQ(cpu->getRegister(3), 30) << "x3应该等于30";
}

TEST_F(OutOfOrderCPUTest, ResetStatsClearsCountersButPreservesArchitecturalState) {
    writeInstruction(0x0, createITypeInstruction(1, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createITypeInstruction(2, 1, 0x0, 1, 0x13));
    writeInstruction(0x8, createECallInstruction());

    cpu->setPC(0x0);
    for (int i = 0; i < 200 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    ASSERT_TRUE(cpu->isHalted()) << "程序应该已经停机";
    ASSERT_EQ(cpu->getRegister(1), 3u) << "执行结果应保留在架构状态中";

    auto hasNonZeroStat = [](const ICpuInterface::StatsList& stats, const std::string& name) {
        for (const auto& entry : stats) {
            if (entry.name == name) {
                return entry.value > 0;
            }
        }
        return false;
    };

    const uint64_t pcBeforeReset = cpu->getPC();
    const uint64_t instructionsBeforeReset = cpu->getInstructionCount();
    const auto statsBeforeReset = cpu->getStats();
    EXPECT_TRUE(hasNonZeroStat(statsBeforeReset, "instructions"));
    EXPECT_TRUE(hasNonZeroStat(statsBeforeReset, "cycles"));

    cpu->resetStats();

    EXPECT_TRUE(cpu->isHalted());
    EXPECT_EQ(cpu->getPC(), pcBeforeReset);
    EXPECT_EQ(cpu->getInstructionCount(), instructionsBeforeReset);
    EXPECT_EQ(cpu->getRegister(1), 3u);

    const auto statsAfterReset = cpu->getStats();
    for (const auto& entry : statsAfterReset) {
        if (entry.name == "instructions" ||
            entry.name == "cycles" ||
            entry.name == "branch_mispredicts" ||
            entry.name == "pipeline_stalls") {
            EXPECT_EQ(entry.value, 0u) << "统计项 " << entry.name << " 应被清零";
        }
    }
}

TEST_F(OutOfOrderCPUTest, FetchStageFetchesTwoSequentialInstructionsPerCycle) {
    writeInstruction(0x0, createITypeInstruction(1, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createITypeInstruction(2, 0, 0x0, 2, 0x13));
    writeInstruction(0x8, createITypeInstruction(3, 0, 0x0, 3, 0x13));
    writeInstruction(0xC, createECallInstruction());

    auto& state = const_cast<CPUState&>(cpu->getCPUState());
    state.l1i_cache.reset();
    state.l1d_cache.reset();

    cpu->setPC(0x0);
    cpu->step();

    EXPECT_EQ(state.fetch_buffer.size(), 2u) << "第一拍应取两条顺序指令";
    EXPECT_EQ(cpu->getPC(), 0x8u) << "两条32位顺序指令后，下一取指PC应前进8字节";

    auto findStat = [&](const std::string& name) -> uint64_t {
        for (const auto& entry : cpu->getStats()) {
            if (entry.name == name) {
                return entry.value;
            }
        }
        return 0;
    };

    EXPECT_EQ(findStat("cpu.fetch.fetched"), 2u);
    EXPECT_EQ(findStat("cpu.fetch.utilized_slots"), 2u);
    EXPECT_EQ(findStat("cpu.fetch.slots"), 2u);
}

TEST_F(OutOfOrderCPUTest, DecodeStageDecodesTwoInstructionsPerCycleWhenResourcesAllow) {
    writeInstruction(0x0, createITypeInstruction(1, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createITypeInstruction(2, 0, 0x0, 2, 0x13));
    writeInstruction(0x8, createITypeInstruction(3, 0, 0x0, 3, 0x13));
    writeInstruction(0xC, createITypeInstruction(4, 0, 0x0, 4, 0x13));
    writeInstruction(0x10, createECallInstruction());

    auto& state = const_cast<CPUState&>(cpu->getCPUState());
    state.l1i_cache.reset();
    state.l1d_cache.reset();

    cpu->setPC(0x0);
    cpu->step();
    cpu->step();

    ASSERT_NE(state.reorder_buffer, nullptr);
    EXPECT_EQ(state.reorder_buffer->get_free_entry_count(),
              static_cast<size_t>(ReorderBuffer::MAX_ROB_ENTRIES - 2))
        << "第二拍应完成两条指令译码并分配到ROB";

    auto findStat = [&](const std::string& name) -> uint64_t {
        for (const auto& entry : cpu->getStats()) {
            if (entry.name == name) {
                return entry.value;
            }
        }
        return 0;
    };

    EXPECT_EQ(findStat("cpu.decode.decoded"), 2u);
    EXPECT_EQ(findStat("cpu.decode.utilized_slots"), 2u);
    EXPECT_EQ(findStat("cpu.decode.slots"), 4u);
}

TEST_F(OutOfOrderCPUTest, IssueStageIssuesTwoInstructionsPerCycleWhenResourcesAllow) {
    writeInstruction(0x0, createITypeInstruction(1, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createITypeInstruction(2, 0, 0x0, 2, 0x13));
    writeInstruction(0x8, createITypeInstruction(3, 0, 0x0, 3, 0x13));
    writeInstruction(0xC, createITypeInstruction(4, 0, 0x0, 4, 0x13));
    writeInstruction(0x10, createECallInstruction());

    auto& state = const_cast<CPUState&>(cpu->getCPUState());
    state.l1i_cache.reset();
    state.l1d_cache.reset();

    cpu->setPC(0x0);
    cpu->step();
    cpu->step();
    cpu->step();

    ASSERT_NE(state.reservation_station, nullptr);
    EXPECT_EQ(state.reservation_station->get_occupied_entry_count(), 2u)
        << "第三拍应将两条最老指令送入保留站";

    auto findStat = [&](const std::string& name) -> uint64_t {
        for (const auto& entry : cpu->getStats()) {
            if (entry.name == name) {
                return entry.value;
            }
        }
        return 0;
    };

    EXPECT_EQ(findStat("cpu.issue.issued"), 2u);
    EXPECT_EQ(findStat("cpu.issue.utilized_slots"), 2u);
    EXPECT_EQ(findStat("cpu.issue.slots"), 6u);
}

TEST_F(OutOfOrderCPUTest, SameCycleIssueRenameTracksYoungerRawDependency) {
    writeInstruction(0x0, createITypeInstruction(1, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createRTypeInstruction(0x00, 0, 1, 0x0, 2, 0x33));
    writeInstruction(0x8, createECallInstruction());

    auto& state = const_cast<CPUState&>(cpu->getCPUState());
    state.l1i_cache.reset();
    state.l1d_cache.reset();

    cpu->setPC(0x0);
    cpu->step();
    cpu->step();
    cpu->step();

    auto first = state.reorder_buffer->get_entry(0);
    auto second = state.reorder_buffer->get_entry(1);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(first->get_status(), DynamicInst::Status::ISSUED);
    ASSERT_EQ(second->get_status(), DynamicInst::Status::ISSUED);
    EXPECT_NE(first->get_physical_dest(), 0u);
    EXPECT_EQ(second->get_physical_src1(), first->get_physical_dest())
        << "同周期 younger 指令应观察到 older 指令更新后的 rename 映射";
    EXPECT_FALSE(second->is_src1_ready())
        << "older 指令尚未执行完成时，younger RAW 源操作数应保持未就绪";
}

TEST_F(OutOfOrderCPUTest, SerializingSystemInstructionBlocksYoungerIssue) {
    // ADDI  x1, x0, 6
    // ADDI  x2, x0, 7
    // MUL   x3, x1, x2
    // EBREAK
    // ADDI  x4, x0, 99
    writeInstruction(0x0, createITypeInstruction(6, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createITypeInstruction(7, 0, 0x0, 2, 0x13));
    writeInstruction(0x8, createRTypeInstruction(0x01, 2, 1, 0x0, 3, 0x33));
    writeInstruction(0xC, createEBreakInstruction());
    writeInstruction(0x10, createITypeInstruction(99, 0, 0x0, 4, 0x13));

    auto& state = const_cast<CPUState&>(cpu->getCPUState());
    state.l1i_cache.reset();
    state.l1d_cache.reset();

    cpu->setPC(0x0);

    bool saw_ebreak_and_younger = false;
    bool verified_younger_blocked = false;
    for (int i = 0; i < 20 && !cpu->isHalted(); ++i) {
        cpu->step();

        DynamicInstPtr ebreak_inst;
        DynamicInstPtr younger_inst;
        for (int rob_index = 0; rob_index < ReorderBuffer::MAX_ROB_ENTRIES; ++rob_index) {
            if (!state.reorder_buffer->is_entry_valid(rob_index)) {
                continue;
            }
            auto inst = state.reorder_buffer->get_entry(rob_index);
            if (!inst) {
                continue;
            }
            if (inst->get_pc() == 0xC) {
                ebreak_inst = inst;
            } else if (inst->get_pc() == 0x10) {
                younger_inst = inst;
            }
        }

        if (!ebreak_inst || !younger_inst) {
            continue;
        }

        saw_ebreak_and_younger = true;
        EXPECT_FALSE(younger_inst->is_issued() || younger_inst->is_executing() || younger_inst->is_completed())
            << "EBREAK 之后的 younger 指令不应越过序列化边界发射";
        verified_younger_blocked = true;
        break;
    }

    EXPECT_TRUE(saw_ebreak_and_younger) << "测试应观察到 EBREAK 与其后的 younger 指令同时在 ROB 中";
    EXPECT_TRUE(verified_younger_blocked) << "需要验证 younger 指令在 issue 阶段被阻塞";
}

TEST_F(OutOfOrderCPUTest, ExecuteStageDispatchesTwoInstructionsPerCycleWhenUnitsAllow) {
    writeInstruction(0x0, createITypeInstruction(1, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createITypeInstruction(2, 0, 0x0, 2, 0x13));
    writeInstruction(0x8, createITypeInstruction(3, 0, 0x0, 3, 0x13));
    writeInstruction(0xC, createITypeInstruction(4, 0, 0x0, 4, 0x13));
    writeInstruction(0x10, createECallInstruction());

    auto& state = const_cast<CPUState&>(cpu->getCPUState());
    state.l1i_cache.reset();
    state.l1d_cache.reset();

    cpu->setPC(0x0);
    cpu->step();
    cpu->step();
    cpu->step();
    cpu->step();

    size_t busy_alu = 0;
    for (const auto& unit : state.alu_units) {
        if (unit.busy) {
            ++busy_alu;
        }
    }
    EXPECT_EQ(busy_alu, 2u) << "第四拍应把两条 ready ALU 指令同时送入执行单元";

    auto findStat = [&](const std::string& name) -> uint64_t {
        for (const auto& entry : cpu->getStats()) {
            if (entry.name == name) {
                return entry.value;
            }
        }
        return 0;
    };

    EXPECT_EQ(findStat("cpu.execute.dispatched"), 2u);
    EXPECT_EQ(findStat("cpu.execute.dispatch_utilized_slots"), 2u);
    EXPECT_EQ(findStat("cpu.execute.dispatch_slots"), 8u);
}

TEST_F(OutOfOrderCPUTest, MExtensionMulInstruction) {
    // ADDI x1, x0, 6
    // ADDI x2, x0, 7
    // MUL  x3, x1, x2
    // ECALL
    writeInstruction(0x0, createITypeInstruction(6, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createITypeInstruction(7, 0, 0x0, 2, 0x13));
    writeInstruction(0x8, createRTypeInstruction(0x01, 2, 1, 0x0, 3, 0x33));
    writeInstruction(0xC, createECallInstruction());

    cpu->setPC(0x0);

    for (int i = 0; i < 80 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted()) << "程序应该停机";
    EXPECT_EQ(cpu->getRegister(3), 42) << "MUL结果应为42";
}

TEST_F(OutOfOrderCPUTest, AtomicAddWordInstruction) {
    memory->writeWord(0x200, 10);

    // ADDI x1, x0, 0x200
    writeInstruction(0x0, createITypeInstruction(0x200, 0, 0x0, 1, 0x13));
    // ADDI x2, x0, 5
    writeInstruction(0x4, createITypeInstruction(5, 0, 0x0, 2, 0x13));
    // AMOADD.W x3, x2, (x1)
    writeInstruction(0x8, createAMOTypeInstruction(/*funct5=*/0x00, /*rs2=*/2, /*rs1=*/1, /*funct3=*/0x2, /*rd=*/3));
    writeInstruction(0xC, createECallInstruction());

    cpu->setPC(0x0);
    for (int i = 0; i < 80 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_EQ(cpu->getRegister(3), 10) << "AMOADD.W应返回更新前内存值";
    EXPECT_EQ(memory->readWord(0x200), 15u) << "AMOADD.W应把rs2加到内存";
}

TEST_F(OutOfOrderCPUTest, LrScWordInstruction) {
    memory->writeWord(0x204, 0x11);

    // ADDI x1, x0, 0x204
    writeInstruction(0x0, createITypeInstruction(0x204, 0, 0x0, 1, 0x13));
    // ADDI x2, x0, 0x22
    writeInstruction(0x4, createITypeInstruction(0x22, 0, 0x0, 2, 0x13));
    // LR.W x5, (x1)
    writeInstruction(0x8, createAMOTypeInstruction(/*funct5=*/0x02, /*rs2=*/0, /*rs1=*/1, /*funct3=*/0x2, /*rd=*/5));
    // SC.W x6, x2, (x1)
    writeInstruction(0xC, createAMOTypeInstruction(/*funct5=*/0x03, /*rs2=*/2, /*rs1=*/1, /*funct3=*/0x2, /*rd=*/6));
    writeInstruction(0x10, createECallInstruction());

    cpu->setPC(0x0);
    for (int i = 0; i < 120 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_EQ(cpu->getRegister(5), 0x11) << "LR.W应读取旧内存值";
    EXPECT_EQ(cpu->getRegister(6), 0) << "SC.W在预留命中时应返回0";
    EXPECT_EQ(memory->readWord(0x204), 0x22u) << "SC.W成功时应写入新值";
}

TEST_F(OutOfOrderCPUTest, FenceInstructionAsNop) {
    // ADDI  x1, x0, 1
    // FENCE
    // ADDI  x2, x1, 1
    // ECALL
    writeInstruction(0x0, createITypeInstruction(1, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createITypeInstruction(0, 0, 0x0, 0, 0x0F));
    writeInstruction(0x8, createITypeInstruction(1, 1, 0x0, 2, 0x13));
    writeInstruction(0xC, createECallInstruction());

    cpu->setPC(0x0);

    for (int i = 0; i < 80 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted()) << "程序应该停机";
    EXPECT_EQ(cpu->getRegister(2), 2) << "FENCE应按NOP处理，不影响后续计算";
}

TEST_F(OutOfOrderCPUTest, HostCommLoadWaitsForOlderTohostStoreToCommit) {
    memory->setHostCommAddresses(0x100, 0x108);
    memory->write64(0x180, 64);   // SYS_write
    memory->write64(0x188, 1);    // fd=stdout
    memory->write64(0x190, 0x200);
    memory->write64(0x198, 1);    // count=1
    memory->writeByte(0x200, 'A');

    // ADDI x2, x0, 0x100   ; tohost
    // ADDI x3, x0, 0x108   ; fromhost
    // ADDI x4, x0, 0x180   ; magic_mem
    // SD   x4, 0(x2)       ; 触发host syscall
    // LD   x15, 0(x3)      ; 必须等store真正生效后才能读到1
    // ECALL
    writeInstruction(0x0, createITypeInstruction(0x100, 0, 0x0, 2, 0x13));
    writeInstruction(0x4, createITypeInstruction(0x108, 0, 0x0, 3, 0x13));
    writeInstruction(0x8, createITypeInstruction(0x180, 0, 0x0, 4, 0x13));
    writeInstruction(0xC, createSTypeInstruction(0, 4, 2, 0x3, 0x23));
    writeInstruction(0x10, createITypeInstruction(0, 3, 0x3, 15, 0x03));
    writeInstruction(0x14, createECallInstruction());

    cpu->setPC(0x0);
    for (int i = 0; i < 120 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted()) << "程序应该停机";
    EXPECT_EQ(cpu->getRegister(15), 1u) << "fromhost load 应等待更老的 tohost store 生效后再读取";
}

TEST_F(OutOfOrderCPUTest, DetailedStatsIncludeLoadReplayAndForwardingProfile) {
    writeInstruction(0x0, createITypeInstruction(0x100, 0, 0x0, 1, 0x13));  // addi x1, x0, 0x100
    writeInstruction(0x4, createITypeInstruction(0x78, 0, 0x0, 2, 0x13));    // addi x2, x0, 0x78
    writeInstruction(0x8, createSTypeInstruction(0, 2, 1, 0x2, 0x23));       // sw x2, 0(x1)
    writeInstruction(0xC, createITypeInstruction(0, 1, 0x2, 3, 0x03));       // lw x3, 0(x1)
    writeInstruction(0x10, createECallInstruction());

    cpu->setPC(0x0);
    for (int i = 0; i < 400 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    ASSERT_TRUE(cpu->isHalted());
    ASSERT_EQ(cpu->getRegister(3), 0x78u);

    std::ostringstream oss;
    cpu->dumpDetailedStats(oss);
    const std::string stats_text = oss.str();
    EXPECT_NE(stats_text.find("cpu.load_profile.top.begin"), std::string::npos);
    EXPECT_NE(stats_text.find("replay_total="), std::string::npos);
    EXPECT_NE(stats_text.find("speculated_addr_unknown="), std::string::npos);
    EXPECT_NE(stats_text.find("forwarded_full="), std::string::npos);
}

TEST_F(OutOfOrderCPUTest, DetailedStatsIncludeStoreForwardingAndBlockingProfile) {
    writeInstruction(0x0, createITypeInstruction(0x100, 0, 0x0, 1, 0x13));   // addi x1, x0, 0x100
    writeInstruction(0x4, createITypeInstruction(0x1234, 0, 0x0, 2, 0x13));  // addi x2, x0, 0x1234
    writeInstruction(0x8, createSTypeInstruction(0, 2, 1, 0x2, 0x23));       // sw x2, 0(x1)
    writeInstruction(0xC, createITypeInstruction(0, 1, 0x2, 3, 0x03));       // lw x3, 0(x1)
    writeInstruction(0x10, createECallInstruction());

    cpu->setPC(0x0);
    for (int i = 0; i < 400 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    ASSERT_TRUE(cpu->isHalted());

    std::ostringstream oss;
    cpu->dumpDetailedStats(oss);
    const std::string stats_text = oss.str();
    EXPECT_NE(stats_text.find("cpu.store_profile.top.begin"), std::string::npos);
    EXPECT_NE(stats_text.find("forwarded_full="), std::string::npos);
    EXPECT_NE(stats_text.find("blocked_store_buffer_overlap="), std::string::npos);
}

TEST_F(OutOfOrderCPUTest, ReadyStoreAddressAvoidsUnknownStoreReplayForDependentLoad) {
    writeInstruction(0x0, createITypeInstruction(0x100, 0, 0x0, 1, 0x13));  // addi x1, x0, 0x100
    writeInstruction(0x4, createITypeInstruction(0x78, 0, 0x0, 2, 0x13));    // addi x2, x0, 0x78
    writeInstruction(0x8, createSTypeInstruction(0, 2, 1, 0x2, 0x23));       // sw x2, 0(x1)
    writeInstruction(0xC, createITypeInstruction(0, 1, 0x2, 3, 0x03));       // lw x3, 0(x1)
    writeInstruction(0x10, createECallInstruction());

    cpu->setPC(0x0);
    for (int i = 0; i < 400 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    ASSERT_TRUE(cpu->isHalted());
    ASSERT_EQ(cpu->getRegister(3), 0x78u);

    auto statValueByName = [](const ICpuInterface::StatsList& stats, const std::string& name) {
        for (const auto& entry : stats) {
            if (entry.name == name) {
                return entry.value;
            }
        }
        return uint64_t{0};
    };

    const auto stats = cpu->getStats();
    EXPECT_EQ(statValueByName(stats, "cpu.memory.load_replays.rob_store_addr_unknown"), 0u);
}

TEST_F(OutOfOrderCPUTest, PartialStoreMergeAvoidsStoreBufferOverlapReplay) {
    memory->writeWord(0x100, 0xAABBCCDDu);

    writeInstruction(0x0, createITypeInstruction(0x100, 0, 0x0, 1, 0x13));  // addi x1, x0, 0x100
    writeInstruction(0x4, createITypeInstruction(0x78, 0, 0x0, 2, 0x13));   // addi x2, x0, 0x78
    writeInstruction(0x8, createSTypeInstruction(0, 2, 1, 0x0, 0x23));      // sb x2, 0(x1)
    writeInstruction(0xC, createITypeInstruction(0, 1, 0x2, 3, 0x03));      // lw x3, 0(x1)
    writeInstruction(0x10, createECallInstruction());

    cpu->setPC(0x0);
    for (int i = 0; i < 400 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    ASSERT_TRUE(cpu->isHalted());
    ASSERT_EQ(cpu->getRegister(3), 0xFFFFFFFFAABBCC78ULL);

    auto statValueByName = [](const ICpuInterface::StatsList& stats, const std::string& name) {
        for (const auto& entry : stats) {
            if (entry.name == name) {
                return entry.value;
            }
        }
        return uint64_t{0};
    };

    const auto stats = cpu->getStats();
    EXPECT_EQ(statValueByName(stats, "cpu.memory.loads_blocked_by_store"), 0u);
    EXPECT_EQ(statValueByName(stats, "cpu.memory.load_replays.store_buffer_overlap"), 0u);
}

TEST_F(OutOfOrderCPUTest, ReadyStoreValueAvoidsRobOverlapReplayForDependentLoad) {
    writeInstruction(0x0, createITypeInstruction(0x100, 0, 0x0, 1, 0x13));  // addi x1, x0, 0x100
    writeInstruction(0x4, createITypeInstruction(0x78, 0, 0x0, 2, 0x13));   // addi x2, x0, 0x78
    writeInstruction(0x8, createSTypeInstruction(0, 2, 1, 0x2, 0x23));      // sw x2, 0(x1)
    writeInstruction(0xC, createITypeInstruction(0, 1, 0x2, 3, 0x03));      // lw x3, 0(x1)
    writeInstruction(0x10, createECallInstruction());

    cpu->setPC(0x0);
    for (int i = 0; i < 400 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    ASSERT_TRUE(cpu->isHalted());
    ASSERT_EQ(cpu->getRegister(3), 0x78u);

    auto statValueByName = [](const ICpuInterface::StatsList& stats, const std::string& name) {
        for (const auto& entry : stats) {
            if (entry.name == name) {
                return entry.value;
            }
        }
        return uint64_t{0};
    };

    const auto stats = cpu->getStats();
    EXPECT_EQ(statValueByName(stats, "cpu.memory.load_replays.rob_store_overlap"), 0u);
}

TEST_F(OutOfOrderCPUTest, LoadCanSpeculatePastAddressUnknownStoreWhenNoOverlapExists) {
    memory->writeWord(0x100, 0x12345678u);

    // ADDI x13, x0, 20
    // ADDI x14, x0, 16
    // MUL  x1, x13, x14    ; x1 <- 0x140，故意放慢 older store 的地址准备
    // ADDI x11, x0, 0x34
    // ADDI x12, x0, 0x100
    // SW   x11, 0(x1)      ; older store, 地址执行前未知
    // LW   x3, 0(x12)      ; younger load, 应可越过未知地址store推测执行
    // ADDI x17, x0, 93
    // ECALL
    writeInstruction(0x0, createITypeInstruction(20, 0, 0x0, 13, 0x13));
    writeInstruction(0x4, createITypeInstruction(16, 0, 0x0, 14, 0x13));
    writeInstruction(0x8, createRTypeInstruction(0x01, 14, 13, 0x0, 1, 0x33));
    writeInstruction(0xC, createITypeInstruction(0x34, 0, 0x0, 11, 0x13));
    writeInstruction(0x10, createITypeInstruction(0x100, 0, 0x0, 12, 0x13));
    writeInstruction(0x14, createSTypeInstruction(0, 11, 1, 0x2, 0x23));
    writeInstruction(0x18, createITypeInstruction(0, 12, 0x2, 3, 0x03));
    writeInstruction(0x1C, createITypeInstruction(93, 0, 0x0, 17, 0x13));
    writeInstruction(0x20, createECallInstruction());

    cpu->setPC(0x0);
    for (int i = 0; i < 500 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    ASSERT_TRUE(cpu->isHalted());
    EXPECT_EQ(cpu->getRegister(3), 0x12345678u);
    EXPECT_EQ(memory->readWord(0x140), 0x34u);

    auto statValueByName = [](const ICpuInterface::StatsList& stats, const std::string& name) {
        for (const auto& entry : stats) {
            if (entry.name == name) {
                return entry.value;
            }
        }
        return uint64_t{0};
    };

    const auto stats = cpu->getStats();
    EXPECT_GT(statValueByName(stats, "cpu.memory.loads_speculated_addr_unknown"), 0u);
    EXPECT_EQ(statValueByName(stats, "cpu.memory.order_violation_recoveries"), 0u);
}

TEST_F(OutOfOrderCPUTest, BranchMispredictRecoversEarlyAndFlushesWrongPath) {
    // BEQ  x0, x0, +12     ; 初始预测不跳，实际跳到0xC
    // ADDI x2, x0, 99      ; wrong path，必须被提前冲刷
    // ADDI x5, x0, 42      ; wrong path，必须被提前冲刷
    // ADDI x3, x0, 7       ; correct path
    // ADDI x17, x0, 93     ; sys_exit
    // ECALL
    writeInstruction(0x0, createBTypeInstruction(12, 0, 0, 0x0));
    writeInstruction(0x4, createITypeInstruction(99, 0, 0x0, 2, 0x13));
    writeInstruction(0x8, createITypeInstruction(42, 0, 0x0, 5, 0x13));
    writeInstruction(0xC, createITypeInstruction(7, 0, 0x0, 3, 0x13));
    writeInstruction(0x10, createITypeInstruction(93, 0, 0x0, 17, 0x13));
    writeInstruction(0x14, createECallInstruction());

    auto& state = const_cast<CPUState&>(cpu->getCPUState());
    state.l1i_cache.reset();
    state.l1d_cache.reset();

    cpu->setPC(0x0);
    for (int i = 0; i < 5; ++i) {
        cpu->step();
    }

    bool has_wrong_path_pc = false;
    for (int i = 0; i < ReorderBuffer::MAX_ROB_ENTRIES; ++i) {
        if (!state.reorder_buffer->is_entry_valid(i)) {
            continue;
        }
        auto entry = state.reorder_buffer->get_entry(i);
        if (entry && (entry->get_pc() == 0x4 || entry->get_pc() == 0x8)) {
            has_wrong_path_pc = true;
            break;
        }
    }

    EXPECT_FALSE(has_wrong_path_pc) << "分支在 execute 发现误预测后，应提前冲刷 wrong-path ROB 表项";

    for (int i = 0; i < 120 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_TRUE(cpu->isHalted()) << "程序应该停机";
    EXPECT_EQ(cpu->getRegister(2), 0u) << "wrong-path 指令不应提交";
    EXPECT_EQ(cpu->getRegister(5), 0u) << "第二条 wrong-path 指令不应提交";
    EXPECT_EQ(cpu->getRegister(3), 7u) << "correct-path 指令应正常提交";
}

// 测试6：CPU状态重置
TEST_F(OutOfOrderCPUTest, SystemCSRInstructions) {
    // 指令序列:
    // ADDI   x1, x0, 5
    // CSRRW  x2, mstatus(0x300), x1   ; x2 <- old(0), mstatus <- 5
    // CSRRS  x3, mstatus(0x300), x0   ; x3 <- 5, mstatus unchanged
    // CSRRWI x4, mstatus(0x300), 7    ; x4 <- old(5), mstatus <- 7
    // CSRRS  x5, mstatus(0x300), x0   ; x5 <- 7
    // ECALL
    writeInstruction(0x0, createITypeInstruction(5, 0, 0x0, 1, 0x13));
    writeInstruction(0x4, createSystemInstruction(0x300, 1, 0x1, 2));
    writeInstruction(0x8, createSystemInstruction(0x300, 0, 0x2, 3));
    writeInstruction(0xC, createSystemInstruction(0x300, 7, 0x5, 4));
    writeInstruction(0x10, createSystemInstruction(0x300, 0, 0x2, 5));
    writeInstruction(0x14, createECallInstruction());

    cpu->setPC(0x0);

    for (int i = 0; i < 120 && !cpu->isHalted(); ++i) {
        cpu->step();
    }

    EXPECT_EQ(cpu->getRegister(2), 0) << "CSRRW应返回旧CSR值";
    EXPECT_EQ(cpu->getRegister(3), 5) << "CSRRS读取应拿到CSRRW写入后的值";
    EXPECT_EQ(cpu->getRegister(4), 5) << "CSRRWI应返回写入前CSR值";
    EXPECT_EQ(cpu->getRegister(5), 7) << "CSRRS读取应拿到CSRRWI写入后的值";
}

// 测试7：CPU状态重置
TEST_F(OutOfOrderCPUTest, CPUStateReset) {
    // 设置一些状态
    cpu->setRegister(1, 0x12345678);
    cpu->setPC(0x100);
    
    // 执行一些周期
    for (int i = 0; i < 5; ++i) {
        cpu->step();
    }
    
    // 重置CPU
    cpu->reset();
    
    // 验证重置后状态
    EXPECT_EQ(cpu->getPC(), 0) << "重置后PC应该为0";
    EXPECT_EQ(cpu->getRegister(1), 0) << "重置后寄存器应该为0";
    EXPECT_EQ(cpu->getCycleCount(), 0) << "重置后周期计数应该为0";
    EXPECT_EQ(cpu->getInstructionCount(), 0) << "重置后指令计数应该为0";
    EXPECT_FALSE(cpu->isHalted()) << "重置后CPU不应该停机";
}

// 测试8：流水线状态调试
TEST_F(OutOfOrderCPUTest, DebugOutput) {
    // 这个测试主要验证调试输出函数不会崩溃
    cpu->dumpState();
    cpu->dumpRegisters();
    cpu->dumpPipelineState();
    
    // 如果没有崩溃，测试就通过了
    SUCCEED();
}

} // namespace riscv
