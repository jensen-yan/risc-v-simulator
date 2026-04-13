target_sources(risc-v-sim-lib PRIVATE
    checkpoint_importer.cpp
    checkpoint_runner.cpp
    checkpoint_recipe.cpp
    simulator.cpp
    elf_loader.cpp
    syscall_handler.cpp
    difftest.cpp
    pipeline_tracer.cpp
)
