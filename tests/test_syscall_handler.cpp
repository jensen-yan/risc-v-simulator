#include <gtest/gtest.h>
#include "system/syscall_handler.h"
#include "common/cpu_interface.h"
#include "core/memory.h"
#include <memory>
#include <sstream>
#include <iostream>

namespace riscv {

// Mock CPU实现，用于测试
class MockCPU : public ICpuInterface {
private:
    uint64_t registers_[32] = {0};
    uint64_t fp_registers_[32] = {0};
    uint64_t pc_ = 0;
    bool halted_ = false;
    uint32_t enabled_extensions_ = 0;

public:
    // 基本CPU接口
    void step() override {}
    void run() override {}
    void reset() override {
        std::fill(registers_, registers_ + 32, 0);
        std::fill(fp_registers_, fp_registers_ + 32, 0);
        pc_ = 0;
        halted_ = false;
    }
    bool isHalted() const override { return halted_; }
    void halt() { halted_ = true; }
    
    // 扩展支持
    void setEnabledExtensions(uint32_t extensions) override { 
        enabled_extensions_ = extensions; 
    }
    uint32_t getEnabledExtensions() const override { 
        return enabled_extensions_; 
    }

    // 寄存器访问
    uint64_t getRegister(RegNum reg) const override { 
        return (reg == 0) ? 0 : registers_[reg]; 
    }
    void setRegister(RegNum reg, uint64_t value) override { 
        if (reg != 0) registers_[reg] = value; 
    }

    uint64_t getFPRegister(RegNum reg) const override { 
        return fp_registers_[reg]; 
    }
    void setFPRegister(RegNum reg, uint64_t value) override { 
        fp_registers_[reg] = value; 
    }

    float getFPRegisterFloat(RegNum reg) const override {
        uint32_t bits = static_cast<uint32_t>(fp_registers_[reg]);
        return *reinterpret_cast<float*>(&bits);
    }
    void setFPRegisterFloat(RegNum reg, float value) override {
        fp_registers_[reg] = *reinterpret_cast<uint32_t*>(&value);
    }

    // PC访问
    uint64_t getPC() const override { return pc_; }
    void setPC(uint64_t pc) override { pc_ = pc; }

    // 计数器（简化实现）
    uint64_t getInstructionCount() const override { return 0; }
    // uint64_t getCycleCount() const override { return 0; }

    // // 性能统计（简化实现）
    // void getPerformanceStats(uint64_t& instructions, uint64_t& cycles, 
    //                        uint64_t& branch_mispredicts, uint64_t& stalls) const override {
    //     instructions = cycles = branch_mispredicts = stalls = 0;
    // }

    // // 调试方法（空实现）
    void dumpState() const override {}
    void dumpRegisters() const override {}
    // void dumpPipelineState() const override {}
};

/**
 * SyscallHandler模块单元测试
 * 当前覆盖率：13.4% -> 目标：70%+
 */
class SyscallHandlerTest : public ::testing::Test {
protected:
    std::shared_ptr<Memory> memory_;
    std::unique_ptr<SyscallHandler> syscall_handler_;
    std::unique_ptr<MockCPU> mock_cpu_;
    
    // 保存标准输出用于重定向测试
    std::streambuf* orig_cout_;
    std::streambuf* orig_cerr_;
    std::ostringstream captured_cout_;
    std::ostringstream captured_cerr_;
    
    void SetUp() override {
        memory_ = std::make_shared<Memory>(8192);  // 8KB内存
        syscall_handler_ = std::make_unique<SyscallHandler>(memory_);
        mock_cpu_ = std::make_unique<MockCPU>();
        
        // 重定向标准输出，用于测试输出
        orig_cout_ = std::cout.rdbuf();
        orig_cerr_ = std::cerr.rdbuf();
    }
    
    void TearDown() override {
        // 恢复标准输出
        std::cout.rdbuf(orig_cout_);
        std::cerr.rdbuf(orig_cerr_);
        
        syscall_handler_.reset();
        mock_cpu_.reset();
        memory_.reset();
    }
    
