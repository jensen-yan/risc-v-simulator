#include <gtest/gtest.h>
#include "cpu/ooo/store_buffer.h"
#include "cpu/ooo/dynamic_inst.h"
#include "common/types.h"

using namespace riscv;

class StoreBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        store_buffer = std::make_unique<StoreBuffer>();
    }

    void TearDown() override {
        store_buffer.reset();
    }

    // 创建测试用的DynamicInst对象
    DynamicInstPtr createTestDynamicInst(uint64_t instruction_id, uint32_t pc) {
        // 创建一个简单的Store指令用于测试
        DecodedInstruction decoded_inst;
        decoded_inst.type = InstructionType::S_TYPE;
        decoded_inst.opcode = Opcode::STORE;  // Store指令的opcode
        decoded_inst.funct3 = Funct3::SW;     // SW指令
        decoded_inst.rs1 = 1;                 // 基址寄存器
        decoded_inst.rs2 = 2;                 // 源寄存器
        decoded_inst.imm = 0;                 // 偏移量
        
        return create_dynamic_inst(decoded_inst, pc, instruction_id);
    }

    std::unique_ptr<StoreBuffer> store_buffer;
};

// 测试基本的Store条目添加
TEST_F(StoreBufferTest, AddStoreEntry) {
    uint32_t address = 0x1000;
    uint32_t value = 0x12345678;
    uint8_t size = 4;
    uint64_t instruction_id = 1;
    uint32_t pc = 0x80000000;

    auto instruction = createTestDynamicInst(instruction_id, pc);
    
    // 添加Store条目不应抛出异常
    EXPECT_NO_THROW(store_buffer->add_store(instruction, address, value, size));
}

// 测试Store-to-Load Forwarding - 完全匹配
TEST_F(StoreBufferTest, ForwardingExactMatch) {
    uint32_t address = 0x1000;
    uint32_t store_value = 0x12345678;
    uint8_t size = 4;
    uint64_t instruction_id = 1;
    uint32_t pc = 0x80000000;

    auto instruction = createTestDynamicInst(instruction_id, pc);
    
    // 添加Store条目
    store_buffer->add_store(instruction, address, store_value, size);

    // 尝试Load相同地址和大小
    uint32_t load_result;
    bool forwarded = store_buffer->forward_load(address, size, load_result);

    EXPECT_TRUE(forwarded);
    EXPECT_EQ(load_result, store_value);
}

// 测试Store-to-Load Forwarding - 字节访问
TEST_F(StoreBufferTest, ForwardingByteAccess) {
    uint32_t address = 0x1000;
    uint32_t store_value = 0x12345678;  // 存储字
    uint8_t store_size = 4;
    uint64_t instruction_id = 1;
    uint32_t pc = 0x80000000;

    auto instruction = createTestDynamicInst(instruction_id, pc);
    
    // 添加Store条目（字存储）
    store_buffer->add_store(instruction, address, store_value, store_size);

    // 尝试Load字节（地址+0）
    uint32_t load_result;
    bool forwarded = store_buffer->forward_load(address, 1, load_result);

    EXPECT_TRUE(forwarded);
    EXPECT_EQ(load_result, 0x78);  // 小端序，最低字节

    // 尝试Load字节（地址+1）
    forwarded = store_buffer->forward_load(address + 1, 1, load_result);
    EXPECT_TRUE(forwarded);
    EXPECT_EQ(load_result, 0x56);  // 第二个字节

    // 尝试Load字节（地址+2）
    forwarded = store_buffer->forward_load(address + 2, 1, load_result);
    EXPECT_TRUE(forwarded);
    EXPECT_EQ(load_result, 0x34);  // 第三个字节

    // 尝试Load字节（地址+3）
    forwarded = store_buffer->forward_load(address + 3, 1, load_result);
    EXPECT_TRUE(forwarded);
    EXPECT_EQ(load_result, 0x12);  // 最高字节
}

// 测试Store-to-Load Forwarding - 不匹配
TEST_F(StoreBufferTest, ForwardingNoMatch) {
    uint32_t store_address = 0x1000;
    uint32_t store_value = 0x12345678;
    uint8_t store_size = 4;
    uint64_t instruction_id = 1;
    uint32_t pc = 0x80000000;

    auto instruction = createTestDynamicInst(instruction_id, pc);
    
    // 添加Store条目
    store_buffer->add_store(instruction, store_address, store_value, store_size);

    // 尝试Load不同地址
    uint32_t load_result;
    bool forwarded = store_buffer->forward_load(0x2000, 4, load_result);

    EXPECT_FALSE(forwarded);
}

