target_sources(risc-v-sim-lib PRIVATE
    fetch_stage.cpp
    decode_stage.cpp
    issue_stage.cpp
    execute_stage.cpp
    writeback_stage.cpp
    commit_stage.cpp
)
