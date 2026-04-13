#include "system/simulator.h"
#include "system/checkpoint_runner.h"
#include "system/elf_loader.h"
#include "common/debug_types.h"
#include "cpu/ooo/ooo_cpu.h"
#include <cctype>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>

using namespace riscv;

namespace {

uint32_t parseEnabledExtensions(const std::string& isa_string) {
    std::string isa_upper;
    isa_upper.reserve(isa_string.size());
    for (const char ch : isa_string) {
        isa_upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }

    size_t pos = 0;
    if (isa_upper.rfind("RV32", 0) == 0 || isa_upper.rfind("RV64", 0) == 0) {
        pos = 4;
    }

    uint32_t extensions = 0;
    const auto add_extension = [&extensions](Extension extension) {
        extensions |= static_cast<uint32_t>(extension);
    };

    for (size_t i = pos; i < isa_upper.size(); ++i) {
        const char ext = isa_upper[i];
        if (ext == '_' || ext == 'Z' || ext == 'X') {
            break;
        }

        switch (ext) {
            case 'I':
            case 'E':
                add_extension(Extension::I);
                break;
            case 'M':
                add_extension(Extension::M);
                break;
            case 'A':
                add_extension(Extension::A);
                break;
            case 'F':
                add_extension(Extension::F);
                break;
            case 'D':
                add_extension(Extension::D);
                break;
            case 'C':
                add_extension(Extension::C);
                break;
            default:
                break;
        }
    }

    if ((extensions & static_cast<uint32_t>(Extension::I)) == 0) {
        add_extension(Extension::I);
    }
    return extensions;
}

} // namespace

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options] [program]\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help                   Show this help message\n";
    std::cout << "  -s, --step                   Step-by-step execution mode\n";
    std::cout << "  -d, --debug                  Debug mode\n";
    std::cout << "  -m SIZE                      Set memory size in bytes\n";
    std::cout << "  --max-instructions=N         In-order max instruction limit (0=disable)\n";
    std::cout << "  --max-ooo-cycles=N           Out-of-order max cycle limit (0=disable)\n";
    std::cout << "  -e, --elf                    Load ELF file (auto detect)\n";
    std::cout << "  --ooo                        Use out-of-order CPU (default)\n";
    std::cout << "  --in-order                   Use in-order CPU\n";
    std::cout << "  --signature-file=FILE        Dump signature to FILE\n";
    std::cout << "  --signature-start=ADDR       Signature start address (hex/dec)\n";
    std::cout << "  --signature-end=ADDR         Signature end address (hex/dec)\n";
    std::cout << "  --signature-granularity=N    Signature bytes per line (default: 8)\n";
    std::cout << "  --tohost-addr=ADDR           Override tohost address (hex/dec)\n";
    std::cout << "  --fromhost-addr=ADDR         Override fromhost address (hex/dec)\n";
    std::cout << "  --isa=ISA                    Override enabled ISA extensions (e.g. RV64I_Zicsr)\n";
    std::cout << "  --stats-file=FILE            Dump detailed OOO stats to FILE\n";
    std::cout << "  --stats-warmup-cycles=N      Print pre-warmup stats, then reset OOO stats at cycle N\n";
    std::cout << "  --l1d-next-line-prefetch=on|off  Toggle OOO L1D next-line prefetcher (default: on)\n";
    std::cout << "  --pipeline-view=FILE         Generate HTML pipeline visualization\n";
    std::cout << "  --pipeline-cycles=START-END  Limit pipeline view to cycle range\n";
    std::cout << "  --pipeline-max=N             Max instructions in pipeline view (default: 2000)\n";
    std::cout << "  --checkpoint=FILE            Run a checkpoint slice instead of an ELF/binary\n";
    std::cout << "  --checkpoint-recipe=FILE     Checkpoint recipe spec path\n";
    std::cout << "  --checkpoint-importer=NAME   Checkpoint importer executable or name\n";
    std::cout << "  --checkpoint-restorer=FILE   Checkpoint restorer executable path\n";
    std::cout << "  --checkpoint-output-dir=DIR  Directory for checkpoint artifacts\n";
    std::cout << "  --checkpoint-difftest=on|off Enable per-commit difftest for checkpoint OOO runs (default: off)\n";
    std::cout << "  --warmup-instructions=N      Warmup instruction count for checkpoint mode\n";
    std::cout << "  --measure-instructions=N     Measure instruction count for checkpoint mode\n";
    std::cout << "  +signature=FILE              Spike-compatible signature file option\n";
    std::cout << "  +signature-granularity=N     Spike-compatible granularity option\n";
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
    uint64_t maxInOrderInstructions = 5000000;
    bool maxInOrderInstructionsSpecified = false;
    uint64_t maxOutOfOrderCycles = 50000;
    CpuType cpuType = CpuType::OUT_OF_ORDER;  // 默认使用乱序执行CPU
    
    // 增强调试参数
    std::string debugCategories;
    std::string debugCycles;
    std::string debugPreset;
    bool debugNoConsole = false;
    std::string signatureFile;
    Address signatureStart = 0;
    Address signatureEnd = 0;
    bool signatureStartSet = false;
    bool signatureEndSet = false;
    size_t signatureGranularity = 8;
    Address tohostAddr = 0;
    Address fromhostAddr = 0;
    bool tohostAddrSet = false;
    bool fromhostAddrSet = false;
    std::string isaString;
    std::string statsFile;
    uint64_t statsWarmupCycles = 0;
    bool l1dNextLinePrefetchEnabled = true;
    std::string pipelineViewFile;
    uint64_t pipelineStartCycle = 0;
    uint64_t pipelineEndCycle = UINT64_MAX;
    size_t pipelineMaxInstructions = 2000;
    std::string checkpointPath;
    std::string checkpointRecipePath;
    std::string checkpointImporterName;
    std::string checkpointRestorerPath;
    std::string checkpointOutputDir;
    bool checkpointDiffTestEnabled = false;
    uint64_t warmupInstructions = 5'000'000;
    uint64_t measureInstructions = 5'000'000;
    
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
        } else if (arg.find("--max-instructions=") == 0) {
            maxInOrderInstructions = std::stoull(arg.substr(19), nullptr, 0);
            maxInOrderInstructionsSpecified = true;
        } else if (arg.find("--max-ooo-cycles=") == 0) {
            maxOutOfOrderCycles = std::stoull(arg.substr(17), nullptr, 0);
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
        } else if (arg.find("--signature-file=") == 0) {
            signatureFile = arg.substr(17);  // 去掉 "--signature-file=" 前缀
        } else if (arg.find("--signature-start=") == 0) {
            signatureStart = static_cast<Address>(std::stoull(arg.substr(18), nullptr, 0));
            signatureStartSet = true;
        } else if (arg.find("--signature-end=") == 0) {
            signatureEnd = static_cast<Address>(std::stoull(arg.substr(16), nullptr, 0));
            signatureEndSet = true;
        } else if (arg.find("--signature-granularity=") == 0) {
            signatureGranularity = static_cast<size_t>(std::stoull(arg.substr(24), nullptr, 0));
        } else if (arg.find("+signature=") == 0) {
            signatureFile = arg.substr(11);  // 去掉 "+signature=" 前缀
        } else if (arg.find("+signature-granularity=") == 0) {
            signatureGranularity = static_cast<size_t>(std::stoull(arg.substr(23), nullptr, 0));
        } else if (arg.find("--tohost-addr=") == 0) {
            tohostAddr = static_cast<Address>(std::stoull(arg.substr(14), nullptr, 0));
            tohostAddrSet = true;
        } else if (arg.find("--fromhost-addr=") == 0) {
            fromhostAddr = static_cast<Address>(std::stoull(arg.substr(16), nullptr, 0));
            fromhostAddrSet = true;
        } else if (arg.find("--isa=") == 0) {
            isaString = arg.substr(6);
        } else if (arg.find("--stats-file=") == 0) {
            statsFile = arg.substr(13);
        } else if (arg.find("--stats-warmup-cycles=") == 0) {
            statsWarmupCycles = std::stoull(arg.substr(22), nullptr, 0);
        } else if (arg.find("--l1d-next-line-prefetch=") == 0) {
            static const std::string kL1DPrefetchPrefix = "--l1d-next-line-prefetch=";
            const std::string value = arg.substr(kL1DPrefetchPrefix.size());
            if (value == "on") {
                l1dNextLinePrefetchEnabled = true;
            } else if (value == "off") {
                l1dNextLinePrefetchEnabled = false;
            } else {
                std::cerr << "Error: invalid --l1d-next-line-prefetch value '" << value
                          << "', expected on|off\n";
                return 1;
            }
        } else if (arg.find("--pipeline-view=") == 0) {
            pipelineViewFile = arg.substr(16);
        } else if (arg.find("--pipeline-cycles=") == 0) {
            std::string range = arg.substr(18);
            size_t dashPos = range.find('-');
            if (dashPos != std::string::npos) {
                pipelineStartCycle = std::stoull(range.substr(0, dashPos));
                std::string endStr = range.substr(dashPos + 1);
                if (!endStr.empty() && endStr != "end") {
                    pipelineEndCycle = std::stoull(endStr);
                }
            }
        } else if (arg.find("--pipeline-max=") == 0) {
            pipelineMaxInstructions = std::stoull(arg.substr(15));
        } else if (arg.find("--checkpoint=") == 0) {
            checkpointPath = arg.substr(13);
        } else if (arg.find("--checkpoint-recipe=") == 0) {
            checkpointRecipePath = arg.substr(20);
        } else if (arg.find("--checkpoint-importer=") == 0) {
            checkpointImporterName = arg.substr(22);
        } else if (arg.find("--checkpoint-restorer=") == 0) {
            checkpointRestorerPath = arg.substr(22);
        } else if (arg.find("--checkpoint-output-dir=") == 0) {
            checkpointOutputDir = arg.substr(24);
        } else if (arg.find("--checkpoint-difftest=") == 0) {
            static const std::string kCheckpointDiffTestPrefix = "--checkpoint-difftest=";
            const std::string value = arg.substr(kCheckpointDiffTestPrefix.size());
            if (value == "on") {
                checkpointDiffTestEnabled = true;
            } else if (value == "off") {
                checkpointDiffTestEnabled = false;
            } else {
                std::cerr << "Error: invalid --checkpoint-difftest value '" << value
                          << "', expected on|off\n";
                return 1;
            }
        } else if (arg.find("--warmup-instructions=") == 0) {
            warmupInstructions = std::stoull(arg.substr(22), nullptr, 0);
        } else if (arg.find("--measure-instructions=") == 0) {
            measureInstructions = std::stoull(arg.substr(23), nullptr, 0);
        } else if (filename.empty()) {
            filename = arg;
        }
    }
    
    try {
        const bool checkpointMode = !checkpointPath.empty();

        // 未显式指定 -m 时，若加载 ELF 则根据段地址自动决定内存大小
        if (!checkpointMode && !memorySizeSpecified && !filename.empty()) {
            const bool willLoadElf = forceElf || filename.find(".elf") != std::string::npos;
            if (willLoadElf) {
                memorySize = ElfLoader::getRequiredMemorySize(filename, memorySize);
            }
        }

        if (cpuType == CpuType::OUT_OF_ORDER) {
            setOutOfOrderL1DNextLinePrefetchEnabled(l1dNextLinePrefetchEnabled);
        }

        // 创建模拟器
        Simulator simulator(memorySize, cpuType);
        simulator.setMaxInOrderInstructions(maxInOrderInstructions);
        simulator.setMaxOutOfOrderCycles(maxOutOfOrderCycles);
        if (tohostAddrSet || fromhostAddrSet) {
            if (!tohostAddrSet || !fromhostAddrSet) {
                std::cerr << "Error: --tohost-addr and --fromhost-addr must be provided together\n";
                return 1;
            }
            simulator.setHostCommAddresses(tohostAddr, fromhostAddr);
        }

        if (!isaString.empty()) {
            simulator.setEnabledExtensions(parseEnabledExtensions(isaString));
        }

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

        if (checkpointMode) {
            if (checkpointOutputDir.empty()) {
                std::cerr << "Error: --checkpoint-output-dir is required in checkpoint mode\n";
                return 1;
            }

            CheckpointRunConfig runConfig;
            runConfig.checkpoint_path = checkpointPath;
            runConfig.recipe_path = checkpointRecipePath;
            runConfig.importer_name = checkpointImporterName;
            runConfig.restorer_path = checkpointRestorerPath;
            runConfig.output_dir = checkpointOutputDir;
            runConfig.warmup_instructions = warmupInstructions;
            runConfig.measure_instructions = measureInstructions;
            runConfig.enable_difftest = checkpointDiffTestEnabled;

            std::cout << "Running checkpoint: " << checkpointPath << "\n";
            CheckpointRunner runner(cpuType, memorySize);
            if (cpuType != CpuType::IN_ORDER || maxInOrderInstructionsSpecified) {
                runner.setMaxInOrderInstructions(maxInOrderInstructions);
            } else {
                runner.setMaxInOrderInstructions(0);
            }
            runner.setMaxOutOfOrderCycles(maxOutOfOrderCycles);

            const CheckpointRunResult result = runner.run(runConfig);
            std::cout << "Checkpoint status: " << result.status << "\n";
            std::cout << "Workload: " << result.workload_name
                      << ", slice: " << result.slice_id
                      << ", instructions: " << result.instructions_measure
                      << ", cycles: " << result.cycles_measure << "\n";
            if (!result.message.empty()) {
                std::cout << "Message: " << result.message << "\n";
            }
            return result.success ? 0 : 1;
        }
        
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
        
        // 启用流水线可视化（在程序加载后，确保 reset 不会清掉 tracer）
        if (!pipelineViewFile.empty()) {
            simulator.enablePipelineTracer(pipelineViewFile, pipelineStartCycle, pipelineEndCycle, pipelineMaxInstructions);
            std::cout << "Pipeline view enabled, output: " << pipelineViewFile << "\n";
        }

        if (debugMode) {
            std::cout << "Initial state:\n";
            simulator.dumpState();
        }
        
        bool executionSuccess = true;
        auto writeOooStats = [&](std::ostream& os,
                                 const std::string& title,
                                 const ICpuInterface::StatsList& stats) {
            os << "\n=== " << title << " ===\n";
            uint64_t instructions = 0;
            uint64_t cycles = 0;
            for (const auto& entry : stats) {
                if (entry.name == "instructions") {
                    instructions = entry.value;
                } else if (entry.name == "cycles") {
                    cycles = entry.value;
                }
            }

            os << "\n=== Execution Stats ===\n";
            os << "Instructions: " << instructions << "\n";
            os << "Final PC: 0x" << std::hex << simulator.getCpu()->getPC() << std::dec << "\n";
            os << "Program State: " << (simulator.isHalted() ? "halted" : "running") << "\n";
            os << "Cycles: " << cycles << "\n";

            os << "\n=== Out-of-Order Performance Stats ===\n";
            if (!stats.empty()) {
                simulator.getCpu()->dumpDetailedStats(os);
            } else {
                os << "Warning: failed to get OOO CPU stats\n";
            }
        };
        bool warmupTriggered = false;
        std::ofstream statsStream;
        if (!statsFile.empty()) {
            statsStream.open(statsFile);
            if (!statsStream.is_open()) {
                std::cerr << "Error: failed to open stats file: " << statsFile << "\n";
                return 1;
            }
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
                    executionSuccess = false;
                    break;
                }
            }
        } else {
            std::cout << "Running program...\n";
            try {
                if (cpuType == CpuType::OUT_OF_ORDER && statsWarmupCycles > 0) {
                    warmupTriggered = simulator.runWithWarmup(statsWarmupCycles, [&]() {
                        const auto warmupStats = simulator.getCpu()->getStats();
                        if (statsStream.is_open()) {
                            writeOooStats(
                                statsStream,
                                fmt::format("Warmup Statistics (cycles 0-{})", simulator.getCycleCount()),
                                warmupStats);
                        }
                        simulator.getCpu()->resetStats();
                    });
                } else {
                    simulator.run();
                }

                if (simulator.hasProgramExit()) {
                    int exitCode = simulator.getProgramExitCode();
                    if (exitCode == 0) {
                        std::cout << "\n=== TEST RESULT: PASS ===\n";
                        std::cout << "Program exited normally, code: " << exitCode << "\n";
                    } else {
                        std::cout << "\n=== TEST RESULT: FAIL ===\n";
                        std::cout << "Program exited with failure, code: " << exitCode << "\n";
                        if (exitCode > 1) {
                            std::cout << "Failed test index: " << exitCode << "\n";
                        }
                    }
                } else if (
                    simulator.isHalted() &&
                    !simulator.isHaltedByInstructionLimit() &&
                    !simulator.isHaltedByCycleLimit()
                ) {
                    int exitCode = static_cast<int>(simulator.getCpu()->getRegister(10));
                    if (exitCode == 0) {
                        std::cout << "\n=== TEST RESULT: PASS ===\n";
                        std::cout << "Program returned normally, code: " << exitCode << "\n";
                    } else {
                        std::cout << "\n=== TEST RESULT: FAIL ===\n";
                        std::cout << "Program returned with failure, code: " << exitCode << "\n";
                    }
                }

                std::cout << "Program finished\n";
            } catch (const SimulatorException& e) {
                std::cerr << "Execution error: " << e.what() << "\n";
                executionSuccess = false;
            }
        }
        
        // 打印最终状态和统计信息
        if (debugMode) {
            std::cout << "\nFinal state:\n";
            simulator.dumpState();
        }
        
        if (cpuType == CpuType::OUT_OF_ORDER) {
            auto stats = simulator.getCpu()->getStats();
            if (statsWarmupCycles > 0) {
                if (!warmupTriggered) {
                    std::cout << "\nWarning: warmup cycle " << statsWarmupCycles
                              << " was not reached before program stopped\n";
                }
                if (statsStream.is_open()) {
                    writeOooStats(statsStream, "Post-Warmup Statistics", stats);
                }
            } else if (statsStream.is_open()) {
                writeOooStats(statsStream, "Out-of-Order Performance Stats", stats);
            }

            if (statsStream.is_open()) {
                statsStream.flush();
                std::cout << "OOO stats dumped to: " << statsFile << "\n";
            }
        } else if (!stepMode) {
            simulator.printStatistics();
        }

        // 生成流水线可视化
        if (!pipelineViewFile.empty()) {
            if (simulator.writePipelineView()) {
                std::cout << "Pipeline view generated: " << pipelineViewFile << "\n";
            } else {
                std::cerr << "Warning: failed to generate pipeline view\n";
            }
        }

        if (!signatureFile.empty()) {
            if (!signatureStartSet || !signatureEndSet) {
                std::cerr << "Error: signature dump requires both --signature-start and --signature-end\n";
                return 1;
            }
            if (!simulator.dumpSignature(signatureFile, signatureStart, signatureEnd, signatureGranularity)) {
                std::cerr << "Error: failed to dump signature\n";
                return 1;
            }
        }
        
        if (!executionSuccess) {
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
