#include "system/elf_loader.h"
#include "core/memory.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <limits>

namespace riscv {

ElfLoader::ElfInfo ElfLoader::loadElfFile(const std::string& filename, std::shared_ptr<Memory> memory) {
    ElfInfo info;
    info.isValid = false;

    try {
        // 加载文件
        auto data = loadFile(filename);
        if (data.empty()) {
            std::cerr << "无法加载文件: " << filename << std::endl;
            return info;
        }

        // 验证ELF文件头
        if (!validateElfHeader(data)) {
            std::cerr << "无效的ELF文件格式" << std::endl;
            return info;
        }

        // 解析ELF文件头
        ElfHeader header = parseElfHeader(data);
        info.entryPoint = header.e_entry;

        std::cout << "ELF文件信息:" << std::endl;
        std::cout << "  入口点: 0x" << std::hex << info.entryPoint << std::dec << std::endl;
        std::cout << "  程序头数量: " << header.e_phnum << std::endl;

        // 解析程序头表
        for (int i = 0; i < header.e_phnum; i++) {
            size_t phOffset = header.e_phoff + i * header.e_phentsize;
            ProgramHeader ph = parseProgramHeader(data, phOffset);

            // 只处理可加载段
            if (ph.p_type == PT_LOAD) {
                ProgramSegment segment;
                segment.virtualAddr = ph.p_vaddr;
                segment.physicalAddr = ph.p_paddr;
                segment.fileSize = ph.p_filesz;
                segment.memorySize = ph.p_memsz;
                segment.executable = (ph.p_flags & PF_X) != 0;
                segment.writable = (ph.p_flags & PF_W) != 0;
                segment.readable = (ph.p_flags & PF_R) != 0;

                // 提取段数据
                if (ph.p_filesz > 0) {
                    segment.data.resize(ph.p_filesz);
                    std::memcpy(segment.data.data(), data.data() + ph.p_offset, ph.p_filesz);
                }

                // 加载段到内存
                memory->loadProgram(segment.data, segment.virtualAddr);
                
                // 如果内存中的大小大于文件大小，需要清零剩余部分（BSS段）
                if (segment.memorySize > segment.fileSize) {
                    for (size_t j = segment.fileSize; j < segment.memorySize; j++) {
                        memory->writeByte(segment.virtualAddr + j, 0);
                    }
                }

                info.segments.push_back(segment);

                std::cout << "  加载段: 0x" << std::hex << segment.virtualAddr 
                          << " 大小: " << std::dec << segment.fileSize
                          << " 内存大小: " << segment.memorySize
                          << " 权限: " << (segment.readable ? "R" : "-")
                          << (segment.writable ? "W" : "-")
                          << (segment.executable ? "X" : "-") << std::endl;
            }
        }

        info.isValid = true;
    } catch (const std::exception& e) {
        std::cerr << "加载ELF文件失败: " << e.what() << std::endl;
    }

    return info;
}

bool ElfLoader::validateElfHeader(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(ElfHeader)) {
        return false;
    }

    // 检查ELF魔数
    if (read32(data, 0) != ELF_MAGIC) {
        return false;
    }

    // 检查32位或64位
    if (data[4] != ELFCLASS32 && data[4] != ELFCLASS64) {
        return false;
    }

    // 检查小端
    if (data[5] != ELFDATA2LSB) {
        return false;
    }

    // 检查版本
    if (data[6] != 1) {
        return false;
    }

    // 检查机器类型
    uint16_t machine = read16(data, 18);
    if (machine != EM_RISCV) {
        return false;
    }

    // 检查文件类型
    uint16_t type = read16(data, 16);
    if (type != ET_EXEC) {
        return false;
    }

    return true;
}

Address ElfLoader::getEntryPoint(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(ElfHeader)) {
        return 0;
    }
    if (data[4] == ELFCLASS32) {
        return read32(data, 24);
    }
    return read64(data, 24);
}

