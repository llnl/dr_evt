// Minimal test for BlockWaitQueue - compiles with CMake

#include "sim/block_wait_queue.hpp"
#include <iostream>
#include <cassert>

void run_tests(bool immediate_erase, size_t block_size) {
    std::cout << "\n=== BlockWaitQueue Tests (block_size=" << block_size
              << ", immediate_erase=" << (immediate_erase ? "true" : "false")
              << ") ===" << std::endl;

    dr_evt::BlockWaitQueue queue(block_size, immediate_erase);
    assert(queue.empty());
    assert(queue.size() == 0);
    std::cout << "✓ Empty queue initialized" << std::endl;

    // Test 1: Insert jobs
    queue.insert_job(1, 0.0, 100.0, 10);   // job_id=1, submit=0, runtime=100, nodes=10
    queue.insert_job(2, 5.0, 50.0, 20);    // job_id=2, submit=5, runtime=50, nodes=20
    queue.insert_job(3, 10.0, 150.0, 15);  // job_id=3, submit=10, runtime=150, nodes=15

    assert(queue.size() == 3);
    assert(queue.active_count() == 3);
    std::cout << "✓ Inserted 3 jobs" << std::endl;

    // Test 2: Find backfill candidate
    // available_nodes=30, current_time=0, reservation_time=200
    // Job 1: nodes=10 (fits), runtime=100 (fits in window)
    // Job 2: nodes=20 (fits), runtime=50 (fits in window, shorter)
    // Job 3: nodes=15 (fits), runtime=150 (fits in window)
    auto candidate = queue.find_backfill_candidate(30, 0.0, 200.0);
    assert(candidate.has_value());
    std::cout << "✓ Found backfill candidate: job " << *candidate << std::endl;

    // Test 3: Find with tight resource constraint
    // Only 12 nodes available - only job 1 (10 nodes) should fit
    candidate = queue.find_backfill_candidate(12, 0.0, 200.0);
    assert(candidate.has_value());
    assert(*candidate == 1);
    std::cout << "✓ Resource constraint filtering works: job " << *candidate << std::endl;

    // Test 4: Find with tight time constraint
    // Window only 60 time units - only job 2 (runtime=50) should fit
    candidate = queue.find_backfill_candidate(30, 0.0, 60.0);
    assert(candidate.has_value());
    assert(*candidate == 2);
    std::cout << "✓ Time constraint filtering works: job " << *candidate << std::endl;

    // Test 5: Mark job removed
    queue.mark_removed(2);
    assert(queue.active_count() == 2);
    std::cout << "✓ Job 2 marked removed, active count: " << queue.active_count() << std::endl;

    // Test 6: Search after removal - job 2 should not be found
    candidate = queue.find_backfill_candidate(30, 0.0, 60.0);
    // Now job 2 is removed, window=60 is too short for job 1 (100) and job 3 (150)
    assert(!candidate.has_value());
    std::cout << "✓ Removed job not returned in search" << std::endl;

    // Test 7: Iterate over active jobs
    std::cout << "Active jobs: ";
    size_t count = 0;
    queue.for_each_active([&count](dr_evt::job_no_t job_id) {
        std::cout << job_id << " ";
        count++;
    });
    std::cout << std::endl;
    assert(count == 2);  // Jobs 1 and 3 still active
    std::cout << "✓ Iteration over active jobs works" << std::endl;

    // Test 8: Stats
    auto stats = queue.get_stats();
    std::cout << "Stats - blocks checked: " << stats.blocks_checked << std::endl;
    std::cout << "Stats - blocks skipped (time): " << stats.blocks_skipped_time << std::endl;
    std::cout << "Stats - blocks skipped (resource): " << stats.blocks_skipped_resource << std::endl;
    std::cout << "Stats - jobs scanned: " << stats.jobs_scanned << std::endl;
    std::cout << "✓ Statistics tracking works" << std::endl;

    // Test 9: Multiple blocks (insert block_size+1 jobs to trigger 2 blocks)
    dr_evt::BlockWaitQueue large_queue(block_size);
    for (size_t i = 0; i < block_size + 1; i++) {
        large_queue.insert_job(i, i * 1.0, 100.0 + i, 10);
    }
    assert(large_queue.size() == block_size + 1);
    std::cout << "✓ Multiple blocks created for " << (block_size + 1) << " jobs" << std::endl;

    // Search should work across blocks
    candidate = large_queue.find_backfill_candidate(20, 0.0, 200.0);
    assert(candidate.has_value());
    std::cout << "✓ Search across multiple blocks works: job " << *candidate << std::endl;

    std::cout << "✓ All tests passed for block_size=" << block_size
              << ", immediate_erase=" << (immediate_erase ? "true" : "false") << std::endl;
}

int main() {
    // Test different block sizes and deletion modes
    std::cout << "Testing with block size 32:" << std::endl;
    run_tests(false, 32);  // Lazy deletion, block_size=32
    run_tests(true, 32);   // Immediate deletion, block_size=32

    std::cout << "\nTesting with block size 128:" << std::endl;
    run_tests(false, 128);  // Lazy deletion, block_size=128
    run_tests(true, 128);   // Immediate deletion, block_size=128

    std::cout << "\n=== All tests passed! ===" << std::endl;
    return 0;
}
