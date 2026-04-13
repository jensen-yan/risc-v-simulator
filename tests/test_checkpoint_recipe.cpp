#include <gtest/gtest.h>

#include "system/checkpoint_recipe.h"

#include <filesystem>
#include <fstream>

namespace riscv {

namespace {

std::filesystem::path resetTempDir(const std::string& name) {
    const auto temp_dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(temp_dir);
    return temp_dir;
}

void touchFile(const std::filesystem::path& path) {
    std::ofstream(path.string()).put('\n');
}

TEST(CheckpointRecipeTest, ParsesCheckpointPathAndRecipeScriptWithNon755Mode) {
    const auto temp_dir = resetTempDir("checkpoint_recipe_test");
    std::filesystem::create_directories(temp_dir / "bzip2_source" / "555");

    const auto checkpoint_path = temp_dir / "bzip2_source" / "555" / "_555_0.026526_.zstd";
    touchFile(checkpoint_path);

    const auto recipe_path = temp_dir / "bzip2_source_initramfs-spec.txt";
    std::ofstream recipe(recipe_path.string());
    recipe << "file   /spec0/task0.sh   /tmp/spec/bzip2_source_task0.sh   700   0   0\n";
    recipe << "file /spec0/run.sh /tmp/spec/bzip2_source_run.sh 644 0 0\n";
    recipe.close();

    const CheckpointRecipeSpec spec =
        loadCheckpointRecipeSpec(checkpoint_path.string(), recipe_path.string());

    EXPECT_EQ(spec.workload_name, "bzip2_source");
    EXPECT_EQ(spec.point_id, "555");
    EXPECT_DOUBLE_EQ(spec.weight, 0.026526);
    EXPECT_EQ(spec.task_script_path, "/tmp/spec/bzip2_source_task0.sh");
    EXPECT_EQ(spec.run_script_path, "/tmp/spec/bzip2_source_run.sh");
}

TEST(CheckpointRecipeTest, RejectsRecipeWithoutTaskScript) {
    const auto temp_dir = resetTempDir("checkpoint_recipe_test_missing");
    std::filesystem::create_directories(temp_dir / "gcc_200" / "28");

    const auto checkpoint_path = temp_dir / "gcc_200" / "28" / "_28_0.123456_.zstd";
    touchFile(checkpoint_path);

    const auto recipe_path = temp_dir / "gcc_200_initramfs-spec.txt";
    std::ofstream recipe(recipe_path.string());
    recipe << "file /spec0/run.sh /tmp/spec/gcc_200_run.sh 755 0 0\n";
    recipe.close();

    EXPECT_THROW(loadCheckpointRecipeSpec(checkpoint_path.string(), recipe_path.string()),
                 SimulatorException);
}

TEST(CheckpointRecipeTest, RejectsRecipeWithoutRunScript) {
    const auto temp_dir = resetTempDir("checkpoint_recipe_test_missing_run");
    std::filesystem::create_directories(temp_dir / "mcf_base" / "99");

    const auto checkpoint_path = temp_dir / "mcf_base" / "99" / "_99_0.500000_.zstd";
    touchFile(checkpoint_path);

    const auto recipe_path = temp_dir / "mcf_base_initramfs-spec.txt";
    std::ofstream recipe(recipe_path.string());
    recipe << "file /spec0/task0.sh /tmp/spec/mcf_base_task0.sh 755 0 0\n";
    recipe.close();

    EXPECT_THROW(loadCheckpointRecipeSpec(checkpoint_path.string(), recipe_path.string()),
                 SimulatorException);
}

TEST(CheckpointRecipeTest, RejectsMalformedRecipeLine) {
    const auto temp_dir = resetTempDir("checkpoint_recipe_test_malformed");
    std::filesystem::create_directories(temp_dir / "omnetpp_ref" / "7");

    const auto checkpoint_path = temp_dir / "omnetpp_ref" / "7" / "_7_0.031250_.zstd";
    touchFile(checkpoint_path);

    const auto recipe_path = temp_dir / "omnetpp_ref_initramfs-spec.txt";
    std::ofstream recipe(recipe_path.string());
    recipe << "file /spec0/task0.sh /tmp/spec/omnetpp_ref_task0.sh 755 0\n";
    recipe << "file /spec0/run.sh /tmp/spec/omnetpp_ref_run.sh 755 0 0\n";
    recipe.close();

    EXPECT_THROW(loadCheckpointRecipeSpec(checkpoint_path.string(), recipe_path.string()),
                 SimulatorException);
}

} // namespace

} // namespace riscv
