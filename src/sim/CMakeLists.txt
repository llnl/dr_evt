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
    scheduler_sjf.hpp
    scheduler_ljf.hpp
    sim.hpp
)

set_full_path(THIS_DIR_SOURCES
    schedule_windows.cpp
    scheduler_base.cpp
    scheduler_fcfs.cpp
    scheduler_fcfs_alt.cpp
    scheduler_sjf.cpp
    scheduler_ljf.cpp
    sim.cpp
)

# Propagate the files up the tree
set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${THIS_DIR_SOURCES}" PARENT_SCOPE)

# Optional: Block-based wait queue optimization
# Note: Must come after propagating main files since it appends to parent scope
option(USE_BLOCK_QUEUE "Use block-based wait queue for scheduler" OFF)
if(USE_BLOCK_QUEUE)
    message(STATUS "Block-based wait queue enabled")
    add_definitions(-DUSE_BLOCK_QUEUE)
    set_full_path(BLOCK_QUEUE_HEADERS block_wait_queue.hpp)
    set_full_path(BLOCK_QUEUE_SOURCES block_wait_queue.cpp)
    set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${BLOCK_QUEUE_HEADERS}" PARENT_SCOPE)
    set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${BLOCK_QUEUE_SOURCES}" PARENT_SCOPE)
endif()
