#include "system/checkpoint_runner.h"

#include "system/simulator.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace riscv {

namespace {

namespace fs = std::filesystem;

struct RunMetadata {
    std::string workload_name;
    std::string slice_id;
    double weight = 0.0;
};

RunMetadata inferMetadataFromConfig(const CheckpointRunConfig& config) {
    RunMetadata metadata;
    const fs::path checkpoint_path(config.checkpoint_path);
    const fs::path point_dir = checkpoint_path.parent_path();
    const fs::path workload_dir = point_dir.parent_path();

    if (!point_dir.empty()) {
        metadata.slice_id = point_dir.filename().string();
    }
    if (!workload_dir.empty()) {
        metadata.workload_name = workload_dir.filename().string();
    }

    const std::string filename = checkpoint_path.filename().string();
    const size_t first_underscore = filename.find('_');
    const size_t second_underscore =
        first_underscore == std::string::npos ? std::string::npos : filename.find('_', first_underscore + 1);
    if (second_underscore != std::string::npos) {
        const size_t third_underscore = filename.find('_', second_underscore + 1);
        if (third_underscore != std::string::npos && third_underscore > second_underscore + 1) {
            try {
                metadata.weight = std::stod(filename.substr(second_underscore + 1,
                                                           third_underscore - second_underscore - 1));
            } catch (const std::exception&) {
            }
        }
    }

    return metadata;
}

std::string failureReasonToString(CheckpointFailureReason reason) {
    switch (reason) {
        case CheckpointFailureReason::NONE:
            return "none";
        case CheckpointFailureReason::IMPORT_ERROR:
            return "import_error";
        case CheckpointFailureReason::RESTORE_ERROR:
            return "restore_error";
        case CheckpointFailureReason::ILLEGAL_INSTRUCTION:
            return "illegal_instruction";
        case CheckpointFailureReason::UNIMPLEMENTED_SYSTEM_INSTRUCTION:
            return "unimplemented_system_instruction";
        case CheckpointFailureReason::TRAP:
            return "trap";
        case CheckpointFailureReason::PROGRAM_EXIT:
            return "program_exit";
        case CheckpointFailureReason::HALT_REQUESTED:
            return "halt_requested";
        case CheckpointFailureReason::WINDOW_NOT_REACHED:
            return "window_not_reached";
        case CheckpointFailureReason::UNKNOWN:
            return "unknown";
    }
    return "unknown";
}

std::string jsonEscape(const std::string& text) {
    std::ostringstream escaped;
    for (const char ch : text) {
        switch (ch) {
            case '\\':
                escaped << "\\\\";
                break;
            case '"':
                escaped << "\\\"";
                break;
            case '\n':
                escaped << "\\n";
                break;
            case '\r':
                escaped << "\\r";
                break;
            case '\t':
                escaped << "\\t";
                break;
            default:
                escaped << ch;
                break;
        }
    }
    return escaped.str();
}

std::string csvEscape(const std::string& text) {
    if (text.find_first_of("\",\n\r") == std::string::npos) {
        return text;
    }
    std::string escaped = text;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.insert(pos, 1, '"');
        pos += 2;
    }
    return "\"" + escaped + "\"";
}

void writeTextFile(const fs::path& path, const std::string& content) {
    std::ofstream stream(path);
    if (!stream.is_open()) {
        throw SimulatorException("无法写入文件: " + path.string());
    }
    stream << content;
}

void writeMarkerFile(const fs::path& path) {
    std::ofstream stream(path);
    if (!stream.is_open()) {
        throw SimulatorException("无法写入标记文件: " + path.string());
    }
}

uint64_t statValue(const ICpuInterface::StatsList& stats, const std::string& name) {
    for (const auto& entry : stats) {
        if (entry.name == name) {
            return entry.value;
        }
    }
    return 0;
}

Address computeMemoryBaseAddress(const SnapshotBundle& snapshot) {
    if (snapshot.memory_segments.empty()) {
        return 0;
    }

    Address base = snapshot.memory_segments.front().base;
    for (const auto& segment : snapshot.memory_segments) {
        base = std::min(base, segment.base);
    }
    return base;
}

size_t computeRequiredMemorySize(const SnapshotBundle& snapshot,
                                 size_t fallback_memory_size,
                                 Address base_address) {
    uint64_t required = fallback_memory_size;
    if (snapshot.pc >= base_address) {
        required = std::max<uint64_t>(required, snapshot.pc - base_address + 4);
    }

    for (const auto& segment : snapshot.memory_segments) {
        const uint64_t segment_size = segment.byteSize();
        const uint64_t end = segment.base + segment_size;
        if (end < segment.base) {
            throw SimulatorException("checkpoint memory segment 地址溢出");
        }
        if (end < base_address) {
            throw SimulatorException("checkpoint memory segment 基址非法");
        }
        required = std::max(required, end - base_address);
    }

    if (required > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        throw SimulatorException("checkpoint 所需内存超出当前平台 size_t 范围");
    }
    return static_cast<size_t>(required);
}

