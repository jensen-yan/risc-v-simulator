#include "system/simulator.h"
#include "system/elf_loader.h"
#include "common/debug_types.h"
#include <iostream>
#include <iomanip>
#include <string>

using namespace riscv;

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options] [program]\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help                   Show this help message\n";
    std::cout << "  -s, --step                   Step-by-step execution mode\n";
    std::cout << "  -d, --debug                  Debug mode\n";
    std::cout << "  -m SIZE                      Set memory size in bytes\n";
    std::cout << "  -e, --elf                    Load ELF file (auto detect)\n";
    std::cout << "  --ooo                        Use out-of-order CPU (default)\n";
    std::cout << "  --in-order                   Use in-order CPU\n";
    std::cout << "\n";
    std::cout << "Extended debug options:\n";
    std::cout << "  --debug-flags=<flags>        Set debug categories (comma separated)\n";
    std::cout << "  --debug-cycles=<start>-<end> Set debug cycle range\n";
    std::cout << "  --debug-preset=<preset>      Use preset debug configuration\n";
    std::cout << "  --debug-file=<file>          Write debug log to file\n";
    std::cout << "  --debug-no-console           Disable console debug output\n";
    std::cout << "\n";
    std::cout << "Available debug presets:\n";
    std::cout << "  basic      Basic pipeline (fetch, decode, commit)\n";
    std::cout << "  ooo        Out-of-order (fetch, decode, issue, execute, writeback, commit, rob, rename, rs)\n";
    std::cout << "  inorder    In-order (inorder)\n";
    std::cout << "  pipeline   Full pipeline (fetch, decode, issue, execute, writeback, commit)\n";
    std::cout << "  performance Performance analysis (execute, commit, rob, rs, branch, stall)\n";
    std::cout << "  detailed   All debug categories\n";
    std::cout << "  memory     Memory access (fetch, memory, execute, commit)\n";
    std::cout << "  branch     Branch prediction (fetch, decode, execute, commit, branch)\n";
    std::cout << "  minimal    Minimal debug (fetch, commit)\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " program.bin                              # Binary file\n";
    std::cout << "  " << programName << " program.elf                              # ELF file\n";
    std::cout << "  " << programName << " -s -d program.elf                        # Step debug mode\n";
    std::cout << "  " << programName << " --ooo program.elf                        # Out-of-order CPU\n";
    std::cout << "  " << programName << " --debug-preset=basic program.elf         # Basic debug preset\n";
    std::cout << "  " << programName << " --debug-preset=ooo program.elf           # OOO debug preset\n";
    std::cout << "  " << programName << " --debug-flags=fetch,decode,commit program.elf  # Custom categories\n";
    std::cout << "  " << programName << " --debug-cycles=100-200 program.elf       # Cycle range\n";
}

int main(int argc, char* argv[]) {
    std::cout << "RISC-V CPU Simulator v1.0\n";
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
        } else if (arg.find("--debug-file=") == 0) {
            std::string logFile = arg.substr(13); // 去掉 "--debug-file=" 前缀
            DebugManager::getInstance().setLogFile(logFile);
            DebugManager::getInstance().setOutputToFile(true);
            debugMode = true; // 自动启用调试模式
            DebugManager::getInstance().setOutputToConsole(false); // 禁用控制台输出
            debugNoConsole = true;
            std::cout << "Debug log will be written to file: " << logFile << "\n";
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
            
            // 配置调试分类
            if (!debugPreset.empty()) {
                debugManager.setPreset(debugPreset);
                std::cout << "Using debug preset: " << debugPreset << "\n";
            } else if (!debugCategories.empty()) {
                debugManager.setCategories(debugCategories);
                std::cout << "Using debug categories: " << debugCategories << "\n";
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
                    std::cout << "Debug cycle range: " << startCycle << "-"
                              << (endCycle == UINT64_MAX ? "END" : std::to_string(endCycle)) << "\n";
                } else {
                    std::cerr << "Warning: invalid cycle range format, expected start-end\n";
                }
            }
            
            // 显示调试配置信息
            std::cout << "\n" << debugManager.getConfigInfo() << "\n";
        } else {
            DebugManager::getInstance().setOutputToConsole(false);
            DebugManager::getInstance().setOutputToFile(false);
        }
        
        // 显示CPU类型
        std::cout << "CPU type: " << (cpuType == CpuType::OUT_OF_ORDER ? "out-of-order" : "in-order") << "\n";
        std::cout << "Memory size: " << memorySize << " bytes\n\n";
        
        if (!filename.empty()) {
            std::cout << "Loading program: " << filename << "\n";
            
            // 自动检测文件类型或根据用户指定加载
            bool loadSuccess = false;
            
            if (forceElf || filename.find(".elf") != std::string::npos) {
                // 尝试加载ELF文件
                std::cout << "Trying ELF loader...\n";
                loadSuccess = simulator.loadElfProgram(filename);
            } else {
                // 尝试加载二进制文件
                std::cout << "Trying binary loader...\n";
                loadSuccess = simulator.loadRiscvProgram(filename, 0x1000);
            }
            
            if (!loadSuccess) {
                std::cerr << "Error: failed to load program file\n";
                return 1;
            }
        } else {
            std::cout << "No program file specified, using test mode\n";
            // TODO: 添加简单的测试程序
        }
        
        if (debugMode) {
            std::cout << "Initial state:\n";
            simulator.dumpState();
        }
        
        if (stepMode) {
            std::cout << "Step mode (press Enter to continue, input q to quit):\n";
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
                    std::cerr << "Execution error: " << e.what() << "\n";
                    break;
                }
            }
        } else {
            std::cout << "Running program...\n";
            try {
                simulator.run();
                std::cout << "Program finished\n";
            } catch (const SimulatorException& e) {
                std::cerr << "Execution error: " << e.what() << "\n";
            }
        }
        
        // 打印最终状态和统计信息
        if (debugMode) {
            std::cout << "\nFinal state:\n";
            simulator.dumpState();
        }
        
        simulator.printStatistics();
        
        // 如果使用乱序执行CPU，显示额外的性能统计
        if (cpuType == CpuType::OUT_OF_ORDER) {
            std::cout << "\n=== Out-of-Order Performance Stats ===\n";
            auto stats = simulator.getCpu()->getStats();
            if (!stats.empty()) {
                uint64_t instructions = 0;
                uint64_t cycles = 0;
                for (const auto& entry : stats) {
                    std::cout << entry.name << ": " << entry.value;
                    if (!entry.description.empty()) {
                        std::cout << " (" << entry.description << ")";
                    }
                    std::cout << "\n";
                    if (entry.name == "instructions") {
                        instructions = entry.value;
                    } else if (entry.name == "cycles") {
                        cycles = entry.value;
                    }
                }
                if (cycles > 0) {
                    double ipc = static_cast<double>(instructions) / cycles;
                    std::cout << "IPC: " << std::fixed << std::setprecision(2) << ipc << "\n";
                }
            } else {
                std::cout << "Warning: failed to get OOO CPU stats\n";
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
