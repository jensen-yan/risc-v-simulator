#include "system/checkpoint_recipe.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <string>

namespace riscv {

namespace {

struct RecipeFileEntry {
    std::string guest_path;
    std::string host_path;
};

bool isWhitespaceOnly(const std::string& line) {
    return line.find_first_not_of(" \t\r\n") == std::string::npos;
}

bool parseRecipeFileEntry(const std::string& line, RecipeFileEntry& entry) {
    if (isWhitespaceOnly(line)) {
        return false;
    }

    std::istringstream iss(line);
    std::string directive;
    iss >> directive;
    if (directive.empty()) {
        return false;
    }
    if (directive != "file") {
        return false;
    }

    std::string mode;
    std::string uid;
    std::string gid;
    std::string trailing;
    if (!(iss >> entry.guest_path >> entry.host_path >> mode >> uid >> gid) || (iss >> trailing)) {
        throw SimulatorException("recipe file 行格式非法: " + line);
    }

    return true;
}

} // namespace

CheckpointRecipeSpec loadCheckpointRecipeSpec(const std::string& checkpoint_path,
                                              const std::string& recipe_path) {
    namespace fs = std::filesystem;

    if (!fs::exists(checkpoint_path)) {
        throw SimulatorException("checkpoint 不存在: " + checkpoint_path);
    }
    if (!fs::exists(recipe_path)) {
        throw SimulatorException("checkpoint recipe 不存在: " + recipe_path);
    }

    const fs::path checkpoint_fs_path(checkpoint_path);
    const fs::path point_dir = checkpoint_fs_path.parent_path();
    const fs::path workload_dir = point_dir.parent_path();
    if (point_dir.empty() || workload_dir.empty()) {
        throw SimulatorException("checkpoint 路径无法解析 workload/point: " + checkpoint_path);
    }

    CheckpointRecipeSpec spec;
    spec.checkpoint_path = checkpoint_path;
    spec.recipe_path = recipe_path;
    spec.workload_name = workload_dir.filename().string();
    spec.point_id = point_dir.filename().string();

    const std::regex filename_re(R"(_([0-9]+)_([0-9]+\.[0-9]+)_\.zstd$)");
    std::smatch match;
    const std::string filename = checkpoint_fs_path.filename().string();
    if (!std::regex_match(filename, match, filename_re)) {
        throw SimulatorException("无法从 checkpoint 文件名解析 weight: " + filename);
    }
    spec.weight = std::stod(match[2].str());

    std::ifstream recipe_stream(recipe_path);
    if (!recipe_stream.is_open()) {
        throw SimulatorException("无法打开 checkpoint recipe: " + recipe_path);
    }

    std::string line;
    RecipeFileEntry entry;
    while (std::getline(recipe_stream, line)) {
        if (!parseRecipeFileEntry(line, entry)) {
            continue;
        }
        if (spec.task_script_path.empty() && entry.guest_path == "/spec0/task0.sh") {
            spec.task_script_path = entry.host_path;
        }
        if (spec.run_script_path.empty() && entry.guest_path == "/spec0/run.sh") {
            spec.run_script_path = entry.host_path;
        }
    }

    if (spec.task_script_path.empty()) {
        throw SimulatorException("recipe 缺少 /spec0/task0.sh 定义: " + recipe_path);
    }
    if (spec.run_script_path.empty()) {
        throw SimulatorException("recipe 缺少 /spec0/run.sh 定义: " + recipe_path);
    }

    return spec;
}

} // namespace riscv
