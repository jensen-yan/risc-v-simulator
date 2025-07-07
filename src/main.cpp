#include "simulator.h"
#include <iostream>
#include <string>

using namespace riscv;

void printUsage(const char* programName) {
    std::cout << "用法: " << programName << " [选项] [程序文件]\n";
    std::cout << "选项:\n";
    std::cout << "  -h, --help     显示此帮助信息\n";
    std::cout << "  -s, --step     单步执行模式\n";
    std::cout << "  -d, --debug    调试模式\n";
    std::cout << "  -m SIZE        设置内存大小（字节）\n";
    std::cout << "\n";
    std::cout << "示例:\n";
    std::cout << "  " << programName << " program.bin\n";
    std::cout << "  " << programName << " -s -d program.bin\n";
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
    size_t memorySize = Memory::DEFAULT_SIZE;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-s" || arg == "--step") {
            stepMode = true;
        } else if (arg == "-d" || arg == "--debug") {
            debugMode = true;
        } else if (arg == "-m" && i + 1 < argc) {
            memorySize = std::stoul(argv[++i]);
        } else if (filename.empty()) {
            filename = arg;
        }
    }
    
    try {
        // 创建模拟器
        Simulator simulator(memorySize);
        
        if (!filename.empty()) {
            std::cout << "加载程序: " << filename << "\n";
            if (!simulator.loadProgram(filename)) {
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
                std::cout << "PC: 0x" << std::hex << simulator.getPC() << std::dec << " > ";
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
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}