    // 重定向输出以便测试
    void redirectOutput() {
        captured_cout_.str("");
        captured_cout_.clear();
        captured_cerr_.str("");
        captured_cerr_.clear();
        std::cout.rdbuf(captured_cout_.rdbuf());
        std::cerr.rdbuf(captured_cerr_.rdbuf());
    }
    
    void restoreOutput() {
        std::cout.rdbuf(orig_cout_);
        std::cerr.rdbuf(orig_cerr_);
    }
    
    // 辅助函数：设置系统调用参数
    void setupSyscall(uint64_t syscall_num, uint64_t a0 = 0, uint64_t a1 = 0, 
                     uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0, uint64_t a5 = 0) {
        mock_cpu_->setRegister(17, syscall_num);  // a7 = 系统调用号
        mock_cpu_->setRegister(10, a0);           // a0 = 第一个参数
        mock_cpu_->setRegister(11, a1);           // a1 = 第二个参数
        mock_cpu_->setRegister(12, a2);           // a2 = 第三个参数
        mock_cpu_->setRegister(13, a3);           // a3 = 第四个参数
        mock_cpu_->setRegister(14, a4);           // a4 = 第五个参数
        mock_cpu_->setRegister(15, a5);           // a5 = 第六个参数
    }
    
    // 辅助函数：写入字符串到内存
    void writeStringToMemory(uint64_t addr, const std::string& str) {
        for (size_t i = 0; i < str.length(); ++i) {
            memory_->writeByte(addr + i, static_cast<uint8_t>(str[i]));
        }
        memory_->writeByte(addr + str.length(), 0);  // 空终止符
    }
    
