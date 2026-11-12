# Add the header and source files for this directory
set_full_path(THIS_DIR_HEADERS
    job_submit_common.hpp
    job_submit_model.hpp
    multi_platform_runs.hpp
    capacity_schedule.hpp
    resource/resource_model.hpp
    resource/capacity_model.hpp
    resource/count_timeline.hpp
    scheduler_policies.hpp
    schedule_windows.hpp
    scheduler_base.hpp
    scheduler_fcfs_easy_deque.hpp
    scheduler_fcfs_easy_custom.hpp
    scheduler_fcfs_easy_multimap.hpp
    scheduler_fcfs_hybrid_deque.hpp
    scheduler_fcfs_hybrid_circular.hpp
    scheduler_fcfs_pass_hybrid_circular.hpp
    scheduler_fcfs_1s_evt_group_circular.hpp
    forecast_1s_evt_group_timeline.hpp
    scheduler_periodic_fcfs_hybrid_circular.hpp
    scheduler_fcfs_periodic_easy_circular.hpp
    scheduler_fcfs_periodic_hybrid_circular.hpp
    scheduler_fcfs_easy_block.hpp
    scheduler_fcfs_easy_circular.hpp
    scheduler_sjf_easy.hpp
    scheduler_ljf_easy.hpp
    block_wait_queue.hpp
    sim.hpp
)

set_full_path(THIS_DIR_SOURCES
    capacity_schedule.cpp
    schedule_windows.cpp
    scheduler_base.cpp
    scheduler_fcfs_easy_deque.cpp
    scheduler_fcfs_easy_custom.cpp
    scheduler_fcfs_easy_multimap.cpp
    scheduler_fcfs_hybrid_deque.cpp
    scheduler_fcfs_hybrid_circular.cpp
    scheduler_fcfs_pass_hybrid_circular.cpp
    scheduler_fcfs_periodic_easy_circular.cpp
    scheduler_fcfs_periodic_hybrid_circular.cpp
    scheduler_fcfs_easy_block.cpp
    scheduler_fcfs_easy_block_depth.cpp
    scheduler_fcfs_easy_circular.cpp
    scheduler_sjf_easy.cpp
    scheduler_ljf_easy.cpp
    block_wait_queue.cpp
    sim.cpp
)

# Propagate the files up the tree
set(DR_EVT_HEADERS "${DR_EVT_HEADERS}" "${THIS_DIR_HEADERS}" PARENT_SCOPE)
set(DR_EVT_SOURCES "${DR_EVT_SOURCES}" "${THIS_DIR_SOURCES}" PARENT_SCOPE)
