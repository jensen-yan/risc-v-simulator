#include <gtest/gtest.h>
#include "memory.h"

using namespace riscv;

class MemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        memory = std::make_unique<Memory>(1024); // 1KB 测试内存
    }
    
    std::unique_ptr<Memory> memory;
};

TEST_F(MemoryTest, BasicByteOperations) {
    // 测试字节读写
    memory->writeByte(0, 0x42);
    EXPECT_EQ(memory->readByte(0), 0x42);
    
    // 测试不同位置
    memory->writeByte(100, 0xFF);
    EXPECT_EQ(memory->readByte(100), 0xFF);
    EXPECT_EQ(memory->readByte(0), 0x42); // 确保其他位置不受影响
}

TEST_F(MemoryTest, HalfWordOperations) {
    // 测试半字读写（小端序）
    memory->writeHalfWord(0, 0x1234);
    EXPECT_EQ(memory->readHalfWord(0), 0x1234);
    
    // 验证小端序存储
    EXPECT_EQ(memory->readByte(0), 0x34);
    EXPECT_EQ(memory->readByte(1), 0x12);
}

TEST_F(MemoryTest, WordOperations) {
    // 测试字读写（小端序）
    memory->writeWord(0, 0x12345678);
    EXPECT_EQ(memory->readWord(0), 0x12345678);
    
    // 验证小端序存储
    EXPECT_EQ(memory->readByte(0), 0x78);
    EXPECT_EQ(memory->readByte(1), 0x56);
    EXPECT_EQ(memory->readByte(2), 0x34);
    EXPECT_EQ(memory->readByte(3), 0x12);
}

TEST_F(MemoryTest, InstructionFetch) {
    // 测试指令获取
    memory->writeWord(0, 0x12345678);
    EXPECT_EQ(memory->fetchInstruction(0), 0x12345678);
    
    // 测试4字节对齐检查
    EXPECT_THROW(memory->fetchInstruction(1), MemoryException);
    EXPECT_THROW(memory->fetchInstruction(2), MemoryException);
    EXPECT_THROW(memory->fetchInstruction(3), MemoryException);
    
    // 对齐的地址应该正常工作
    memory->writeWord(4, 0x87654321);
    EXPECT_EQ(memory->fetchInstruction(4), 0x87654321);
}

TEST_F(MemoryTest, BoundaryChecks) {
    // 测试越界访问
    EXPECT_THROW(memory->readByte(1024), MemoryException);
    EXPECT_THROW(memory->writeByte(1024, 0), MemoryException);
    EXPECT_THROW(memory->readWord(1021), MemoryException); // 读取4字节会越界
}

TEST_F(MemoryTest, ProgramLoading) {
    // 测试程序加载
    std::vector<uint8_t> program = {0x01, 0x02, 0x03, 0x04, 0x05};
    memory->loadProgram(program, 0);
    
    EXPECT_EQ(memory->readByte(0), 0x01);
    EXPECT_EQ(memory->readByte(1), 0x02);
    EXPECT_EQ(memory->readByte(4), 0x05);
    
    // 测试加载到不同起始位置
    memory->loadProgram(program, 100);
    EXPECT_EQ(memory->readByte(100), 0x01);
    EXPECT_EQ(memory->readByte(104), 0x05);
}

TEST_F(MemoryTest, ClearMemory) {
    // 写入一些数据
    memory->writeByte(0, 0xFF);
    memory->writeWord(4, 0x12345678);
    
    // 清除内存
    memory->clear();
    
    // 验证内存已清零
    EXPECT_EQ(memory->readByte(0), 0);
    EXPECT_EQ(memory->readWord(4), 0);
}