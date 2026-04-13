#include <gtest/gtest.h>

#include "system/checkpoint_importer.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace riscv {

namespace {

std::filesystem::path resetTempDir(const std::string& name) {
    const auto temp_dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);
    return temp_dir;
}

void touchFile(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path.string()).put('\n');
}

void writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path.string(), std::ios::binary);
    stream << content;
}

void writeBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path.string(), std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

void writeExecutableScript(const std::filesystem::path& path, const std::string& body) {
    writeTextFile(path, body);
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write |
                                     std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_read |
                                     std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_read |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::replace);
}

CheckpointRunConfig makeRunConfig(const std::filesystem::path& checkpoint_path,
                                  const std::filesystem::path& recipe_path,
                                  const std::filesystem::path& importer_path,
                                  const std::filesystem::path& restorer_path,
                                  const std::filesystem::path& output_dir) {
    CheckpointRunConfig config;
    config.checkpoint_path = checkpoint_path.string();
    config.recipe_path = recipe_path.string();
    config.importer_name = importer_path.string();
    config.restorer_path = restorer_path.string();
    config.output_dir = output_dir.string();
    config.warmup_instructions = 123;
    config.measure_instructions = 456;
    return config;
}

void writeRecipeFile(const std::filesystem::path& recipe_path) {
    writeTextFile(recipe_path,
                  "file /spec0/task0.sh /tmp/spec/task0.sh 700 0 0\n"
                  "file /spec0/run.sh /tmp/spec/run.sh 644 0 0\n");
}

} // namespace

TEST(CheckpointImporterTest, ExternalImporterProducesSnapshotBundleFromManifest) {
    const auto temp_dir = resetTempDir("checkpoint_importer_success");
    const auto checkpoint_path =
        temp_dir / "bzip2_source" / "555" / "_555_0.026526_.zstd";
    const auto recipe_path = temp_dir / "bzip2_source_initramfs-spec.txt";
    const auto importer_path = temp_dir / "fake_importer.sh";
    const auto restorer_path = temp_dir / "fake_restorer.sh";
    const auto output_dir = temp_dir / "output";

    touchFile(checkpoint_path);
    writeRecipeFile(recipe_path);

    writeExecutableScript(restorer_path,
                          "#!/usr/bin/env bash\n"
                          "set -euo pipefail\n"
                          "output_dir=\"$6\"\n"
                          "mkdir -p \"$output_dir\"\n"
                          "printf '\\x01\\x02\\x03\\x04' > \"$output_dir/segment.bin\"\n");
    writeExecutableScript(importer_path,
                          "#!/usr/bin/env bash\n"
                          "set -euo pipefail\n"
                          "output_dir=\"$6\"\n"
                          "printf 'pc=0x80000000\\n'\n"
                          "printf 'extensions=IMC\\n'\n"
                          "printf 'gpr[1]=0x123\\n'\n"
                          "printf 'gpr[17]=0x5d\\n'\n"
                          "printf 'segment=0x80000000:%s/segment.bin\\n' \"$output_dir\"\n");

    auto importer = createCheckpointImporter(importer_path.string());
    const SnapshotBundle snapshot =
        importer->importCheckpoint(makeRunConfig(checkpoint_path,
                                                 recipe_path,
                                                 importer_path,
                                                 restorer_path,
                                                 output_dir));

    const uint32_t expected_extensions = static_cast<uint32_t>(Extension::I) |
                                         static_cast<uint32_t>(Extension::M) |
                                         static_cast<uint32_t>(Extension::C);
    ASSERT_EQ(snapshot.memory_segments.size(), 1u);
    EXPECT_EQ(snapshot.pc, 0x80000000u);
    EXPECT_EQ(snapshot.enabled_extensions, expected_extensions);
    EXPECT_EQ(snapshot.integer_regs[1], 0x123u);
    EXPECT_EQ(snapshot.integer_regs[17], 0x5du);
    EXPECT_EQ(snapshot.memory_segments[0].base, 0x80000000u);
    EXPECT_EQ(snapshot.memory_segments[0].bytes, std::vector<uint8_t>({1, 2, 3, 4}));
    EXPECT_EQ(snapshot.recipe.workload_name, "bzip2_source");
    EXPECT_EQ(snapshot.recipe.point_id, "555");
}

