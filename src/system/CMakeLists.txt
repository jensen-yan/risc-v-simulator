target_sources(risc-v-sim-lib PRIVATE
    address_translation.cpp
    checkpoint_importer.cpp
    checkpoint_runner.cpp
    checkpoint_recipe.cpp
    privilege_state.cpp
    simulator.cpp
    sv39_page_walker.cpp
    elf_loader.cpp
    syscall_handler.cpp
    difftest.cpp
    pipeline_tracer.cpp
)
