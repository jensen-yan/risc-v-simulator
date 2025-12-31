#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <memory>

namespace riscv {

class Memory;

/**
 * ELF文件解析器和加载器
 * 支持32位/64位RISC-V ELF文件
 */
class ElfLoader {
public:
    struct ProgramSegment {
        Address virtualAddr;    // 虚拟地址
        Address physicalAddr;   // 物理地址
        size_t fileSize;        // 文件中的大小
        size_t memorySize;      // 内存中的大小
        std::vector<uint8_t> data;  // 段数据
        bool executable;        // 是否可执行
        bool writable;          // 是否可写
        bool readable;          // 是否可读
    };

    struct ElfInfo {
        Address entryPoint;     // 程序入口点
        std::vector<ProgramSegment> segments;  // 程序段
        bool isValid;           // 是否有效的ELF文件
    };

    /**
     * 加载ELF文件到内存
     * @param filename ELF文件路径
     * @param memory 内存对象
     * @return ELF信息，包含入口点等
     */
    static ElfInfo loadElfFile(const std::string& filename, std::shared_ptr<Memory> memory);

    /**
     * 验证ELF文件头
     * @param data 文件数据
     * @return 是否为有效的RISC-V ELF文件
     */
    static bool validateElfHeader(const std::vector<uint8_t>& data);

    /**
     * 获取程序入口点
     * @param data 文件数据
     * @return 入口点地址
     */
    static Address getEntryPoint(const std::vector<uint8_t>& data);

private:
    // 统一的ELF文件头结构（字段使用64位表示）
    struct ElfHeader {
        uint8_t e_ident[16];    // ELF标识
        uint16_t e_type;        // 文件类型
        uint16_t e_machine;     // 机器类型
        uint32_t e_version;     // 版本
        uint64_t e_entry;       // 入口点
        uint64_t e_phoff;       // 程序头表偏移
        uint64_t e_shoff;       // 节头表偏移
        uint32_t e_flags;       // 处理器特定标志
        uint16_t e_ehsize;      // ELF头大小
        uint16_t e_phentsize;   // 程序头条目大小
        uint16_t e_phnum;       // 程序头条目数量
        uint16_t e_shentsize;   // 节头条目大小
        uint16_t e_shnum;       // 节头条目数量
        uint16_t e_shstrndx;    // 节头字符串表索引
    };

    // 统一的程序头结构（字段使用64位表示）
    struct ProgramHeader {
        uint32_t p_type;        // 段类型
        uint32_t p_flags;       // 段标志
        uint64_t p_offset;      // 文件偏移
        uint64_t p_vaddr;       // 虚拟地址
        uint64_t p_paddr;       // 物理地址
        uint64_t p_filesz;      // 文件中的大小
        uint64_t p_memsz;       // 内存中的大小
        uint64_t p_align;       // 对齐
    };

    // ELF常量
    static constexpr uint32_t ELF_MAGIC = 0x464C457F;  // 0x7F + "ELF"
    static constexpr uint8_t ELFCLASS32 = 1;           // 32位
    static constexpr uint8_t ELFCLASS64 = 2;           // 64位
    static constexpr uint8_t ELFDATA2LSB = 1;          // 小端
    static constexpr uint16_t EM_RISCV = 243;          // RISC-V架构
    static constexpr uint16_t ET_EXEC = 2;             // 可执行文件
    static constexpr uint32_t PT_LOAD = 1;             // 可加载段
    static constexpr uint32_t PF_X = 1;                // 可执行
    static constexpr uint32_t PF_W = 2;                // 可写
    static constexpr uint32_t PF_R = 4;                // 可读

    // 辅助方法
    static std::vector<uint8_t> loadFile(const std::string& filename);
    static ElfHeader parseElfHeader(const std::vector<uint8_t>& data);
    static ProgramHeader parseProgramHeader(const std::vector<uint8_t>& data, size_t offset);
    static uint64_t read64(const std::vector<uint8_t>& data, size_t offset);
    static uint32_t read32(const std::vector<uint8_t>& data, size_t offset);
    static uint16_t read16(const std::vector<uint8_t>& data, size_t offset);
};

} // namespace riscv
