#include "alu.h"

namespace riscv {

uint32_t ALU::add(uint32_t a, uint32_t b) {
    return a + b;
}

uint32_t ALU::sub(uint32_t a, uint32_t b) {
    return a - b;
}

uint32_t ALU::and_(uint32_t a, uint32_t b) {
    return a & b;
}

uint32_t ALU::or_(uint32_t a, uint32_t b) {
    return a | b;
}

uint32_t ALU::xor_(uint32_t a, uint32_t b) {
    return a ^ b;
}

uint32_t ALU::sll(uint32_t a, uint32_t shamt) {
    return a << (shamt & 0x1F);
}

uint32_t ALU::srl(uint32_t a, uint32_t shamt) {
    return a >> (shamt & 0x1F);
}

uint32_t ALU::sra(uint32_t a, uint32_t shamt) {
    return static_cast<uint32_t>(static_cast<int32_t>(a) >> (shamt & 0x1F));
}

bool ALU::slt(int32_t a, int32_t b) {
    return a < b;
}

bool ALU::sltu(uint32_t a, uint32_t b) {
    return a < b;
}

bool ALU::eq(uint32_t a, uint32_t b) {
    return a == b;
}

bool ALU::ne(uint32_t a, uint32_t b) {
    return a != b;
}

bool ALU::lt(int32_t a, int32_t b) {
    return a < b;
}

bool ALU::ge(int32_t a, int32_t b) {
    return a >= b;
}

bool ALU::ltu(uint32_t a, uint32_t b) {
    return a < b;
}

bool ALU::geu(uint32_t a, uint32_t b) {
    return a >= b;
}

uint32_t ALU::signExtend(uint32_t value, int bits) {
    int32_t mask = (1 << bits) - 1;
    int32_t signBit = 1 << (bits - 1);
    return (value & mask) | (((value & signBit) != 0) ? ~mask : 0);
}

} // namespace riscv