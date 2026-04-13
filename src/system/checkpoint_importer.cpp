#include "system/checkpoint_importer.h"

#include "system/checkpoint_recipe.h"
#include "system/privilege_state.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <vector>

namespace riscv {

namespace {

struct ProcessResult {
    int exit_code = -1;
    std::string output;
};

struct BuiltinImageInfo {
    std::filesystem::path path;
    uint64_t size = 0;
};

constexpr const char* kBuiltinZstdImporterName = "builtin-zstd";
constexpr Address kDefaultGcptGuestBase = 0x80000000ULL;
constexpr uint64_t kGcptMagicNumber = 0xBEEFULL;
constexpr uint32_t kMstatusCsrAddress = 0x300U;
constexpr size_t kElfMagicSize = 4;

struct DefaultGcptLayout {
    size_t magic_number_cpt_addr = 0xECDB0;
    size_t pc_cpt_addr = 0xECDB8;
    size_t int_reg_cpt_addr = 0xEDDE0;
    size_t float_reg_cpt_addr = 0xEDEE8;
    size_t csr_reg_cpt_addr = 0xEDFF0;
};

std::string trim(const std::string& value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string shellEscape(const std::string& value) {
    std::string escaped = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            escaped += "'\\''";
        } else {
            escaped += ch;
        }
    }
    escaped += "'";
    return escaped;
}

uint64_t parseUnsignedValue(const std::string& text, const std::string& field_name) {
    try {
        size_t parsed = 0;
        const uint64_t value = std::stoull(trim(text), &parsed, 0);
        if (parsed != trim(text).size()) {
            throw SimulatorException("manifest 字段包含非法尾随内容: " + field_name + "=" + text);
        }
        return value;
    } catch (const std::exception&) {
        throw SimulatorException("manifest 字段无法解析整数: " + field_name + "=" + text);
    }
}

uint32_t extensionBitForChar(const char ch) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))) {
        case 'I':
            return static_cast<uint32_t>(Extension::I);
        case 'M':
            return static_cast<uint32_t>(Extension::M);
        case 'A':
            return static_cast<uint32_t>(Extension::A);
        case 'F':
            return static_cast<uint32_t>(Extension::F);
        case 'D':
            return static_cast<uint32_t>(Extension::D);
        case 'C':
            return static_cast<uint32_t>(Extension::C);
        default:
            throw SimulatorException(std::string("manifest 包含未知扩展: ") + ch);
    }
}

uint32_t parseExtensions(const std::string& value) {
    const std::string cleaned = trim(value);
    if (cleaned.empty()) {
        throw SimulatorException("manifest extensions 不能为空");
    }

    const char first = cleaned.front();
    if (std::isdigit(static_cast<unsigned char>(first))) {
        return static_cast<uint32_t>(parseUnsignedValue(cleaned, "extensions"));
    }
    if (cleaned.size() > 2 && cleaned[0] == '0' &&
        (cleaned[1] == 'x' || cleaned[1] == 'X')) {
        return static_cast<uint32_t>(parseUnsignedValue(cleaned, "extensions"));
    }

    uint32_t bits = 0;
    for (const char ch : cleaned) {
        if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',') {
            continue;
        }
        bits |= extensionBitForChar(ch);
    }
    return bits;
}

uint32_t parseExtensionsFromMisa(uint64_t misa) {
    uint32_t extensions = static_cast<uint32_t>(Extension::I);

    const auto hasMisaBit = [misa](char extension) {
        const unsigned bit = static_cast<unsigned>(std::toupper(static_cast<unsigned char>(extension)) - 'A');
        return bit < 64 && ((misa >> bit) & 0x1ULL) != 0;
    };
    const auto addIfPresent = [&](char misa_extension, Extension extension) {
        if (hasMisaBit(misa_extension)) {
            extensions |= static_cast<uint32_t>(extension);
        }
    };

    addIfPresent('M', Extension::M);
    addIfPresent('A', Extension::A);
    addIfPresent('F', Extension::F);
    addIfPresent('D', Extension::D);
    addIfPresent('C', Extension::C);
    return extensions;
}

std::vector<uint8_t> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        throw SimulatorException("无法打开 memory segment 文件: " + path.string());
    }

    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    if (size < 0) {
        throw SimulatorException("无法读取 memory segment 大小: " + path.string());
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            throw SimulatorException("读取 memory segment 失败: " + path.string());
        }
    }
    return bytes;
}

uint64_t readLittleEndian64FromFile(const std::filesystem::path& path,
                                    uint64_t file_size,
                                    size_t offset,
                                    const std::string& field_name);

