#include "system/simulator.h"
#include "system/elf_loader.h"
#include "common/debug_types.h"
#include <iostream>
#include <string>

using namespace riscv;

void printUsage(const char* programName) {
    std::cout << "用法: " << programName << " [选项] [程序文件]\n";
    std::cout << "选项:\n";
    std::cout << "  -h, --help                   显示此帮助信息\n";
    std::cout << "  -s, --step                   单步执行模式\n";
    std::cout << "  -d, --debug                  调试模式\n";
    std::cout << "  -m SIZE                      设置内存大小（字节）\n";
    std::cout << "  -e, --elf                    加载ELF文件（自动检测）\n";
    std::cout << "  --ooo                        使用乱序执行CPU（默认）\n";
    std::cout << "  --in-order                   使用顺序执行CPU\n";
    std::cout << "\n";
    std::cout << "增强调试选项:\n";
    std::cout << "  --debug-flags=<flags>        指定调试分类（用逗号分隔）\n";
    std::cout << "  --debug-cycles=<start>-<end> 指定调试周期范围\n";
    std::cout << "  --debug-preset=<preset>      使用预设调试配置\n";
    std::cout << "  --debug-simple               简洁输出模式\n";
    std::cout << "  --debug-verbose              详细输出模式（默认）\n";
    std::cout << "  --debug-with-pc              带PC信息的输出模式\n";
    std::cout << "  --debug-file=<file>      调试日志输出到文件\n";
    std::cout << "  --debug-no-console           禁用控制台输出（仅文件输出）\n";
    std::cout << "\n";
    std::cout << "可用的调试预设:\n";
    std::cout << "  basic      基础流水线 (fetch, decode, commit)\n";
    std::cout << "  ooo        乱序执行 (fetch, decode, issue, execute, writeback, commit, rob, rename, rs)\n";
    std::cout << "  inorder    顺序执行 (inorder)\n";
    std::cout << "  pipeline   完整流水线 (fetch, decode, issue, execute, writeback, commit)\n";
    std::cout << "  performance 性能分析 (execute, commit, rob, rs, branch, stall)\n";
    std::cout << "  detailed   所有调试信息\n";
    std::cout << "  memory     内存访问 (fetch, memory, execute, commit)\n";
    std::cout << "  branch     分支预测 (fetch, decode, execute, commit, branch)\n";
    std::cout << "  minimal    最小调试 (fetch, commit)\n";
    std::cout << "\n";
    std::cout << "示例:\n";
    std::cout << "  " << programName << " program.bin                              # 二进制文件\n";
    std::cout << "  " << programName << " program.elf                              # ELF文件\n";
    std::cout << "  " << programName << " -s -d program.elf                        # 单步调试\n";
    std::cout << "  " << programName << " --ooo program.elf                        # 乱序执行CPU\n";
    std::cout << "  " << programName << " --debug-preset=basic program.elf         # 基础调试\n";
    std::cout << "  " << programName << " --debug-preset=ooo --debug-simple program.elf  # 乱序调试简洁模式\n";
    std::cout << "  " << programName << " --debug-flags=fetch,decode,commit program.elf  # 自定义分类\n";
    std::cout << "  " << programName << " --debug-cycles=100-200 program.elf       # 指定周期范围\n";
}

