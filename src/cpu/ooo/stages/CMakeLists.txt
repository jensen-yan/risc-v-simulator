target_sources(risc-v-sim-lib PRIVATE
    fetch_stage.cpp
    decode_stage.cpp
    dispatch_stage.cpp
    execute_stage.cpp
    writeback_stage.cpp
    commit_stage.cpp
)
