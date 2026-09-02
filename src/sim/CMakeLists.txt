# Add the header and source files for this directory
set_full_path(THIS_DIR_HEADERS
    job_submit_common.hpp
    job_submit_model.hpp
    multi_platform_runs.hpp
    scheduler_policies.hpp
    schedule_windows.hpp
    scheduler_base.hpp
    scheduler_fcfs.hpp
    scheduler_fcfs_alt.hpp
    scheduler_block_fcfs.hpp
    scheduler_sjf.hpp
    scheduler_ljf.hpp
    block_wait_queue.hpp
    sim.hpp
)

set_full_path(THIS_DIR_SOURCES
    schedule_windows.cpp
    scheduler_base.cpp
    scheduler_fcfs.cpp
    scheduler_fcfs_alt.cpp
    scheduler_block_fcfs.cpp
    scheduler_sjf.cpp
    scheduler_ljf.cpp
    block_wait_queue.cpp
    sim.cpp
)

# Propagate the files up the tree
set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${THIS_DIR_SOURCES}" PARENT_SCOPE)
