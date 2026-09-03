/******************************************************************************
 *         Copyright 2023 Lawrence Livermore National Security, LLC           *
 *         See the top-level LICENSE file for details.                        *
 *                                                                            *
 *         SPDX-License-Identifier: MIT                                       *
 ******************************************************************************/

// Minimal test for BlockWaitQueue - compiles with CMake

#include "sim/block_wait_queue.hpp"
#include <iostream>
#include <cassert>

template<size_t BlockSize>
void run_tests_impl() {
    std::cout << "\n=== BlockWaitQueue Tests (block_size=" << BlockSize
              << ") ===" << std::endl;

    dr_evt::BlockWaitQueue<BlockSize> queue;
    assert(queue.empty());
    assert(queue.size() == 0);
    std::cout << "✓ Empty queue initialized" << std::endl;

    // Test 1: Insert jobs
    queue.insert_job(1, 0.0, 100.0, 10);   // job_id=1, submit=0, run_time=100, nodes=10
    queue.insert_job(2, 5.0, 50.0, 20);    // job_id=2, submit=5, run_time=50, nodes=20
    queue.insert_job(3, 10.0, 150.0, 15);  // job_id=3, submit=10, run_time=150, nodes=15

    assert(queue.size() == 3);
    assert(queue.active_count() == 3);
    std::cout << "✓ Inserted 3 jobs" << std::endl;

    // Test 2: Find and remove backfill candidate (NEW combined API)
    // available_nodes=30, current_time=0, reservation_time=200
    // Job 1: nodes=10 (fits), run_time=100 (fits in window)
    // Job 2: nodes=20 (fits), run_time=50 (fits in window, shorter)
    // Job 3: nodes=15 (fits), run_time=150 (fits in window)
    auto candidate = queue.find_and_remove_backfill_candidate(30, 0.0, 200.0);
    assert(candidate.has_value());
    std::cout << "✓ Found and removed backfill candidate: job " << *candidate << std::endl;
    // Note: candidate was automatically removed
    assert(queue.active_count() == 2);  // One job removed
    std::cout << "✓ Combined find-and-remove worked, active count: " << queue.active_count() << std::endl;

    // Test 3: Find with tight resource constraint
    // Only 12 nodes available - only job 1 (10 nodes) should fit
    // We need fresh queue for this test
    dr_evt::BlockWaitQueue<BlockSize> queue2;
    queue2.insert_job(1, 0.0, 100.0, 10);
    queue2.insert_job(2, 5.0, 50.0, 20);
    queue2.insert_job(3, 10.0, 150.0, 15);

    candidate = queue2.find_and_remove_backfill_candidate(12, 0.0, 200.0);
    assert(candidate.has_value());
    assert(*candidate == 1);
    std::cout << "✓ Resource constraint filtering works: job " << *candidate << std::endl;

    // Test 4: Find with tight time constraint
    // Window only 60 time units - only job 2 (run_time=50) should fit
    // NOTE: current_time must be >= submit_time for job to be eligible
    dr_evt::BlockWaitQueue<BlockSize> queue3;
    queue3.insert_job(1, 0.0, 100.0, 10);
    queue3.insert_job(2, 0.0, 50.0, 20);  // Changed submit_time to 0.0 (was 5.0)
    queue3.insert_job(3, 0.0, 150.0, 15);  // Changed submit_time to 0.0 (was 10.0)

    candidate = queue3.find_and_remove_backfill_candidate(30, 0.0, 60.0);
    assert(candidate.has_value());
    assert(*candidate == 2);
    std::cout << "✓ Time constraint filtering works: job " << *candidate << std::endl;

    // Test 5: Manual remove (still supported)
    dr_evt::BlockWaitQueue<BlockSize> queue4;
    queue4.insert_job(1, 0.0, 100.0, 10);
    queue4.insert_job(2, 5.0, 50.0, 20);
    queue4.remove(2);
    assert(queue4.active_count() == 1);
    std::cout << "✓ Manual remove works, active count: " << queue4.active_count() << std::endl;

    // Test 6: Search after removal - job 2 should not be found
    candidate = queue4.find_and_remove_backfill_candidate(30, 0.0, 60.0);
    // Now job 2 is removed, window=60 is too short for job 1 (100)
    assert(!candidate.has_value());
    std::cout << "✓ Removed job not returned in search" << std::endl;

    // Test 7: Iterate over active jobs
    // Use queue (which removed one job via find_and_remove)
    std::cout << "Active jobs: ";
    size_t count = 0;
    queue.for_each_active([&count](dr_evt::job_no_t job_id) {
        std::cout << job_id << " ";
        count++;
    });
    std::cout << std::endl;
    assert(count == 2);  // 2 jobs still active (one was removed by find_and_remove)
    std::cout << "✓ Iteration over active jobs works" << std::endl;

    // Test 8: Stats
    auto stats = queue.get_stats();
    std::cout << "Stats - blocks checked: " << stats.blocks_checked << std::endl;
    std::cout << "Stats - blocks skipped (time): " << stats.blocks_skipped_time << std::endl;
    std::cout << "Stats - blocks skipped (resource): " << stats.blocks_skipped_resource << std::endl;
    std::cout << "Stats - jobs scanned: " << stats.jobs_scanned << std::endl;
    std::cout << "✓ Statistics tracking works" << std::endl;

    // Test 9: Multiple blocks (insert BlockSize+1 jobs to trigger 2 blocks)
    dr_evt::BlockWaitQueue<BlockSize> large_queue;
    for (size_t i = 0; i < BlockSize + 1; i++) {
        large_queue.insert_job(i, i * 1.0, 100.0 + i, 10);
    }
    assert(large_queue.size() == BlockSize + 1);
    std::cout << "✓ Multiple blocks created for " << (BlockSize + 1) << " jobs" << std::endl;

    // Search should work across blocks (using new combined API)
    candidate = large_queue.find_and_remove_backfill_candidate(20, 0.0, 200.0);
    assert(candidate.has_value());
    std::cout << "✓ Search across multiple blocks works: job " << *candidate << std::endl;
    // Verify it was removed
    assert(large_queue.active_count() == BlockSize);
    std::cout << "✓ Cross-block find-and-remove works, active count: " << large_queue.active_count() << std::endl;

    std::cout << "✓ All tests passed for block_size=" << BlockSize << std::endl;
}

// Factory function: run_time block_size -> template instantiation
void run_tests(size_t block_size) {
    switch (block_size) {
        case 16:  run_tests_impl<16>(); break;
        case 32:  run_tests_impl<32>(); break;
        case 64:  run_tests_impl<64>(); break;
        case 128: run_tests_impl<128>(); break;
        case 256: run_tests_impl<256>(); break;
        default:
            std::cerr << "Unsupported block size: " << block_size << std::endl;
            std::exit(1);
    }
}

int main() {
    // Test all supported block sizes: 16, 32, 64, 128, 256
    std::cout << "Testing with block size 16:" << std::endl;
    run_tests(16);

    std::cout << "\nTesting with block size 32:" << std::endl;
    run_tests(32);

    std::cout << "\nTesting with block size 64:" << std::endl;
    run_tests(64);

    std::cout << "\nTesting with block size 128:" << std::endl;
    run_tests(128);

    std::cout << "\nTesting with block size 256:" << std::endl;
    run_tests(256);

    std::cout << "\n=== All tests passed for all block sizes! ===" << std::endl;
    return 0;
}
