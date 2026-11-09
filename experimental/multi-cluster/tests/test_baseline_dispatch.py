#!/usr/bin/env python3
"""Unit tests for the platform-ranking baseline dispatcher."""

import pathlib
import sys
import tempfile
import unittest
from types import SimpleNamespace

# Production scripts live one directory above this test suite.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from grpc_baseline_dispatch import (ApplicationType, PlatformType,
                                    choose_system, estimate_wait, read_arrivals)


def system(index, platform_type, rank, capacity=100):
    return {
        "index": index,
        "system_id": f"system-{index}",
        "capacity": capacity,
        "platform_type": platform_type,
        "performance_rank": rank,
    }


def job(application_type, nodes=8, runtime=10):
    return {
        "job_id": "job",
        "application_type": application_type,
        "num_nodes": nodes,
        "limit_time": runtime,
    }


def window(wait, nodes=8):
    if wait == 0:
        return SimpleNamespace(current_time=0, available_nodes=nodes,
                               shadow_time=-1, releases=[])
    return SimpleNamespace(
        current_time=0, available_nodes=0, shadow_time=-1,
        releases=[SimpleNamespace(time=wait, nodes_released=nodes)])


class BaselineDispatchTests(unittest.TestCase):
    def test_backfill_can_end_exactly_at_shadow_time(self):
        snapshot = SimpleNamespace(current_time=10, available_nodes=2,
                                   shadow_time=30, releases=[])
        self.assertEqual(estimate_wait(snapshot, 2, 20, 40), 0)
        self.assertEqual(estimate_wait(snapshot, 2, 20.001, 40), 60)

    def test_gpu_and_cpu_only_require_matching_platform(self):
        systems = [
            system(0, PlatformType.CPU_ONLY, 1),
            system(1, PlatformType.GPU_ENABLED, 1),
        ]
        windows = [window(0), window(0)]
        cpu = choose_system(job(ApplicationType.CPU_ONLY), systems,
                            windows, [0, 0], 0)
        gpu = choose_system(job(ApplicationType.GPU_ONLY), systems,
                            windows, [0, 0], 0)
        self.assertEqual(cpu["platform_type"], PlatformType.CPU_ONLY)
        self.assertEqual(gpu["platform_type"], PlatformType.GPU_ENABLED)

    def test_rank_wins_when_waits_are_similar(self):
        systems = [
            system(0, PlatformType.CPU_ONLY, 2),
            system(1, PlatformType.CPU_ONLY, 1),
        ]
        choice = choose_system(job(ApplicationType.CPU_ONLY), systems,
                               [window(2), window(5)], [0, 0], 5)
        self.assertEqual(choice["system_id"], "system-1")

    def test_shorter_wait_wins_outside_tolerance(self):
        systems = [
            system(0, PlatformType.CPU_ONLY, 2),
            system(1, PlatformType.CPU_ONLY, 1),
        ]
        choice = choose_system(job(ApplicationType.CPU_ONLY), systems,
                               [window(2), window(20)], [0, 0], 5)
        self.assertEqual(choice["system_id"], "system-0")

    def test_zero_tolerance_considers_wait_time_only(self):
        systems = [
            system(0, PlatformType.CPU_ONLY, 2),
            system(1, PlatformType.CPU_ONLY, 1),
        ]
        choice = choose_system(job(ApplicationType.CPU_ONLY), systems,
                               [window(2), window(3)], [0, 0], 0)
        self.assertEqual(choice["system_id"], "system-0")

    def test_gpu_portable_prefers_gpu_within_tolerance(self):
        systems = [
            system(0, PlatformType.CPU_ONLY, 1),
            system(1, PlatformType.GPU_ENABLED, 2),
        ]
        choice = choose_system(job(ApplicationType.GPU_PORTABLE), systems,
                               [window(1), window(4)], [0, 0], 5)
        self.assertEqual(choice["platform_type"], PlatformType.GPU_ENABLED)

    def test_gpu_portable_falls_back_for_significantly_shorter_cpu_wait(self):
        systems = [
            system(0, PlatformType.CPU_ONLY, 1),
            system(1, PlatformType.GPU_ENABLED, 1),
        ]
        choice = choose_system(job(ApplicationType.GPU_PORTABLE), systems,
                               [window(1), window(20)], [0, 0], 5)
        self.assertEqual(choice["platform_type"], PlatformType.CPU_ONLY)

    def test_arrival_type_is_an_enum(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "jobs.csv"
            path.write_text(
                "job_submit_time,num_nodes,time_limit,application_type\n"
                "0,4,20,gpu-only\n", encoding="utf-8")
            self.assertEqual(read_arrivals(path)[0]["application_type"],
                             ApplicationType.GPU_ONLY)

    def test_numeric_application_type_codes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "jobs.csv"
            path.write_text(
                "job_submit_time,num_nodes,time_limit,application_type\n"
                "0,4,20,0\n"
                "1,4,20,1\n"
                "2,4,20,2\n", encoding="utf-8")
            self.assertEqual(
                [job["application_type"] for job in read_arrivals(path)],
                [ApplicationType.CPU_ONLY, ApplicationType.GPU_ONLY,
                 ApplicationType.GPU_PORTABLE])


if __name__ == "__main__":
    unittest.main()
