#include <gtest/gtest.h>

#include "system/checkpoint_importer.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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

bool hasZstdBinary() {
    return std::system("zstd --version >/dev/null 2>&1") == 0;
}

void writeU64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value) {
    ASSERT_LE(offset + sizeof(uint64_t), bytes.size());
    for (size_t i = 0; i < sizeof(uint64_t); ++i) {
        bytes[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFFU);
    }
}

void writeU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    ASSERT_LE(offset + sizeof(uint32_t), bytes.size());
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        bytes[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFFU);
    }
}

void writeZstdFile(const std::filesystem::path& raw_path, const std::filesystem::path& zstd_path) {
    std::ostringstream command;
    command << "zstd -q -f "
            << raw_path.string()
            << " -o "
            << zstd_path.string()
            << " >/dev/null 2>&1";
    ASSERT_EQ(std::system(command.str().c_str()), 0);
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

TEST(CheckpointImporterTest, BuiltinZstdImporterParsesDefaultGcptLayout) {
    if (!hasZstdBinary()) {
        GTEST_SKIP() << "缺少 zstd 命令";
    }

    const auto temp_dir = resetTempDir("checkpoint_importer_builtin_zstd");
    const auto checkpoint_path =
        temp_dir / "bzip2_source" / "555" / "_555_0.026526_.zstd";
    const auto recipe_path = temp_dir / "scripts" / "bzip2_source_initramfs-spec.txt";
    const auto raw_path = temp_dir / "image.bin";

    std::vector<uint8_t> image(1024 * 1024, 0);
    writeU32(image, 0x100, 0x00100093U); // addi x1, x0, 1
    writeU64(image, 0xECDB0, 0xBEEFULL);
    writeU64(image, 0xECDB8, 0x100ULL);
    writeU64(image, 0xEDDE0 + 1 * 8, 0x123ULL);
    writeU64(image, 0xEDDE0 + 2 * 8, 0x456ULL);
    writeU64(image, 0xEDFF0 + 0x301 * 8, 0x800000000014112DULL); // misa with IMAC
    writeBinaryFile(raw_path, image);

    std::filesystem::create_directories(checkpoint_path.parent_path());
    writeZstdFile(raw_path, checkpoint_path);
    writeRecipeFile(recipe_path);

    CheckpointRunConfig config;
    config.checkpoint_path = checkpoint_path.string();
    config.recipe_path = recipe_path.string();
    config.importer_name = "builtin-zstd";
    config.output_dir = (temp_dir / "output").string();

    auto importer = createCheckpointImporter(config.importer_name);
    const SnapshotBundle snapshot = importer->importCheckpoint(config);

    ASSERT_EQ(snapshot.memory_segments.size(), 1u);
    EXPECT_EQ(snapshot.memory_segments[0].base, 0x80000000ULL);
    EXPECT_TRUE(snapshot.memory_segments[0].isFileBacked());
    EXPECT_EQ(snapshot.memory_segments[0].size, image.size());
    EXPECT_TRUE(std::filesystem::exists(snapshot.memory_segments[0].file_path));
    EXPECT_EQ(snapshot.pc, 0x100ULL);
    EXPECT_EQ(snapshot.integer_regs[1], 0x123ULL);
    EXPECT_EQ(snapshot.integer_regs[2], 0x456ULL);
    EXPECT_EQ(snapshot.recipe.workload_name, "bzip2_source");
    EXPECT_EQ(snapshot.recipe.point_id, "555");
    EXPECT_TRUE((snapshot.enabled_extensions & static_cast<uint32_t>(Extension::I)) != 0);
    EXPECT_TRUE((snapshot.enabled_extensions & static_cast<uint32_t>(Extension::M)) != 0);
    EXPECT_TRUE((snapshot.enabled_extensions & static_cast<uint32_t>(Extension::A)) != 0);
    EXPECT_TRUE((snapshot.enabled_extensions & static_cast<uint32_t>(Extension::C)) != 0);
}

TEST(CheckpointImporterTest, BuiltinZstdImporterCanAutoDiscoverRecipePath) {
    if (!hasZstdBinary()) {
        GTEST_SKIP() << "缺少 zstd 命令";
    }

    const auto temp_dir = resetTempDir("checkpoint_importer_builtin_autorecipe");
    const auto root_dir = temp_dir / "spec06_gcpt";
    const auto checkpoint_path =
        root_dir / "checkpoint-0-0-0" / "mcf" / "99" / "_99_0.500000_.zstd";
    const auto recipe_path = root_dir / "scripts" / "mcf_initramfs-spec.txt";
    const auto raw_path = temp_dir / "image.bin";

    std::vector<uint8_t> image(1024 * 1024, 0);
    writeU64(image, 0xECDB0, 0xBEEFULL);
    writeU64(image, 0xECDB8, 0x0ULL);
    writeBinaryFile(raw_path, image);

    std::filesystem::create_directories(checkpoint_path.parent_path());
    writeZstdFile(raw_path, checkpoint_path);
    writeRecipeFile(recipe_path);

    CheckpointRunConfig config;
    config.checkpoint_path = checkpoint_path.string();
    config.importer_name = "builtin-zstd";
    config.output_dir = (temp_dir / "output").string();

    auto importer = createCheckpointImporter(config.importer_name);
    const SnapshotBundle snapshot = importer->importCheckpoint(config);

    EXPECT_EQ(snapshot.recipe.recipe_path, recipe_path.string());
    EXPECT_EQ(snapshot.recipe.workload_name, "mcf");
    EXPECT_EQ(snapshot.recipe.point_id, "99");
}

TEST(CheckpointImporterTest, BuiltinZstdImporterUsesExplicitFlatRestorerBeforeAutoDiscovery) {
    if (!hasZstdBinary()) {
        GTEST_SKIP() << "缺少 zstd 命令";
    }

    const auto temp_dir = resetTempDir("checkpoint_importer_builtin_assisted_explicit");
    const auto dataset_root = temp_dir / "spec06_gcpt";
    const auto checkpoint_path =
        dataset_root / "checkpoint-0-0-0" / "bzip2_source" / "555" / "_555_0.026526_.zstd";
    const auto recipe_path = dataset_root / "scripts" / "bzip2_source_initramfs-spec.txt";
    const auto explicit_restorer_path = temp_dir / "explicit_restorer.bin";
    const auto auto_restorer_path = dataset_root / "gcpt_bins" / "bzip2_source";
    const auto raw_path = temp_dir / "image.bin";

    std::vector<uint8_t> image(1024 * 1024, 0);
    writeU64(image, 0xECDB0, 0xBEEFULL);
    writeU64(image, 0xECDB8, 0x100ULL);
    writeU64(image, 0xEDDE0 + 1 * 8, 0x123ULL);
    writeU64(image, 0xEDFF0 + 0x301 * 8, 0x800000000014112DULL); // misa with IMAC
    writeBinaryFile(raw_path, image);

    std::filesystem::create_directories(checkpoint_path.parent_path());
    writeZstdFile(raw_path, checkpoint_path);
    writeRecipeFile(recipe_path);
    writeBinaryFile(explicit_restorer_path, {0x13, 0x05, 0x70, 0x00});
    writeBinaryFile(auto_restorer_path, {0x37, 0x05, 0x00, 0x80});

    CheckpointRunConfig config;
    config.checkpoint_path = checkpoint_path.string();
    config.recipe_path = recipe_path.string();
    config.importer_name = "builtin-zstd";
    config.restorer_path = explicit_restorer_path.string();
    config.output_dir = (temp_dir / "output").string();

    auto importer = createCheckpointImporter(config.importer_name);
    const SnapshotBundle snapshot = importer->importCheckpoint(config);

    ASSERT_EQ(snapshot.memory_segments.size(), 2u);
    EXPECT_EQ(snapshot.pc, 0ULL);
    ASSERT_TRUE(snapshot.privilege_mode.has_value());
    EXPECT_EQ(*snapshot.privilege_mode, PrivilegeMode::MACHINE);
    EXPECT_EQ(snapshot.integer_regs[1], 0ULL);
    EXPECT_TRUE(snapshot.csr_values.empty());

    const auto low_it = std::find_if(snapshot.memory_segments.begin(),
                                     snapshot.memory_segments.end(),
                                     [](const auto& segment) { return segment.base == 0x0ULL; });
    ASSERT_NE(low_it, snapshot.memory_segments.end());
    const std::vector<uint8_t> expected_explicit_restorer = {0x13, 0x05, 0x70, 0x00};
    EXPECT_EQ(low_it->bytes, expected_explicit_restorer);

    const auto high_it = std::find_if(snapshot.memory_segments.begin(),
                                      snapshot.memory_segments.end(),
                                      [](const auto& segment) { return segment.base == 0x80000000ULL; });
    ASSERT_NE(high_it, snapshot.memory_segments.end());
    EXPECT_TRUE(high_it->isFileBacked());
    EXPECT_EQ(high_it->size, image.size());
    EXPECT_TRUE(std::filesystem::exists(high_it->file_path));

    EXPECT_TRUE((snapshot.enabled_extensions & static_cast<uint32_t>(Extension::I)) != 0);
    EXPECT_TRUE((snapshot.enabled_extensions & static_cast<uint32_t>(Extension::M)) != 0);
    EXPECT_TRUE((snapshot.enabled_extensions & static_cast<uint32_t>(Extension::A)) != 0);
    EXPECT_TRUE((snapshot.enabled_extensions & static_cast<uint32_t>(Extension::C)) != 0);
}

TEST(CheckpointImporterTest, BuiltinZstdImporterAutoDiscoversFlatRestorerAndBuildsAssistedSnapshot) {
    if (!hasZstdBinary()) {
        GTEST_SKIP() << "缺少 zstd 命令";
    }

    const auto temp_dir = resetTempDir("checkpoint_importer_builtin_assisted_auto");
    const auto dataset_root = temp_dir / "spec06_gcpt";
    const auto checkpoint_path =
        dataset_root / "checkpoint-0-0-0" / "mcf" / "99" / "_99_0.500000_.zstd";
    const auto recipe_path = dataset_root / "scripts" / "mcf_initramfs-spec.txt";
    const auto auto_restorer_path = dataset_root / "gcpt_bins" / "mcf";
    const auto raw_path = temp_dir / "image.bin";

    std::vector<uint8_t> image(1024 * 1024, 0);
    writeU64(image, 0xECDB0, 0xBEEFULL);
    writeU64(image, 0xECDB8, 0x400ULL);
    writeU64(image, 0xEDDE0 + 2 * 8, 0x456ULL);
    writeU64(image, 0xEDFF0 + 0x180 * 8, 0x8000000000001001ULL);
    writeBinaryFile(raw_path, image);

    std::filesystem::create_directories(checkpoint_path.parent_path());
    writeZstdFile(raw_path, checkpoint_path);
    writeRecipeFile(recipe_path);
    writeBinaryFile(auto_restorer_path, {0x93, 0x05, 0x10, 0x00, 0x13, 0x06, 0x20, 0x00});

    CheckpointRunConfig config;
    config.checkpoint_path = checkpoint_path.string();
    config.recipe_path = recipe_path.string();
    config.importer_name = "builtin-zstd";
    config.output_dir = (temp_dir / "output").string();

    auto importer = createCheckpointImporter(config.importer_name);
    const SnapshotBundle snapshot = importer->importCheckpoint(config);

    ASSERT_EQ(snapshot.memory_segments.size(), 2u);
    EXPECT_EQ(snapshot.pc, 0ULL);
    ASSERT_TRUE(snapshot.privilege_mode.has_value());
    EXPECT_EQ(*snapshot.privilege_mode, PrivilegeMode::MACHINE);
    EXPECT_EQ(snapshot.integer_regs[2], 0ULL);
    EXPECT_TRUE(snapshot.csr_values.empty());

    const auto low_it = std::find_if(snapshot.memory_segments.begin(),
                                     snapshot.memory_segments.end(),
                                     [](const auto& segment) { return segment.base == 0x0ULL; });
    ASSERT_NE(low_it, snapshot.memory_segments.end());
    const std::vector<uint8_t> expected_auto_restorer = {
        0x93, 0x05, 0x10, 0x00, 0x13, 0x06, 0x20, 0x00};
    EXPECT_EQ(low_it->bytes, expected_auto_restorer);

    const auto high_it = std::find_if(snapshot.memory_segments.begin(),
                                      snapshot.memory_segments.end(),
                                      [](const auto& segment) { return segment.base == 0x80000000ULL; });
    ASSERT_NE(high_it, snapshot.memory_segments.end());
    EXPECT_TRUE(high_it->isFileBacked());
    EXPECT_EQ(high_it->size, image.size());
}

TEST(CheckpointImporterTest, BuiltinZstdImporterRejectsElfRestorer) {
    if (!hasZstdBinary()) {
        GTEST_SKIP() << "缺少 zstd 命令";
    }

    const auto temp_dir = resetTempDir("checkpoint_importer_builtin_assisted_elf");
    const auto checkpoint_path =
        temp_dir / "bzip2_source" / "555" / "_555_0.026526_.zstd";
    const auto recipe_path = temp_dir / "scripts" / "bzip2_source_initramfs-spec.txt";
    const auto restorer_path = temp_dir / "restorer.elf";
    const auto raw_path = temp_dir / "image.bin";

    std::vector<uint8_t> image(1024 * 1024, 0);
    writeU64(image, 0xECDB0, 0xBEEFULL);
    writeBinaryFile(raw_path, image);

    std::filesystem::create_directories(checkpoint_path.parent_path());
    writeZstdFile(raw_path, checkpoint_path);
    writeRecipeFile(recipe_path);
    writeBinaryFile(restorer_path, {0x7F, 'E', 'L', 'F', 0x02, 0x01, 0x01, 0x00});

    CheckpointRunConfig config;
    config.checkpoint_path = checkpoint_path.string();
    config.recipe_path = recipe_path.string();
    config.importer_name = "builtin-zstd";
    config.restorer_path = restorer_path.string();
    config.output_dir = (temp_dir / "output").string();

    auto importer = createCheckpointImporter(config.importer_name);
    try {
        static_cast<void>(importer->importCheckpoint(config));
        FAIL() << "ELF restorer 应被拒绝";
    } catch (const SimulatorException& ex) {
        EXPECT_NE(std::string(ex.what()).find("flat binary"), std::string::npos);
        EXPECT_NE(std::string(ex.what()).find("ELF"), std::string::npos);
    }
}

TEST(CheckpointImporterTest, BuiltinZstdImporterAcceptsVirtualMemoryCheckpointAndPreservesSatp) {
    if (!hasZstdBinary()) {
        GTEST_SKIP() << "缺少 zstd 命令";
    }

    const auto temp_dir = resetTempDir("checkpoint_importer_builtin_vm_reject");
    const auto checkpoint_path =
        temp_dir / "perlbench_splitmail" / "12" / "_12_0.100000_.zstd";
    const auto recipe_path = temp_dir / "scripts" / "perlbench_splitmail_initramfs-spec.txt";
    const auto raw_path = temp_dir / "image.bin";

    std::vector<uint8_t> image(1024 * 1024, 0);
    writeU64(image, 0xECDB0, 0xBEEFULL);
    writeU64(image, 0xECDB8, 0x1000ULL);
    writeU64(image, 0xEDFF0 + 0x180 * 8, 0x8000000000001001ULL); // satp != 0
    writeBinaryFile(raw_path, image);

    std::filesystem::create_directories(checkpoint_path.parent_path());
    writeZstdFile(raw_path, checkpoint_path);
    writeRecipeFile(recipe_path);

    CheckpointRunConfig config;
    config.checkpoint_path = checkpoint_path.string();
    config.recipe_path = recipe_path.string();
    config.importer_name = "builtin-zstd";
    config.output_dir = (temp_dir / "output").string();

    auto importer = createCheckpointImporter(config.importer_name);
    const SnapshotBundle snapshot = importer->importCheckpoint(config);

    EXPECT_EQ(snapshot.pc, 0x1000ULL);
    auto satp_it = std::find_if(snapshot.csr_values.begin(),
                                snapshot.csr_values.end(),
                                [](const auto& csr_entry) { return csr_entry.first == 0x180U; });
    ASSERT_NE(satp_it, snapshot.csr_values.end());
    EXPECT_EQ(satp_it->second, 0x8000000000001001ULL);
}

TEST(CheckpointImporterTest, BuiltinZstdImporterDerivesPrivilegeModeFromMstatusMpp) {
    if (!hasZstdBinary()) {
        GTEST_SKIP() << "缺少 zstd 命令";
    }

    const auto temp_dir = resetTempDir("checkpoint_importer_builtin_privilege_mode");
    const auto checkpoint_path = temp_dir / "bzip2_source" / "555" / "_555_0.026526_.zstd";
    const auto recipe_path = temp_dir / "scripts" / "bzip2_source_initramfs-spec.txt";
    const auto raw_path = temp_dir / "image.bin";

    std::vector<uint8_t> image(1024 * 1024, 0);
    writeU64(image, 0xECDB0, 0xBEEFULL);
    writeU64(image, 0xECDB8, 0x1000ULL);
    writeU64(image, 0xEDFF0 + 0x180 * 8, 0x8000000000001001ULL); // satp != 0
    writeU64(image, 0xEDFF0 + 0x300 * 8, 0x0000000A00006022ULL); // MPP = 0 -> U-mode
    writeBinaryFile(raw_path, image);

    std::filesystem::create_directories(checkpoint_path.parent_path());
    writeZstdFile(raw_path, checkpoint_path);
    writeRecipeFile(recipe_path);

    CheckpointRunConfig config;
    config.checkpoint_path = checkpoint_path.string();
    config.recipe_path = recipe_path.string();
    config.importer_name = "builtin-zstd";
    config.output_dir = (temp_dir / "output").string();

    auto importer = createCheckpointImporter(config.importer_name);
    const SnapshotBundle snapshot = importer->importCheckpoint(config);

    ASSERT_TRUE(snapshot.privilege_mode.has_value());
    EXPECT_EQ(*snapshot.privilege_mode, PrivilegeMode::USER);
}

} // namespace riscv
