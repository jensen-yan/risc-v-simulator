#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <memory>
#include "cpu/inorder/cpu.h"
#include "cpu/ooo/ooo_cpu.h"
#include "core/decoder.h"
#include "core/memory.h"
#include "system/simulator.h"

using namespace riscv;

// 分析mret指令的编码
void analyze_mret_encoding() {
    std::cout << "=== MRET指令编码分析 ===" << std::endl;
    
    // MRET指令的机器码是 0x30200073
    // 格式: SYSTEM指令，funct3=0, imm=0x302
    uint32_t mret_encoding = 0x30200073;
    
    std::cout << "MRET机器码: 0x" << std::hex << std::setw(8) << std::setfill('0') << mret_encoding << std::endl;
    
    // 解码mret指令
    Decoder decoder;
    try {
        DecodedInstruction decoded = decoder.decode(mret_encoding, 0xFFFFFFFF);
        std::cout << "解码结果:" << std::endl;
        std::cout << "  opcode: " << static_cast<int>(decoded.opcode) << std::endl;
        std::cout << "  type: " << static_cast<int>(decoded.type) << std::endl;
        std::cout << "  funct3: " << static_cast<int>(decoded.funct3) << std::endl;
        std::cout << "  rd: " << static_cast<int>(decoded.rd) << std::endl;
        std::cout << "  rs1: " << static_cast<int>(decoded.rs1) << std::endl;
        std::cout << "  rs2: " << static_cast<int>(decoded.rs2) << std::endl;
        std::cout << "  imm: 0x" << std::hex << decoded.imm << std::endl;
        
        // 检查是否为SYSTEM指令
        if (decoded.opcode == Opcode::SYSTEM) {
            std::cout << "  这是SYSTEM指令" << std::endl;
            if (decoded.funct3 == Funct3::ECALL_EBREAK && decoded.imm == 0x302) {
                std::cout << "  这是MRET指令" << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cout << "解码失败: " << e.what() << std::endl;
    }
}

// 测试顺序CPU中的mret指令
void test_inorder_mret() {
    std::cout << "\n=== 顺序CPU MRET指令测试 ===" << std::endl;
    
    auto memory = std::make_shared<Memory>();
    CPU inorder_cpu(memory);
    
    // 设置一些初始值
    inorder_cpu.setRegister(10, 0x12345678);  // x10 = 0x12345678
    
    // 在内存中写入mret指令
    uint32_t mret_encoding = 0x30200073;
    memory->writeWord(0x1000, mret_encoding);
    
    // 设置PC
    inorder_cpu.setPC(0x1000);
    
    std::cout << "执行前:" << std::endl;
    std::cout << "  PC: 0x" << std::hex << inorder_cpu.getPC() << std::endl;
    std::cout << "  x10: 0x" << std::hex << inorder_cpu.getRegister(10) << std::endl;
    
    // 执行一步
    try {
        inorder_cpu.step();
    } catch (const std::exception& e) {
        std::cout << "执行异常: " << e.what() << std::endl;
    }
    
    std::cout << "执行后:" << std::endl;
    std::cout << "  PC: 0x" << std::hex << inorder_cpu.getPC() << std::endl;
    std::cout << "  x10: 0x" << std::hex << inorder_cpu.getRegister(10) << std::endl;
}

// 测试乱序CPU中的mret指令
void test_ooo_mret() {
    std::cout << "\n=== 乱序CPU MRET指令测试 ===" << std::endl;
    
    auto memory = std::make_shared<Memory>();
    OutOfOrderCPU ooo_cpu(memory);
    
    // 设置一些初始值
    ooo_cpu.setRegister(10, 0x12345678);  // x10 = 0x12345678
    
    // 在内存中写入mret指令
    uint32_t mret_encoding = 0x30200073;
    memory->writeWord(0x1000, mret_encoding);
    
    // 设置PC
    ooo_cpu.setPC(0x1000);
    
    std::cout << "执行前:" << std::endl;
    std::cout << "  PC: 0x" << std::hex << ooo_cpu.getPC() << std::endl;
    std::cout << "  x10: 0x" << std::hex << ooo_cpu.getRegister(10) << std::endl;
    
    // 执行一步
    try {
        ooo_cpu.step();
    } catch (const std::exception& e) {
        std::cout << "执行异常: " << e.what() << std::endl;
    }
    
    std::cout << "执行后:" << std::endl;
    std::cout << "  PC: 0x" << std::hex << ooo_cpu.getPC() << std::endl;
    std::cout << "  x10: 0x" << std::hex << ooo_cpu.getRegister(10) << std::endl;
}

// 检查系统指令的解码行为
void check_system_instruction_handling() {
    std::cout << "\n=== SYSTEM指令处理检查 ===" << std::endl;
    
    Decoder decoder;
    
    // 检查不同的SYSTEM指令
    uint32_t instructions[] = {
        0x00000073,  // ECALL
        0x00100073,  // EBREAK
        0x30200073,  // MRET
        0x10500073,  // WFI
    };
    
    for (uint32_t encoding : instructions) {
        try {
            DecodedInstruction decoded = decoder.decode(encoding, 0xFFFFFFFF);
            std::cout << "指令 0x" << std::hex << std::setw(8) << std::setfill('0') << encoding << ": ";
            std::cout << "opcode=" << static_cast<int>(decoded.opcode);
            std::cout << " type=" << static_cast<int>(decoded.type);
            std::cout << " rd=" << static_cast<int>(decoded.rd);
            std::cout << " imm=0x" << std::hex << decoded.imm << std::endl;
        } catch (const std::exception& e) {
            std::cout << "解码失败: " << e.what() << std::endl;
        }
    }
}

int main() {
    analyze_mret_encoding();
    check_system_instruction_handling();
    test_inorder_mret();
    test_ooo_mret();
    
    return 0;
}