CheckpointRunResult makeBaseResult(const CheckpointRunConfig& config) {
    CheckpointRunResult result;
    const RunMetadata metadata = inferMetadataFromConfig(config);
    result.benchmark = metadata.workload_name;
    result.workload_name = metadata.workload_name;
    result.slice_id = metadata.slice_id;
    result.weight = metadata.weight;
    return result;
}

void applyRecipeMetadata(CheckpointRunResult& result, const SnapshotBundle& snapshot) {
    if (!snapshot.recipe.workload_name.empty()) {
        result.benchmark = snapshot.recipe.workload_name;
        result.workload_name = snapshot.recipe.workload_name;
    }
    if (!snapshot.recipe.point_id.empty()) {
        result.slice_id = snapshot.recipe.point_id;
    }
    result.weight = snapshot.recipe.weight;
}

void fillMeasuredStats(CheckpointRunResult& result,
                       const InstructionWindowResult& window_result,
                       const ICpuInterface::StatsList& stats) {
    result.instructions_measure = statValue(stats, "instructions");
    if (result.instructions_measure == 0) {
        result.instructions_measure = window_result.measure_instructions_completed;
    }

    result.cycles_measure = statValue(stats, "cycles");
    if (result.cycles_measure == 0) {
        result.cycles_measure = window_result.measure_cycles;
    }

    if (result.cycles_measure > 0) {
        result.ipc_measure = static_cast<double>(result.instructions_measure) /
                             static_cast<double>(result.cycles_measure);
    } else {
        result.ipc_measure = 0.0;
    }
}

void writeStatsFile(const fs::path& output_dir,
                    const CheckpointRunResult& result,
                    Simulator* simulator) {
    std::ofstream stream(output_dir / "stats.txt");
    if (!stream.is_open()) {
        throw SimulatorException("无法写入 stats.txt");
    }

    stream << "=== Execution Stats ===\n";
    stream << "Status: " << result.status << "\n";
    stream << "Success: " << (result.success ? "true" : "false") << "\n";
    stream << "Failure Reason: " << failureReasonToString(result.failure_reason) << "\n";
    stream << "Message: " << result.message << "\n";
    stream << "Benchmark: " << result.benchmark << "\n";
    stream << "Workload: " << result.workload_name << "\n";
    stream << "Slice ID: " << result.slice_id << "\n";
    stream << "Weight: " << std::fixed << std::setprecision(6) << result.weight << "\n";
    stream << "Instructions Measure: " << result.instructions_measure << "\n";
    stream << "Cycles Measure: " << result.cycles_measure << "\n";
    stream << "IPC Measure: " << std::setprecision(6) << result.ipc_measure << "\n";

    if (simulator != nullptr) {
        stream << "Final PC: 0x" << std::hex << simulator->getCpu()->getPC() << std::dec << "\n";
        const auto stats = simulator->getCpu()->getStats();
        if (!stats.empty()) {
            stream << "\n=== CPU Stats ===\n";
            simulator->getCpu()->dumpDetailedStats(stream);
        }
    }
}

void writeResultJson(const fs::path& output_dir, const CheckpointRunResult& result) {
    std::ostringstream stream;
    stream << std::boolalpha;
    stream << "{";
    stream << "\"status\":\"" << jsonEscape(result.status) << "\",";
    stream << "\"success\":" << result.success << ",";
    stream << "\"failure_reason\":\"" << jsonEscape(failureReasonToString(result.failure_reason)) << "\",";
    stream << "\"message\":\"" << jsonEscape(result.message) << "\",";
    stream << "\"benchmark\":\"" << jsonEscape(result.benchmark) << "\",";
    stream << "\"workload_name\":\"" << jsonEscape(result.workload_name) << "\",";
    stream << "\"slice_id\":\"" << jsonEscape(result.slice_id) << "\",";
    stream << "\"weight\":" << std::fixed << std::setprecision(6) << result.weight << ",";
    stream << "\"instructions_measure\":" << result.instructions_measure << ",";
    stream << "\"cycles_measure\":" << result.cycles_measure << ",";
    stream << "\"ipc_measure\":" << std::fixed << std::setprecision(6) << result.ipc_measure;
    stream << "}\n";
    writeTextFile(output_dir / "result.json", stream.str());
}

void writeSummaryCsv(const fs::path& output_dir, const CheckpointRunResult& result) {
    std::ostringstream stream;
    stream << "status,success,failure_reason,benchmark,workload_name,slice_id,weight,"
              "instructions_measure,cycles_measure,ipc_measure\n";
    stream << csvEscape(result.status) << ","
           << (result.success ? "true" : "false") << ","
           << csvEscape(failureReasonToString(result.failure_reason)) << ","
           << csvEscape(result.benchmark) << ","
           << csvEscape(result.workload_name) << ","
           << csvEscape(result.slice_id) << ","
           << std::fixed << std::setprecision(6) << result.weight << ","
           << result.instructions_measure << ","
           << result.cycles_measure << ","
           << std::fixed << std::setprecision(6) << result.ipc_measure << "\n";
    writeTextFile(output_dir / "summary.csv", stream.str());
}