// 测试Store条目退休
TEST_F(StoreBufferTest, RetireStores) {
    uint32_t address1 = 0x1000;
    uint32_t address2 = 0x2000;
    uint32_t value = 0x12345678;
    uint8_t size = 4;
    uint32_t pc = 0x80000000;

    auto instruction1 = createTestDynamicInst(1, pc);
    auto instruction2 = createTestDynamicInst(2, pc);
    
    // 添加两个Store条目
    store_buffer->add_store(instruction1, address1, value, size);
    store_buffer->add_store(instruction2, address2, value, size);

    // 两个Store都应该能转发
    uint32_t load_result;
    EXPECT_TRUE(store_buffer->forward_load(address1, size, load_result));
    EXPECT_TRUE(store_buffer->forward_load(address2, size, load_result));

    // 退休指令ID <= 1的Store
    store_buffer->retire_stores_before(1);

    // 第一个Store应该被退休，第二个仍然存在
    EXPECT_FALSE(store_buffer->forward_load(address1, size, load_result));
    EXPECT_TRUE(store_buffer->forward_load(address2, size, load_result));

    // 退休所有Store
    store_buffer->retire_stores_before(2);

    // 两个Store都应该被退休
    EXPECT_FALSE(store_buffer->forward_load(address1, size, load_result));
    EXPECT_FALSE(store_buffer->forward_load(address2, size, load_result));
}

// 测试Store Buffer循环覆盖
TEST_F(StoreBufferTest, CircularOverwrite) {
    uint32_t base_address = 0x1000;
    uint32_t value = 0x12345678;
    uint8_t size = 4;
    uint32_t pc = 0x80000000;

    // 添加超过Buffer容量的Store条目（假设容量为8）
    for (int i = 0; i < 10; ++i) {
        auto instruction = createTestDynamicInst(i + 1, pc);
        store_buffer->add_store(instruction, base_address + i * 4, value + i, size);
    }

    // 最早的Store条目应该被覆盖
    uint32_t load_result;
    
    // 最新的几个条目应该仍然存在
    EXPECT_TRUE(store_buffer->forward_load(base_address + 9 * 4, size, load_result));
    EXPECT_EQ(load_result, value + 9);
    
    EXPECT_TRUE(store_buffer->forward_load(base_address + 8 * 4, size, load_result));
    EXPECT_EQ(load_result, value + 8);
}

// 测试刷新操作
TEST_F(StoreBufferTest, FlushBuffer) {
    uint32_t address = 0x1000;
    uint32_t value = 0x12345678;
    uint8_t size = 4;
    uint64_t instruction_id = 1;
    uint32_t pc = 0x80000000;

    auto instruction = createTestDynamicInst(instruction_id, pc);
    
    // 添加Store条目
    store_buffer->add_store(instruction, address, value, size);

    // 验证Store条目存在
    uint32_t load_result;
    EXPECT_TRUE(store_buffer->forward_load(address, size, load_result));

    // 刷新Buffer
    store_buffer->flush();

    // Store条目应该被清除
    EXPECT_FALSE(store_buffer->forward_load(address, size, load_result));
}

// 测试多次写入同一地址
TEST_F(StoreBufferTest, MultipleWritesToSameAddress) {
    uint32_t address = 0x1000;
    uint8_t size = 4;
    uint32_t pc = 0x80000000;

    auto instruction1 = createTestDynamicInst(1, pc);
    auto instruction2 = createTestDynamicInst(2, pc);
    auto instruction3 = createTestDynamicInst(3, pc);
    
    // 添加多次写入到同一地址
    store_buffer->add_store(instruction1, address, 0x11111111, size);
    store_buffer->add_store(instruction2, address, 0x22222222, size);
    store_buffer->add_store(instruction3, address, 0x33333333, size);

    // 应该转发最新的值
    uint32_t load_result;
    bool forwarded = store_buffer->forward_load(address, size, load_result);

    EXPECT_TRUE(forwarded);
    EXPECT_EQ(load_result, 0x33333333);  // 最新的值
}

// 测试地址重叠但无法转发的情况
TEST_F(StoreBufferTest, OverlapNoForwarding) {
    uint32_t store_address = 0x1000;
    uint32_t store_value = 0x12345678;
    uint8_t store_size = 1;  // 只存储一个字节
    uint64_t instruction_id = 1;
    uint32_t pc = 0x80000000;

    auto instruction = createTestDynamicInst(instruction_id, pc);
    
    // 添加字节Store
    store_buffer->add_store(instruction, store_address, store_value, store_size);

    // 尝试Load字（覆盖更大范围）
    uint32_t load_result;
    bool forwarded = store_buffer->forward_load(store_address, 4, load_result);

    // 应该无法转发，因为Load需要的数据超出了Store的范围
    EXPECT_FALSE(forwarded);
} 