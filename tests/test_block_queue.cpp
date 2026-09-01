/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

// Basic compilation test for BlockWaitQueue

#include "sim/block_wait_queue.hpp"
#include <iostream>

int main() {
    dr_evt::BlockWaitQueue queue(128);

    // Insert some test jobs
    queue.insert_job(1, 0.0, 100.0, 10);
    queue.insert_job(2, 5.0, 50.0, 20);
    queue.insert_job(3, 10.0, 150.0, 15);

    std::cout << "Queue size: " << queue.size() << std::endl;
    std::cout << "Active count: " << queue.active_count() << std::endl;

    // Test backfill search
    auto candidate = queue.find_backfill_candidate(30, 0.0, 200.0);
    if (candidate) {
        std::cout << "Found backfill candidate: " << *candidate << std::endl;
    } else {
        std::cout << "No backfill candidate found" << std::endl;
    }

    // Test removal
    queue.mark_removed(2);
    std::cout << "After removal - Active count: " << queue.active_count() << std::endl;

    // Test stats
    auto stats = queue.get_stats();
    std::cout << "Stats - blocks checked: " << stats.blocks_checked << std::endl;
    std::cout << "Stats - jobs scanned: " << stats.jobs_scanned << std::endl;

    return 0;
}
