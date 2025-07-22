// types.cpp - 基础数据类型实现
#include "common/types.h"

namespace riscv {

void DecodedInstruction::initialize_execution_properties() {
    // 重置异常状态
    has_decode_exception = false;
    decode_exception_msg.clear();
    
    // 初始化内存访问属性
    if (opcode == Opcode::LOAD) {
        // 根据funct3确定加载指令的访问大小和符号扩展
        switch (funct3) {
            case static_cast<Funct3>(0): // LB
                memory_access_size = 1;
                is_signed_load = true;
                break;
            case static_cast<Funct3>(1): // LH
                memory_access_size = 2;
                is_signed_load = true;
                break;
            case static_cast<Funct3>(2): // LW
                memory_access_size = 4;
                is_signed_load = true; // LW符号扩展到32位（但对32位系统无影响）
                break;
            case static_cast<Funct3>(4): // LBU
                memory_access_size = 1;
                is_signed_load = false;
                break;
            case static_cast<Funct3>(5): // LHU
                memory_access_size = 2;
                is_signed_load = false;
                break;
            default:
                // 非法的Load指令funct3值 (3, 6, 7等)
                memory_access_size = 0;
                is_signed_load = false;
                has_decode_exception = true;
                decode_exception_msg = "非法的Load指令funct3值: " + std::to_string(static_cast<int>(funct3));
                break;
        }
        execution_cycles = 2; // 加载指令需要2个周期
    } else if (opcode == Opcode::STORE) {
        // 根据funct3确定存储指令的访问大小
        switch (funct3) {
            case static_cast<Funct3>(0): // SB
                memory_access_size = 1;
                break;
            case static_cast<Funct3>(1): // SH
                memory_access_size = 2;
                break;
            case static_cast<Funct3>(2): // SW
                memory_access_size = 4;
                break;
            default:
                // 非法的Store指令funct3值 (3, 4, 5, 6, 7等)
                memory_access_size = 0;
                has_decode_exception = true;
                decode_exception_msg = "非法的Store指令funct3值: " + std::to_string(static_cast<int>(funct3));
                break;
        }
        is_signed_load = false; // 存储指令不涉及符号扩展
        execution_cycles = 1;   // 存储指令需要1个周期
    } else {
        // 非内存指令
        memory_access_size = 0;
        is_signed_load = false;
        execution_cycles = 1; // 默认1个周期
    }
}

} // namespace riscv