void writeArtifacts(const fs::path& output_dir,
                    const CheckpointRunResult& result,
                    Simulator* simulator,
                    bool write_error_file) {
    fs::create_directories(output_dir);

    writeStatsFile(output_dir, result, simulator);
    writeResultJson(output_dir, result);
    writeSummaryCsv(output_dir, result);

    fs::remove(output_dir / "completed");
    fs::remove(output_dir / "abort");
    if (result.success) {
        fs::remove(output_dir / "error.txt");
        writeMarkerFile(output_dir / "completed");
    } else {
        if (write_error_file) {
            writeTextFile(output_dir / "error.txt", result.message + "\n");
        }
        writeMarkerFile(output_dir / "abort");
    }
}

void cleanupEphemeralSegments(const SnapshotBundle& snapshot) {
    for (const auto& segment : snapshot.memory_segments) {
        if (segment.isFileBacked() && segment.ephemeral && !segment.file_path.empty()) {
            std::error_code ec;
            fs::remove(segment.file_path, ec);
        }
    }
}

} // namespace

CheckpointRunner::CheckpointRunner(CpuType cpu_type,
                                   size_t memory_size,
                                   ImporterFactory importer_factory)
    : cpu_type_(cpu_type), memory_size_(memory_size), importer_factory_(std::move(importer_factory)) {
    if (!importer_factory_) {
        importer_factory_ = [](const std::string& importer_name) {
            return createCheckpointImporter(importer_name);
        };
    }
}

void CheckpointRunner::setMaxInOrderInstructions(uint64_t limit) {
    max_in_order_instructions_ = limit;
}

void CheckpointRunner::setMaxOutOfOrderCycles(uint64_t limit) {
    max_out_of_order_cycles_ = limit;
}

CheckpointRunResult CheckpointRunner::run(const CheckpointRunConfig& config) const {
    if (config.output_dir.empty()) {
        throw SimulatorException("checkpoint output_dir 不能为空");
    }

    const fs::path output_dir(config.output_dir);
    CheckpointRunResult result = makeBaseResult(config);
    result.message = "checkpoint run aborted";

    std::unique_ptr<Simulator> simulator;
    SnapshotBundle snapshot;
    try {
        std::unique_ptr<ICheckpointImporter> importer = importer_factory_(config.importer_name);
        if (!importer) {
            throw SimulatorException("checkpoint importer 创建失败");
        }
        snapshot = importer->importCheckpoint(config);
        applyRecipeMetadata(result, snapshot);
    } catch (const std::exception& e) {
        result.status = "abort";
        result.success = false;
        result.failure_reason = CheckpointFailureReason::IMPORT_ERROR;
        result.message = e.what();
        cleanupEphemeralSegments(snapshot);
        writeArtifacts(output_dir, result, simulator.get(), /*write_error_file=*/true);
        return result;
    }

    try {
        const Address memory_base_address = computeMemoryBaseAddress(snapshot);
        simulator = std::make_unique<Simulator>(
            computeRequiredMemorySize(snapshot, memory_size_, memory_base_address),
            cpu_type_,
            memory_base_address);
        simulator->setMaxInOrderInstructions(max_in_order_instructions_);
        simulator->setMaxOutOfOrderCycles(max_out_of_order_cycles_);
    } catch (const std::exception& e) {
        result.status = "abort";
        result.success = false;
        result.failure_reason = CheckpointFailureReason::RESTORE_ERROR;
        result.message = e.what();
        cleanupEphemeralSegments(snapshot);
        writeArtifacts(output_dir, result, simulator.get(), /*write_error_file=*/true);
        return result;
    }

    if (!simulator->loadSnapshot(snapshot)) {
        result.status = "abort";
        result.success = false;
        result.failure_reason = CheckpointFailureReason::RESTORE_ERROR;
        result.message = "failed to load snapshot";
        cleanupEphemeralSegments(snapshot);
        writeArtifacts(output_dir, result, simulator.get(), /*write_error_file=*/true);
        return result;
    }

    cleanupEphemeralSegments(snapshot);

    try {
        const InstructionWindowResult window_result =
            simulator->runInstructionWindow(config.warmup_instructions, config.measure_instructions);
        fillMeasuredStats(result, window_result, simulator->getCpu()->getStats());

        if (window_result.success) {
            result.status = "completed";
            result.success = true;
            result.failure_reason = CheckpointFailureReason::NONE;
            result.message = window_result.message;
            writeArtifacts(output_dir, result, simulator.get(), /*write_error_file=*/false);
            return result;
        }

        result.status = "abort";
        result.success = false;
        result.failure_reason = window_result.failure_reason;
        result.message = window_result.message;
        writeArtifacts(output_dir, result, simulator.get(), /*write_error_file=*/true);
        return result;
    } catch (const std::exception& e) {
        result.status = "abort";
        result.success = false;
        result.failure_reason = CheckpointFailureReason::UNKNOWN;
        result.message = e.what();
        writeArtifacts(output_dir, result, simulator.get(), /*write_error_file=*/true);
        return result;
    }
}

} // namespace riscv