void validateBuiltinZstdMagic(const BuiltinImageInfo& image) {
    const DefaultGcptLayout layout;
    const uint64_t magic =
        readLittleEndian64FromFile(image.path, image.size, layout.magic_number_cpt_addr, "magic_number");
    if (magic != kGcptMagicNumber) {
        std::ostringstream oss;
        oss << "checkpoint 不是受支持的 default_qemu_memlayout 格式，magic=0x" << std::hex << magic;
        throw SimulatorException(oss.str());
    }
}

uint32_t readBuiltinZstdExtensions(const BuiltinImageInfo& image) {
    const DefaultGcptLayout layout;
    const uint64_t misa =
        readLittleEndian64FromFile(
            image.path, image.size, layout.csr_reg_cpt_addr + 0x301ULL * sizeof(uint64_t), "misa");
    return parseExtensionsFromMisa(misa);
}

std::vector<uint8_t> loadFlatBinaryRestorer(const std::filesystem::path& restorer_path) {
    if (!std::filesystem::exists(restorer_path)) {
        throw SimulatorException("restorer 不存在: " + restorer_path.string());
    }
    if (!std::filesystem::is_regular_file(restorer_path)) {
        throw SimulatorException("restorer 不是普通文件: " + restorer_path.string());
    }

    std::vector<uint8_t> bytes = readBinaryFile(restorer_path);
    if (bytes.size() >= kElfMagicSize && bytes[0] == 0x7F && bytes[1] == 'E' && bytes[2] == 'L' &&
        bytes[3] == 'F') {
        throw SimulatorException("builtin-zstd assisted 模式只支持 flat binary restorer，不支持 ELF: " +
                                 restorer_path.string());
    }
    return bytes;
}

std::optional<std::filesystem::path> resolveBuiltinRestorerPath(const CheckpointRunConfig& config,
                                                                const CheckpointRecipeSpec& recipe) {
    const std::string explicit_restorer = trim(config.restorer_path);
    if (!explicit_restorer.empty()) {
        return std::filesystem::path(explicit_restorer);
    }

    const std::filesystem::path checkpoint_path(config.checkpoint_path);
    const std::filesystem::path point_dir = checkpoint_path.parent_path();
    const std::filesystem::path workload_dir = point_dir.parent_path();
    const std::filesystem::path checkpoint_root = workload_dir.parent_path();
    if (workload_dir.empty() || checkpoint_root.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path dataset_root = checkpoint_root.parent_path();
    if (dataset_root.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path candidate = dataset_root / "gcpt_bins" / recipe.workload_name;
    if (std::filesystem::exists(candidate)) {
        return candidate;
    }
    return std::nullopt;
}

uint64_t readLittleEndian64FromFile(const std::filesystem::path& path,
                                    uint64_t file_size,
                                    size_t offset,
                                    const std::string& field_name) {
    if (offset > file_size || file_size - offset < sizeof(uint64_t)) {
        throw SimulatorException("checkpoint 镜像缺少字段: " + field_name);
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        throw SimulatorException("无法打开 checkpoint 原始镜像: " + path.string());
    }
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream) {
        throw SimulatorException("checkpoint 镜像 seek 失败: " + field_name);
    }

    std::array<uint8_t, sizeof(uint64_t)> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw SimulatorException("checkpoint 镜像读取失败: " + field_name);
    }

    uint64_t value = 0;
    for (size_t i = 0; i < bytes.size(); ++i) {
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    }
    return value;
}

ProcessResult runProcess(const std::string& executable, const std::vector<std::string>& args) {
    if (trim(executable).empty()) {
        throw SimulatorException("外部进程路径不能为空");
    }

    std::string command = shellEscape(executable);
    for (const auto& arg : args) {
        command += " ";
        command += shellEscape(arg);
    }
    command += " 2>&1";

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        throw SimulatorException("无法启动外部进程: " + executable);
    }

    std::array<char, 4096> buffer{};
    std::string output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    if (status == -1) {
        throw SimulatorException("等待外部进程失败: " + executable);
    }

    ProcessResult result;
    result.output = output;
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        return result;
    }

    result.exit_code = 128;
    if (WIFSIGNALED(status)) {
        result.exit_code += WTERMSIG(status);
    }
    return result;
}

std::vector<std::string> makeProcessArgs(const CheckpointRunConfig& config,
                                         const CheckpointRecipeSpec& recipe) {
    return {
        config.checkpoint_path,
        config.recipe_path,
        recipe.task_script_path,
        recipe.run_script_path,
        config.restorer_path,
        config.output_dir,
        std::to_string(config.warmup_instructions),
        std::to_string(config.measure_instructions),
    };
}

