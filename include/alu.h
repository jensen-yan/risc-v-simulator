#pragma once

#include "types.h"

namespace riscv {

/**
 * 算术逻辑单元类
 * 执行所有的算术和逻辑运算
 */
class ALU {
public:
    ALU() = default;
    ~ALU() = default;
    
    // 算术运算
    static uint32_t add(uint32_t a, uint32_t b);
    static uint32_t sub(uint32_t a, uint32_t b);
    
    // 逻辑运算
    static uint32_t and_(uint32_t a, uint32_t b);
    static uint32_t or_(uint32_t a, uint32_t b);
    static uint32_t xor_(uint32_t a, uint32_t b);
    
    // 移位运算
    static uint32_t sll(uint32_t a, uint32_t shamt);  // 逻辑左移
    static uint32_t srl(uint32_t a, uint32_t shamt);  // 逻辑右移
    static uint32_t sra(uint32_t a, uint32_t shamt);  // 算术右移
    
    // 比较运算
    static bool slt(int32_t a, int32_t b);            // 有符号小于
    static bool sltu(uint32_t a, uint32_t b);         // 无符号小于
    
    // 分支比较运算
    static bool eq(uint32_t a, uint32_t b);           // 相等
    static bool ne(uint32_t a, uint32_t b);           // 不等
    static bool lt(int32_t a, int32_t b);             // 有符号小于
    static bool ge(int32_t a, int32_t b);             // 有符号大于等于
    static bool ltu(uint32_t a, uint32_t b);          // 无符号小于
    static bool geu(uint32_t a, uint32_t b);          // 无符号大于等于
    
private:
    // 辅助函数
    static uint32_t signExtend(uint32_t value, int bits);
};

} // namespace riscv