size_t ElfLoader::getRequiredMemorySize(const std::string& filename,
                                        size_t minSize,
                                        size_t stackReserve) {
    try {
        auto data = loadFile(filename);
        if (!validateElfHeader(data)) {
            return minSize;
        }

        ElfHeader header = parseElfHeader(data);
        uint64_t maxEnd = 0;

        for (int i = 0; i < header.e_phnum; i++) {
            size_t phOffset = header.e_phoff + i * header.e_phentsize;
            ProgramHeader ph = parseProgramHeader(data, phOffset);
            if (ph.p_type != PT_LOAD) {
                continue;
            }
            uint64_t end = ph.p_vaddr + ph.p_memsz;
            if (end > maxEnd) {
                maxEnd = end;
            }
        }

        uint64_t reserve = stackReserve;
        uint64_t total = maxEnd + reserve;
        uint64_t maxSize = static_cast<uint64_t>(std::numeric_limits<size_t>::max());
        if (total < maxEnd || total > maxSize) {
            return minSize;
        }

        size_t recommended = static_cast<size_t>(total);
        return recommended > minSize ? recommended : minSize;
    } catch (const std::exception&) {
        return minSize;
    }
}

std::vector<uint8_t> ElfLoader::loadFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw SimulatorException("无法打开文件: " + filename);
    }

    // 获取文件大小
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize == 0) {
        throw SimulatorException("文件为空: " + filename);
    }

    // 读取文件内容
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);

    if (!file.good()) {
        throw SimulatorException("读取文件失败: " + filename);
    }

    return data;
}

ElfLoader::ElfHeader ElfLoader::parseElfHeader(const std::vector<uint8_t>& data) {
    ElfHeader header;
    
    std::memcpy(header.e_ident, data.data(), 16);
    header.e_type = read16(data, 16);
    header.e_machine = read16(data, 18);
    header.e_version = read32(data, 20);
    if (data[4] == ELFCLASS32) {
        header.e_entry = read32(data, 24);
        header.e_phoff = read32(data, 28);
        header.e_shoff = read32(data, 32);
        header.e_flags = read32(data, 36);
        header.e_ehsize = read16(data, 40);
        header.e_phentsize = read16(data, 42);
        header.e_phnum = read16(data, 44);
        header.e_shentsize = read16(data, 46);
        header.e_shnum = read16(data, 48);
        header.e_shstrndx = read16(data, 50);
    } else {
        header.e_entry = read64(data, 24);
        header.e_phoff = read64(data, 32);
        header.e_shoff = read64(data, 40);
        header.e_flags = read32(data, 48);
        header.e_ehsize = read16(data, 52);
        header.e_phentsize = read16(data, 54);
        header.e_phnum = read16(data, 56);
        header.e_shentsize = read16(data, 58);
        header.e_shnum = read16(data, 60);
        header.e_shstrndx = read16(data, 62);
    }

    return header;
}

ElfLoader::ProgramHeader ElfLoader::parseProgramHeader(const std::vector<uint8_t>& data, size_t offset) {
    ProgramHeader ph;
    
    if (data[4] == ELFCLASS32) {
        ph.p_type = read32(data, offset + 0);
        ph.p_offset = read32(data, offset + 4);
        ph.p_vaddr = read32(data, offset + 8);
        ph.p_paddr = read32(data, offset + 12);
        ph.p_filesz = read32(data, offset + 16);
        ph.p_memsz = read32(data, offset + 20);
        ph.p_flags = read32(data, offset + 24);
        ph.p_align = read32(data, offset + 28);
    } else {
        ph.p_type = read32(data, offset + 0);
        ph.p_flags = read32(data, offset + 4);
        ph.p_offset = read64(data, offset + 8);
        ph.p_vaddr = read64(data, offset + 16);
        ph.p_paddr = read64(data, offset + 24);
        ph.p_filesz = read64(data, offset + 32);
        ph.p_memsz = read64(data, offset + 40);
        ph.p_align = read64(data, offset + 48);
    }

    return ph;
}

uint64_t ElfLoader::read64(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 8 > data.size()) {
        throw SimulatorException("读取越界");
    }
    
    // 小端字节序
    return static_cast<uint64_t>(data[offset]) |
           (static_cast<uint64_t>(data[offset + 1]) << 8) |
           (static_cast<uint64_t>(data[offset + 2]) << 16) |
           (static_cast<uint64_t>(data[offset + 3]) << 24) |
           (static_cast<uint64_t>(data[offset + 4]) << 32) |
           (static_cast<uint64_t>(data[offset + 5]) << 40) |
           (static_cast<uint64_t>(data[offset + 6]) << 48) |
           (static_cast<uint64_t>(data[offset + 7]) << 56);
}

uint32_t ElfLoader::read32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) {
        throw SimulatorException("读取越界");
    }
    
    // 小端字节序
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint16_t ElfLoader::read16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) {
        throw SimulatorException("读取越界");
    }
    
    // 小端字节序
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
}

} // namespace riscv