void parseGprLine(const std::string& line, SnapshotBundle& snapshot) {
    const size_t left_bracket = line.find('[');
    const size_t right_bracket = line.find(']');
    const size_t equals = line.find('=');
    if (left_bracket == std::string::npos || right_bracket == std::string::npos ||
        equals == std::string::npos || right_bracket > equals) {
        throw SimulatorException("manifest gpr 行格式非法: " + line);
    }

    const uint64_t reg_index =
        parseUnsignedValue(line.substr(left_bracket + 1, right_bracket - left_bracket - 1), "gpr");
    if (reg_index >= snapshot.integer_regs.size()) {
        throw SimulatorException("manifest gpr 编号越界: " + std::to_string(reg_index));
    }
    snapshot.integer_regs[reg_index] = parseUnsignedValue(line.substr(equals + 1), "gpr");
}

void parseSegmentLine(const std::string& line,
                      const std::filesystem::path& output_dir,
                      SnapshotBundle& snapshot) {
    const std::string spec = line.substr(std::string("segment=").size());
    const size_t separator = spec.find(':');
    if (separator == std::string::npos) {
        throw SimulatorException("manifest segment 行格式非法: " + line);
    }

    MemorySegment segment;
    segment.base = parseUnsignedValue(spec.substr(0, separator), "segment.base");

    std::filesystem::path path = trim(spec.substr(separator + 1));
    if (path.empty()) {
        throw SimulatorException("manifest segment 缺少路径: " + line);
    }
    if (path.is_relative()) {
        path = output_dir / path;
    }
    segment.bytes = readBinaryFile(path);
    snapshot.memory_segments.push_back(std::move(segment));
}

void parseManifest(const std::string& manifest,
                   const std::filesystem::path& output_dir,
                   SnapshotBundle& snapshot) {
    std::istringstream input(manifest);
    std::string raw_line;
    while (std::getline(input, raw_line)) {
        const std::string line = trim(raw_line);
        if (line.empty() || startsWith(line, "#")) {
            continue;
        }

        if (startsWith(line, "pc=")) {
            snapshot.pc = parseUnsignedValue(line.substr(3), "pc");
            continue;
        }
        if (startsWith(line, "extensions=")) {
            snapshot.enabled_extensions = parseExtensions(line.substr(std::string("extensions=").size()));
            continue;
        }
        if (startsWith(line, "gpr[")) {
            parseGprLine(line, snapshot);
            continue;
        }
        if (startsWith(line, "segment=")) {
            parseSegmentLine(line, output_dir, snapshot);
            continue;
        }
    }
}

std::string formatProcessFailure(const std::string& kind,
                                 const std::string& executable,
                                 const ProcessResult& result) {
    std::ostringstream oss;
    oss << "外部 " << kind << " 执行失败(exit=" << result.exit_code << "): " << executable;
    const std::string text = trim(result.output);
    if (!text.empty()) {
        oss << " output=" << text;
    }
    return oss.str();
}

BuiltinImageInfo loadBuiltinZstdImage(const CheckpointRunConfig& config) {
    const std::filesystem::path output_dir(config.output_dir);
    std::filesystem::create_directories(output_dir);

    const std::filesystem::path raw_image_path = output_dir / "checkpoint.raw.bin";
    const ProcessResult decompress_result =
        runProcess("zstd",
                   {"-d", "-q", "-f", "--sparse", config.checkpoint_path, "-o", raw_image_path.string()});
    if (decompress_result.exit_code != 0) {
        throw SimulatorException(formatProcessFailure("zstd 解压", "zstd", decompress_result));
    }

    const uint64_t raw_size = static_cast<uint64_t>(std::filesystem::file_size(raw_image_path));
    return {raw_image_path, raw_size};
}

SnapshotBundle parseBuiltinZstdSnapshot(const CheckpointRecipeSpec& recipe,
                                        const BuiltinImageInfo& image) {
    const DefaultGcptLayout layout;

    validateBuiltinZstdMagic(image);

    SnapshotBundle snapshot;
    snapshot.recipe = recipe;
    snapshot.pc = readLittleEndian64FromFile(image.path, image.size, layout.pc_cpt_addr, "pc");

    for (size_t i = 0; i < snapshot.integer_regs.size(); ++i) {
        snapshot.integer_regs[i] =
            readLittleEndian64FromFile(
                image.path, image.size, layout.int_reg_cpt_addr + i * sizeof(uint64_t), "gpr");
    }
    for (size_t i = 0; i < snapshot.fp_regs.size(); ++i) {
        snapshot.fp_regs[i] =
            readLittleEndian64FromFile(
                image.path, image.size, layout.float_reg_cpt_addr + i * sizeof(uint64_t), "fpr");
    }
    for (uint32_t csr_index = 0; csr_index < 4096; ++csr_index) {
        const uint64_t csr_value =
            readLittleEndian64FromFile(
                image.path,
                image.size,
                layout.csr_reg_cpt_addr + static_cast<size_t>(csr_index) * sizeof(uint64_t),
                "csr");
        if (csr_value != 0) {
            snapshot.csr_values.push_back({csr_index, csr_value});
        }
    }

    snapshot.enabled_extensions = readBuiltinZstdExtensions(image);

    const uint64_t mstatus =
        readLittleEndian64FromFile(
            image.path,
            image.size,
            layout.csr_reg_cpt_addr + static_cast<size_t>(kMstatusCsrAddress) * sizeof(uint64_t),
            "mstatus");
    const auto privilege_mode = decodePrivilegeModeFromMstatusMpp(mstatus);
    if (!privilege_mode.has_value()) {
        std::ostringstream oss;
        oss << "checkpoint mstatus.MPP 非法: mstatus=0x" << std::hex << mstatus;
        throw SimulatorException(oss.str());
    }
    snapshot.privilege_mode = *privilege_mode;

    MemorySegment segment;
    segment.base = kDefaultGcptGuestBase;
    segment.file_path = image.path.string();
    segment.size = image.size;
    segment.ephemeral = true;
    snapshot.memory_segments.push_back(std::move(segment));
    return snapshot;
}