    // 辅助函数：从内存读取字符串
    std::string readStringFromMemory(uint64_t addr, size_t max_len = 1024) {
        std::string result;
        for (size_t i = 0; i < max_len; ++i) {
            uint8_t byte = memory_->readByte(addr + i);
            if (byte == 0) break;
            result += static_cast<char>(byte);
        }
        return result;
    }
};

// ========== 构造函数测试 ==========

TEST_F(SyscallHandlerTest, Constructor) {
    EXPECT_NE(syscall_handler_, nullptr) << "SyscallHandler应该被正确构造";
}

// ========== SYS_EXIT 系统调用测试 ==========

TEST_F(SyscallHandlerTest, SysExitNormalCode) {
    // 设置exit(0)系统调用
    setupSyscall(SyscallHandler::SYS_EXIT, 0);
    
    // 调用系统调用处理器
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    // 验证返回值
    EXPECT_TRUE(should_halt) << "exit系统调用应该返回true表示需要停机";
    
    // 验证返回值寄存器（通常exit不设置返回值，但确保没有意外修改）
    EXPECT_EQ(mock_cpu_->getRegister(10), 0) << "exit后a0寄存器应该保持为0（退出码）";
}

TEST_F(SyscallHandlerTest, SysExitWithErrorCode) {
    // 设置exit(42)系统调用
    setupSyscall(SyscallHandler::SYS_EXIT, 42);
    
    // 调用系统调用处理器
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    // 验证行为
    EXPECT_TRUE(should_halt) << "带错误码的exit系统调用也应该返回true";
    EXPECT_EQ(mock_cpu_->getRegister(10), 42) << "退出码应该保持在a0寄存器中";
}

// ========== SYS_WRITE 系统调用测试 ==========

TEST_F(SyscallHandlerTest, SysWriteToStdout) {
    redirectOutput();
    
    // 准备要写入的字符串
    std::string test_string = "Hello, World!";
    uint64_t string_addr = 0x1000;
    writeStringToMemory(string_addr, test_string);
    
    // 设置write(1, string_addr, length)系统调用
    setupSyscall(SyscallHandler::SYS_WRITE, 
                 SyscallHandler::STDOUT,     // fd = 1 (stdout)
                 string_addr,                // buffer address
                 test_string.length());      // count
    
    // 调用系统调用处理器
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    // 验证行为
    EXPECT_FALSE(should_halt) << "write系统调用不应该导致停机";
    
    // 验证返回值（写入的字节数）
    EXPECT_EQ(mock_cpu_->getRegister(10), test_string.length()) << "应该返回写入的字节数";
    
    restoreOutput();
    
    // 验证输出内容
    EXPECT_EQ(captured_cout_.str(), test_string) << "应该输出正确的字符串到stdout";
}

TEST_F(SyscallHandlerTest, SysWriteToStderr) {
    redirectOutput();
    
    // 准备要写入的字符串
    std::string test_string = "Error message";
    uint64_t string_addr = 0x2000;
    writeStringToMemory(string_addr, test_string);
    
    // 设置write(2, string_addr, length)系统调用
    setupSyscall(SyscallHandler::SYS_WRITE, 
                 SyscallHandler::STDERR,     // fd = 2 (stderr)
                 string_addr,                // buffer address
                 test_string.length());      // count
    
    // 调用系统调用处理器
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    // 验证行为
    EXPECT_FALSE(should_halt) << "write系统调用不应该导致停机";
    EXPECT_EQ(mock_cpu_->getRegister(10), test_string.length()) << "应该返回写入的字节数";
    
    restoreOutput();
    
    // 验证输出到stderr
    EXPECT_EQ(captured_cerr_.str(), test_string) << "应该输出正确的字符串到stderr";
}

TEST_F(SyscallHandlerTest, SysWriteEmptyString) {
    redirectOutput();
    
    // 写入空字符串
    uint64_t string_addr = 0x1000;
    writeStringToMemory(string_addr, "");
    
    setupSyscall(SyscallHandler::SYS_WRITE, SyscallHandler::STDOUT, string_addr, 0);
    
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    EXPECT_FALSE(should_halt) << "写入空字符串不应该导致停机";
    EXPECT_EQ(mock_cpu_->getRegister(10), 0) << "写入0字节应该返回0";
    
    restoreOutput();
    
    EXPECT_EQ(captured_cout_.str(), "") << "应该没有输出";
}

TEST_F(SyscallHandlerTest, SysWriteInvalidFileDescriptor) {
    redirectOutput();
    
    std::string test_string = "Test";
    uint64_t string_addr = 0x1000;
    writeStringToMemory(string_addr, test_string);
    
    // 使用无效的文件描述符
    setupSyscall(SyscallHandler::SYS_WRITE, 999, string_addr, test_string.length());
    
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    EXPECT_FALSE(should_halt) << "无效fd的write不应该导致停机";
    
    // 对于无效fd，通常返回-1（错误）
    int64_t result = static_cast<int64_t>(mock_cpu_->getRegister(10));
    EXPECT_EQ(result, -1) << "无效fd应该返回-1";
    
    restoreOutput();
}

// ========== SYS_READ 系统调用测试 ==========

TEST_F(SyscallHandlerTest, SysReadFromStdin) {
    // 模拟stdin输入 - 这在单元测试中比较困难，我们主要测试接口调用
    uint64_t buffer_addr = 0x2000;
    size_t buffer_size = 100;
    
    setupSyscall(SyscallHandler::SYS_READ, 
                 SyscallHandler::STDIN,      // fd = 0 (stdin)
                 buffer_addr,                // buffer address
                 buffer_size);               // count
    
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    EXPECT_FALSE(should_halt) << "read系统调用不应该导致停机";
    
    // 在实际实现中，read系统调用应该设置返回值
    // 这里我们主要验证调用不会崩溃
    SUCCEED() << "read系统调用成功执行";
}

TEST_F(SyscallHandlerTest, SysReadInvalidFileDescriptor) {
    uint64_t buffer_addr = 0x2000;
    size_t buffer_size = 100;
    
    // 使用无效的文件描述符
    setupSyscall(SyscallHandler::SYS_READ, 999, buffer_addr, buffer_size);
    
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    EXPECT_FALSE(should_halt) << "无效fd的read不应该导致停机";
    
    // 对于无效fd，通常返回-1（错误）
    int64_t result = static_cast<int64_t>(mock_cpu_->getRegister(10));
    EXPECT_EQ(result, -1) << "无效fd应该返回-1";
}

TEST_F(SyscallHandlerTest, SysReadZeroBytes) {
    uint64_t buffer_addr = 0x2000;
    
    setupSyscall(SyscallHandler::SYS_READ, SyscallHandler::STDIN, buffer_addr, 0);
    
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    EXPECT_FALSE(should_halt) << "读取0字节不应该导致停机";
    EXPECT_EQ(mock_cpu_->getRegister(10), 0) << "读取0字节应该返回0";
}

// ========== SYS_BRK 系统调用测试 ==========

TEST_F(SyscallHandlerTest, SysBrkQuery) {
    // brk(0) 用于查询当前断点
    setupSyscall(SyscallHandler::SYS_BRK, 0);
    
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    EXPECT_FALSE(should_halt) << "brk系统调用不应该导致停机";
    
    // brk(0)应该返回当前的程序断点
    uint64_t current_brk = mock_cpu_->getRegister(10);
    EXPECT_GT(current_brk, 0) << "brk(0)应该返回非零的当前断点地址";
}

TEST_F(SyscallHandlerTest, SysBrkSetNewBreak) {
    // 首先查询当前断点
    setupSyscall(SyscallHandler::SYS_BRK, 0);
    syscall_handler_->handleSyscall(mock_cpu_.get());
    uint64_t original_brk = mock_cpu_->getRegister(10);
    
    // 设置新的断点
    uint64_t new_brk = original_brk + 4096;  // 增加4KB
    setupSyscall(SyscallHandler::SYS_BRK, new_brk);
    
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    EXPECT_FALSE(should_halt) << "设置新断点不应该导致停机";
    
    // 验证返回值
    uint64_t returned_brk = mock_cpu_->getRegister(10);
    EXPECT_EQ(returned_brk, new_brk) << "应该返回新设置的断点地址";
}

TEST_F(SyscallHandlerTest, SysBrkReduceBreak) {
    // 查询当前断点
    setupSyscall(SyscallHandler::SYS_BRK, 0);
    syscall_handler_->handleSyscall(mock_cpu_.get());
    uint64_t original_brk = mock_cpu_->getRegister(10);
    
    // 尝试减少断点（如果原始断点足够大）
    if (original_brk > 1024) {
        uint64_t new_brk = original_brk - 512;
        setupSyscall(SyscallHandler::SYS_BRK, new_brk);
        
        bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
        
        EXPECT_FALSE(should_halt) << "减少断点不应该导致停机";
        
        uint64_t returned_brk = mock_cpu_->getRegister(10);
        EXPECT_EQ(returned_brk, new_brk) << "应该能够减少断点";
    } else {
        SUCCEED() << "原始断点太小，跳过减少断点测试";
    }
}

// ========== 未知系统调用测试 ==========

TEST_F(SyscallHandlerTest, UnknownSyscall) {
    redirectOutput();
    
    // 使用一个不存在的系统调用号
    setupSyscall(9999, 1, 2, 3);
    
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    EXPECT_FALSE(should_halt) << "未知系统调用不应该导致停机";
    EXPECT_EQ(mock_cpu_->getRegister(10), 0) << "未知系统调用应该返回0";
    
    restoreOutput();
    
    // 验证错误输出
    EXPECT_NE(captured_cerr_.str().find("不支持的系统调用"), std::string::npos) 
        << "应该输出未知系统调用的错误信息";
}

// ========== 边界条件测试 ==========

TEST_F(SyscallHandlerTest, MaxSyscallNumber) {
    redirectOutput();
    
    // 使用最大可能的系统调用号
    setupSyscall(UINT64_MAX, 0);
    
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    EXPECT_FALSE(should_halt) << "最大系统调用号不应该导致停机";
    EXPECT_EQ(mock_cpu_->getRegister(10), 0) << "无效系统调用应该返回0";
    
    restoreOutput();
}

TEST_F(SyscallHandlerTest, NullPointerAddresses) {
    // 测试空指针地址的write系统调用
    setupSyscall(SyscallHandler::SYS_WRITE, SyscallHandler::STDOUT, 0, 10);
    
    // 这应该不会崩溃，但可能返回错误
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    EXPECT_FALSE(should_halt) << "空指针地址不应该导致停机";
    
    // 验证错误处理
    int64_t result = static_cast<int64_t>(mock_cpu_->getRegister(10));
    EXPECT_LE(result, 0) << "空指针访问应该返回错误或0";
}

// ========== 内存边界测试 ==========

TEST_F(SyscallHandlerTest, WriteOutOfBounds) {
    // 尝试写入超出内存范围的地址
    uint64_t out_of_bounds_addr = 0x100000;  // 超出8KB内存
    
    setupSyscall(SyscallHandler::SYS_WRITE, SyscallHandler::STDOUT, out_of_bounds_addr, 10);
    
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    
    EXPECT_FALSE(should_halt) << "越界访问不应该导致停机";
    
    // 验证错误处理
    int64_t result = static_cast<int64_t>(mock_cpu_->getRegister(10));
    EXPECT_LE(result, 0) << "越界访问应该返回错误";
}

// ========== 多次系统调用测试 ==========

TEST_F(SyscallHandlerTest, MultipleSyscalls) {
    redirectOutput();
    
    // 第一次write
    std::string str1 = "First ";
    uint64_t addr1 = 0x1000;
    writeStringToMemory(addr1, str1);
    setupSyscall(SyscallHandler::SYS_WRITE, SyscallHandler::STDOUT, addr1, str1.length());
    
    bool halt1 = syscall_handler_->handleSyscall(mock_cpu_.get());
    EXPECT_FALSE(halt1);
    EXPECT_EQ(mock_cpu_->getRegister(10), str1.length());
    
    // 第二次write
    std::string str2 = "Second";
    uint64_t addr2 = 0x2000;
    writeStringToMemory(addr2, str2);
    setupSyscall(SyscallHandler::SYS_WRITE, SyscallHandler::STDOUT, addr2, str2.length());
    
    bool halt2 = syscall_handler_->handleSyscall(mock_cpu_.get());
    EXPECT_FALSE(halt2);
    EXPECT_EQ(mock_cpu_->getRegister(10), str2.length());
    
    restoreOutput();
    
    // 验证输出
    EXPECT_EQ(captured_cout_.str(), str1 + str2) << "应该依次输出两个字符串";
}

// ========== 系统调用参数验证测试 ==========

TEST_F(SyscallHandlerTest, SyscallParameterValidation) {
    // 测试各种参数组合
    
    // 测试write的各种参数
    setupSyscall(SyscallHandler::SYS_WRITE, SyscallHandler::STDOUT, 0x1000, 0);
    bool should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    EXPECT_FALSE(should_halt) << "零长度write应该成功";
    EXPECT_EQ(mock_cpu_->getRegister(10), 0) << "零长度write应该返回0";
    
    // 测试read的各种参数
    setupSyscall(SyscallHandler::SYS_READ, SyscallHandler::STDIN, 0x1000, 0);
    should_halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    EXPECT_FALSE(should_halt) << "零长度read应该成功";
    EXPECT_EQ(mock_cpu_->getRegister(10), 0) << "零长度read应该返回0";
}

// ========== 系统调用序列测试 ==========

TEST_F(SyscallHandlerTest, SyscallSequence) {
    redirectOutput();
    
    // 执行一系列系统调用：brk -> write -> brk -> exit
    
    // 1. 查询初始brk
    setupSyscall(SyscallHandler::SYS_BRK, 0);
    bool halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    EXPECT_FALSE(halt);
    uint64_t initial_brk = mock_cpu_->getRegister(10);
    
    // 2. 输出消息
    std::string msg = "About to exit";
    uint64_t msg_addr = 0x1000;
    writeStringToMemory(msg_addr, msg);
    setupSyscall(SyscallHandler::SYS_WRITE, SyscallHandler::STDOUT, msg_addr, msg.length());
    halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    EXPECT_FALSE(halt);
    
    // 3. 增加brk
    setupSyscall(SyscallHandler::SYS_BRK, initial_brk + 1024);
    halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    EXPECT_FALSE(halt);
    
    // 4. 最后exit
    setupSyscall(SyscallHandler::SYS_EXIT, 0);
    halt = syscall_handler_->handleSyscall(mock_cpu_.get());
    EXPECT_TRUE(halt) << "最后的exit应该导致停机";
    
    restoreOutput();
    
    EXPECT_EQ(captured_cout_.str(), msg) << "应该输出预期的消息";
}

} // namespace riscv