TEST(CheckpointImporterTest, ImportFailsWhenRestorerPathIsMissing) {
    const auto temp_dir = resetTempDir("checkpoint_importer_missing_restorer");
    const auto checkpoint_path = temp_dir / "gcc_200" / "28" / "_28_0.123456_.zstd";
    const auto recipe_path = temp_dir / "gcc_200_initramfs-spec.txt";
    const auto importer_path = temp_dir / "fake_importer.sh";

    touchFile(checkpoint_path);
    writeRecipeFile(recipe_path);
    writeExecutableScript(importer_path,
                          "#!/usr/bin/env bash\n"
                          "set -euo pipefail\n"
                          "printf 'pc=0x0\\n'\n");

    auto importer = createCheckpointImporter(importer_path.string());
    CheckpointRunConfig config =
        makeRunConfig(checkpoint_path, recipe_path, importer_path, "", temp_dir / "output");
    config.restorer_path.clear();

    EXPECT_THROW(importer->importCheckpoint(config), SimulatorException);
}

TEST(CheckpointImporterTest, ImportFailsWhenExternalImporterReturnsNonZero) {
    const auto temp_dir = resetTempDir("checkpoint_importer_non_zero");
    const auto checkpoint_path = temp_dir / "mcf_base" / "99" / "_99_0.500000_.zstd";
    const auto recipe_path = temp_dir / "mcf_base_initramfs-spec.txt";
    const auto importer_path = temp_dir / "failing_importer.sh";
    const auto restorer_path = temp_dir / "fake_restorer.sh";

    touchFile(checkpoint_path);
    writeRecipeFile(recipe_path);
    writeExecutableScript(restorer_path,
                          "#!/usr/bin/env bash\n"
                          "set -euo pipefail\n"
                          "exit 0\n");
    writeExecutableScript(importer_path,
                          "#!/usr/bin/env bash\n"
                          "set -euo pipefail\n"
                          "echo 'importer failed' >&2\n"
                          "exit 7\n");

    auto importer = createCheckpointImporter(importer_path.string());
    EXPECT_THROW(importer->importCheckpoint(makeRunConfig(checkpoint_path,
                                                          recipe_path,
                                                          importer_path,
                                                          restorer_path,
                                                          temp_dir / "output")),
                 SimulatorException);
}

TEST(CheckpointImporterTest, ImportFailsWhenManifestHasNoMemorySegment) {
    const auto temp_dir = resetTempDir("checkpoint_importer_missing_segment");
    const auto checkpoint_path = temp_dir / "omnetpp_ref" / "7" / "_7_0.031250_.zstd";
    const auto recipe_path = temp_dir / "omnetpp_ref_initramfs-spec.txt";
    const auto importer_path = temp_dir / "fake_importer.sh";
    const auto restorer_path = temp_dir / "fake_restorer.sh";

    touchFile(checkpoint_path);
    writeRecipeFile(recipe_path);
    writeExecutableScript(restorer_path,
                          "#!/usr/bin/env bash\n"
                          "set -euo pipefail\n"
                          "exit 0\n");
    writeExecutableScript(importer_path,
                          "#!/usr/bin/env bash\n"
                          "set -euo pipefail\n"
                          "printf 'pc=0x1000\\n'\n"
                          "printf 'gpr[2]=0x456\\n'\n");

    auto importer = createCheckpointImporter(importer_path.string());
    EXPECT_THROW(importer->importCheckpoint(makeRunConfig(checkpoint_path,
                                                          recipe_path,
                                                          importer_path,
                                                          restorer_path,
                                                          temp_dir / "output")),
                 SimulatorException);
}

} // namespace riscv