SnapshotBundle parseBuiltinZstdRestorerSnapshot(const CheckpointRecipeSpec& recipe,
                                                const BuiltinImageInfo& image,
                                                const std::filesystem::path& restorer_path) {
    validateBuiltinZstdMagic(image);

    SnapshotBundle snapshot;
    snapshot.recipe = recipe;
    snapshot.pc = 0;
    snapshot.enabled_extensions = readBuiltinZstdExtensions(image);
    snapshot.privilege_mode = PrivilegeMode::MACHINE;

    MemorySegment restorer_segment;
    restorer_segment.base = 0;
    restorer_segment.bytes = loadFlatBinaryRestorer(restorer_path);
    snapshot.memory_segments.push_back(std::move(restorer_segment));

    MemorySegment checkpoint_segment;
    checkpoint_segment.base = kDefaultGcptGuestBase;
    checkpoint_segment.file_path = image.path.string();
    checkpoint_segment.size = image.size;
    checkpoint_segment.ephemeral = true;
    snapshot.memory_segments.push_back(std::move(checkpoint_segment));
    return snapshot;
}

} // namespace

class BuiltinZstdCheckpointImporter : public ICheckpointImporter {
public:
    SnapshotBundle importCheckpoint(const CheckpointRunConfig& config) const override {
        const CheckpointRecipeSpec recipe =
            loadCheckpointRecipeSpec(config.checkpoint_path, config.recipe_path);
        const BuiltinImageInfo image = loadBuiltinZstdImage(config);
        const auto restorer_path = resolveBuiltinRestorerPath(config, recipe);
        if (restorer_path.has_value()) {
            return parseBuiltinZstdRestorerSnapshot(recipe, image, *restorer_path);
        }
        return parseBuiltinZstdSnapshot(recipe, image);
    }
};

ExternalProcessCheckpointImporter::ExternalProcessCheckpointImporter(std::string importer_command)
    : importer_command_(std::move(importer_command)) {}

SnapshotBundle ExternalProcessCheckpointImporter::importCheckpoint(
    const CheckpointRunConfig& config) const {
    if (trim(config.restorer_path).empty()) {
        throw SimulatorException("checkpoint importer 缺少 restorer_path");
    }
    if (trim(config.output_dir).empty()) {
        throw SimulatorException("checkpoint importer 缺少 output_dir");
    }

    const CheckpointRecipeSpec recipe =
        loadCheckpointRecipeSpec(config.checkpoint_path, config.recipe_path);
    std::filesystem::create_directories(config.output_dir);

    const std::vector<std::string> args = makeProcessArgs(config, recipe);

    const ProcessResult restorer_result = runProcess(config.restorer_path, args);
    if (restorer_result.exit_code != 0) {
        throw SimulatorException(formatProcessFailure("restorer", config.restorer_path, restorer_result));
    }

    const ProcessResult importer_result = runProcess(importer_command_, args);
    if (importer_result.exit_code != 0) {
        throw SimulatorException(formatProcessFailure("importer", importer_command_, importer_result));
    }

    SnapshotBundle snapshot;
    snapshot.recipe = recipe;
    parseManifest(importer_result.output, config.output_dir, snapshot);

    if (snapshot.memory_segments.empty()) {
        throw SimulatorException("checkpoint manifest 未提供 memory segment");
    }

    return snapshot;
}

std::unique_ptr<ICheckpointImporter> createCheckpointImporter(const std::string& importer_name) {
    if (trim(importer_name).empty() || importer_name == kBuiltinZstdImporterName) {
        return std::make_unique<BuiltinZstdCheckpointImporter>();
    }
    return std::make_unique<ExternalProcessCheckpointImporter>(importer_name);
}

} // namespace riscv