int main(int argc, char* argv[]) {
    std::cout << "RISC-V CPU 模拟器 v1.0\n";
    std::cout << "===========================\n\n";
    
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    // 简单的参数解析
    std::string filename;
    bool stepMode = false;
    bool debugMode = false;
    bool forceElf = false;
    size_t memorySize = Memory::DEFAULT_SIZE;
    bool memorySizeSpecified = false;
    CpuType cpuType = CpuType::OUT_OF_ORDER;  // 默认使用乱序执行CPU
    
    // 增强调试参数
    std::string debugCategories;
    std::string debugCycles;
    std::string debugPreset;
    bool debugSimple = false;
    bool debugVerbose = false;
    bool debugWithPC = false;
    bool debugNoConsole = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-s" || arg == "--step") {
            stepMode = true;
        } else if (arg == "-d" || arg == "--debug") {
            debugMode = true;
        } else if (arg == "-e" || arg == "--elf") {
            forceElf = true;
        } else if (arg == "--ooo") {
            cpuType = CpuType::OUT_OF_ORDER;
        } else if (arg == "--in-order") {
            cpuType = CpuType::IN_ORDER;
        } else if (arg == "-m" && i + 1 < argc) {
            memorySize = std::stoul(argv[++i]);
            memorySizeSpecified = true;
        } else if (arg.find("--debug-flags=") == 0) {
            debugCategories = arg.substr(14);  // 去掉 "--debug-flags=" 前缀
            debugMode = true;  // 自动启用调试模式
        } else if (arg.find("--debug-cycles=") == 0) {
            debugCycles = arg.substr(15);  // 去掉 "--debug-cycles=" 前缀
            debugMode = true;  // 自动启用调试模式
        } else if (arg.find("--debug-preset=") == 0) {
            debugPreset = arg.substr(15);  // 去掉 "--debug-preset=" 前缀
            debugMode = true;  // 自动启用调试模式
        } else if (arg == "--debug-simple") {
            debugSimple = true;
            debugMode = true;  // 自动启用调试模式
        } else if (arg == "--debug-verbose") {
            debugVerbose = true;
            debugMode = true;  // 自动启用调试模式
        } else if (arg == "--debug-with-pc") {
            debugWithPC = true;
            debugMode = true;  // 自动启用调试模式
        } else if (arg.find("--debug-file=") == 0) {
            std::string logFile = arg.substr(13); // 去掉 "--debug-file=" 前缀
            DebugManager::getInstance().setLogFile(logFile);
            DebugManager::getInstance().setOutputToFile(true);
            debugMode = true; // 自动启用调试模式
            DebugManager::getInstance().setOutputToConsole(false); // 禁用控制台输出
            debugNoConsole = true;
            std::cout << "调试日志将输出到文件: " << logFile << "\n";
        } else if (arg == "--debug-no-console") {
            DebugManager::getInstance().setOutputToConsole(false);
            debugMode = true; // 自动启用调试模式
            debugNoConsole = true;
        } else if (filename.empty()) {
            filename = arg;
        }
    }
    
    try {
        // 未显式指定 -m 时，若加载 ELF 则根据段地址自动决定内存大小
        if (!memorySizeSpecified && !filename.empty()) {
            const bool willLoadElf = forceElf || filename.find(".elf") != std::string::npos;
            if (willLoadElf) {
                memorySize = ElfLoader::getRequiredMemorySize(filename, memorySize);
            }
        }

        // 创建模拟器
        Simulator simulator(memorySize, cpuType);
        
        // 配置调试系统
        if (debugMode) {
            auto& debugManager = DebugManager::getInstance();

            // 根据参数启用/禁用控制台输出
            debugManager.setOutputToConsole(!debugNoConsole);
            
            // 配置输出格式
            if (debugWithPC) {
                debugManager.setOutputMode(DebugFormatter::Mode::WITH_PC);
            } else if (debugSimple) {
                debugManager.setOutputMode(DebugFormatter::Mode::SIMPLE);
            } else if (debugVerbose) {
                debugManager.setOutputMode(DebugFormatter::Mode::VERBOSE);
            }
            
            // 配置调试分类
            if (!debugPreset.empty()) {
                debugManager.setPreset(debugPreset);
                std::cout << "使用调试预设: " << debugPreset << "\n";
            } else if (!debugCategories.empty()) {
                debugManager.setCategories(debugCategories);
                std::cout << "使用调试分类: " << debugCategories << "\n";
            }
            
            // 配置周期范围
            if (!debugCycles.empty()) {
                size_t dashPos = debugCycles.find('-');
                if (dashPos != std::string::npos) {
                    uint64_t startCycle = std::stoull(debugCycles.substr(0, dashPos));
                    uint64_t endCycle = UINT64_MAX;
                    
                    std::string endStr = debugCycles.substr(dashPos + 1);
                    if (!endStr.empty() && endStr != "end" && endStr != "END") {
                        endCycle = std::stoull(endStr);
                    }
                    
                    debugManager.setCycleRange(startCycle, endCycle);
                    std::cout << "调试周期范围: " << startCycle << "-" << 
                        (endCycle == UINT64_MAX ? "END" : std::to_string(endCycle)) << "\n";
                } else {
                    std::cerr << "警告: 无效的周期范围格式，应为 start-end\n";
                }
            }
            
            // 显示调试配置信息
            std::cout << "\n" << debugManager.getConfigInfo() << "\n";
        } else {
            DebugManager::getInstance().setOutputToConsole(false);
            DebugManager::getInstance().setOutputToFile(false);
        }
        
        // 显示CPU类型
        std::cout << "CPU类型: " << (cpuType == CpuType::OUT_OF_ORDER ? "乱序执行" : "顺序执行") << "\n";
        std::cout << "内存大小: " << memorySize << " 字节\n\n";
        
        if (!filename.empty()) {
            std::cout << "加载程序: " << filename << "\n";
            
            // 自动检测文件类型或根据用户指定加载
            bool loadSuccess = false;
            
            if (forceElf || filename.find(".elf") != std::string::npos) {
                // 尝试加载ELF文件
                std::cout << "尝试加载ELF文件...\n";
                loadSuccess = simulator.loadElfProgram(filename);
            } else {
                // 尝试加载二进制文件
                std::cout << "尝试加载二进制文件...\n";
                loadSuccess = simulator.loadRiscvProgram(filename, 0x1000);
            }
            
            if (!loadSuccess) {
                std::cerr << "错误: 无法加载程序文件\n";
                return 1;
            }
        } else {
            std::cout << "未指定程序文件，使用测试模式\n";
            // TODO: 添加简单的测试程序
        }
        
        if (debugMode) {
            std::cout << "初始状态:\n";
            simulator.dumpState();
        }
        
        if (stepMode) {
            std::cout << "单步执行模式（按Enter继续，输入q退出）:\n";
            std::string input;
            
            while (!simulator.isHalted()) {
                std::cout << "PC: 0x" << std::hex << simulator.getCpu()->getPC() << std::dec << " > ";
                std::getline(std::cin, input);
                
                if (input == "q" || input == "quit") {
                    break;
                }
                
                try {
                    simulator.step();
                    if (debugMode) {
                        simulator.dumpRegisters();
                    }
                } catch (const SimulatorException& e) {
                    std::cerr << "执行错误: " << e.what() << "\n";
                    break;
                }
            }
        } else {
            std::cout << "运行程序...\n";
            try {
                simulator.run();
                std::cout << "程序执行完成\n";
            } catch (const SimulatorException& e) {
                std::cerr << "执行错误: " << e.what() << "\n";
            }
        }
        
        // 打印最终状态和统计信息
        if (debugMode) {
            std::cout << "\n最终状态:\n";
            simulator.dumpState();
        }
        
        simulator.printStatistics();
        
        // 如果使用乱序执行CPU，显示额外的性能统计
        if (cpuType == CpuType::OUT_OF_ORDER) {
            std::cout << "\n=== 乱序执行性能统计 ===\n";
            // 通过适配器获取OutOfOrderCPU的特有功能
            // 这里需要dynamic_cast来安全地转换
            // 暂时简化处理
            std::cout << "注意: 乱序执行CPU的详细性能统计需要访问特定接口\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
