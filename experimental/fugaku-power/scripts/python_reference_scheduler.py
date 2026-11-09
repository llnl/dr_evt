#!/usr/bin/env python3
################################################################################
#         Copyright 2023 Lawrence Livermore National Security, LLC             #
#         See the top-level LICENSE file for details.                          #
#                                                                              #
#         SPDX-License-Identifier: MIT                                         #
################################################################################

"""
Python Reference EASY Backfilling Scheduler

Pure implementation of EASY backfilling algorithm for verification.
No dependencies, no fancy features - just correct EASY logic.

IMPORTANT: This script writes output files to /tmp by default to avoid
polluting the working directory. Use --outdir to specify a different location.

Algorithm:
1. Jobs arrive and enter wait queue (FCFS order)
2. First job in queue gets a RESERVATION (guaranteed start time)
3. Other jobs can BACKFILL if:
   - They fit in current free nodes
   - Their resource-time use preserves the first job's reservation
4. When jobs complete, run scheduler again

This is the REFERENCE implementation for verifying DR_EVT.
"""

import csv
import sys
import os
from dataclasses import dataclass
from typing import List, Optional
import importlib.util
from pathlib import Path

@dataclass
class Job:
    """Job record"""
    idx: int
    submit_time: float
    nodes: int
    duration: float  # time_limit (what scheduler sees for planning)
    actual_run_time: Optional[float] = None  # actual run time (<= duration), defaults to duration
    avgpcon: Optional[float] = None
    minpcon: Optional[float] = None
    maxpcon: Optional[float] = None

    # Simulation results (filled in by scheduler)
    start_time: Optional[float] = None
    end_time: Optional[float] = None

    def __post_init__(self):
        # Default actual_run_time to duration if not specified
        if self.actual_run_time is None:
            self.actual_run_time = self.duration

# Keep scheduling semantics aligned with the shared Python EASY oracle while
# retaining Fugaku-specific job parsing and power output in this file.
_reference_path = Path(__file__).resolve().parents[3] / "scripts" / "python_reference_scheduler.py"
_reference_spec = importlib.util.spec_from_file_location("_dr_evt_easy_reference", _reference_path)
_reference = importlib.util.module_from_spec(_reference_spec)
_reference_spec.loader.exec_module(_reference)
EasyBackfillingScheduler = _reference.EasyBackfillingScheduler

def load_trace(filename: str) -> List[Job]:
    """Load job trace from CSV"""
    jobs = []
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for idx, row in enumerate(reader):
            # Handle both simple format and full format
            submit_time = float(row.get('job_submit_time', row.get('submit_time', 0)))
            nodes = int(row.get('num_nodes', row.get('nodes', 1)))
            duration = float(row.get('time_limit', row.get('duration', 100)))

            # The Fugaku input keeps the scheduling estimate in time_limit and
            # the observed run time in duration.
            actual_run_time = None
            if 'actual_run_time' in row and row['actual_run_time']:
                actual_run_time = float(row['actual_run_time'])
            elif 'duration' in row and row['duration']:
                actual_run_time = float(row['duration'])

            avgpcon = float(row['avgpcon']) if row.get('avgpcon') else None
            minpcon = float(row['minpcon']) if row.get('minpcon') else None
            maxpcon = float(row['maxpcon']) if row.get('maxpcon') else None

            jobs.append(Job(
                idx=idx,
                submit_time=submit_time,
                nodes=nodes,
                duration=duration,
                actual_run_time=actual_run_time,
                avgpcon=avgpcon,
                minpcon=minpcon,
                maxpcon=maxpcon
            ))
    return jobs

def write_scheduler_output(jobs: List[Job], filename: str):
    """Write scheduler results in format comparable to DR_EVT"""
    with open(filename, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['job_idx', 'submit_time', 'start_time', 'end_time', 'nodes', 'duration'])
        for job in jobs:
            writer.writerow([
                job.idx,
                job.submit_time,
                job.start_time if job.start_time is not None else '',
                job.end_time if job.end_time is not None else '',
                job.nodes,
                job.duration
            ])

def write_resource_trace(jobs: List[Job], filename: str, total_nodes: int):
    """Generate a node and aggregate power-usage resource trace."""
    has_pcon = any(job.avgpcon is not None for job in jobs)

    # Collect all events (start and end)
    events = []
    for job in jobs:
        pcon = (
            job.avgpcon or 0.0,
            job.minpcon or 0.0,
            job.maxpcon or 0.0,
        )
        if job.start_time is not None:
            events.append((job.start_time, 1, +job.nodes, *pcon))
        if job.end_time is not None:
            events.append((job.end_time, 0, -job.nodes,
                           -pcon[0], -pcon[1], -pcon[2]))

    # Sort by time and process completions before starts at the same instant.
    events.sort()

    # Track resource state changes
    allocated = 0
    avgpcon = 0.0
    minpcon = 0.0
    maxpcon = 0.0
    resource_history = []

    for time, event_order, node_delta, avg_delta, min_delta, max_delta in events:
        allocated += node_delta
        avgpcon += avg_delta
        minpcon += min_delta
        maxpcon += max_delta
        free = total_nodes - allocated
        resource_history.append(
            (time, free, allocated, avgpcon, minpcon, maxpcon))

    # Write resource trace
    with open(filename, 'w', newline='') as f:
        writer = csv.writer(f)
        header = ['time', 'free_nodes', 'allocated_nodes']
        if has_pcon:
            header.extend(['avgpcon', 'minpcon', 'maxpcon'])
        writer.writerow(header)
        for time, free, allocated, avgpcon, minpcon, maxpcon in resource_history:
            row = [time, free, allocated]
            if has_pcon:
                row.extend([avgpcon, minpcon, maxpcon])
            writer.writerow(row)

def main():
    if len(sys.argv) < 2:
        print("Usage: python_reference_scheduler.py <trace.csv> [--verbose] [--nodes N] [--outdir DIR]")
        print()
        print("Python reference EASY backfilling scheduler for verification")
        print()
        print("Options:")
        print("  --verbose    Show detailed simulation progress")
        print("  --nodes N    Total nodes in system (default: 1000)")
        print("  --outdir DIR Output directory for generated files (default: /tmp)")
        print()
        print("Generates reference output for EASY backfilling verification")
        print("Output files are written to /tmp by default to avoid directory pollution")
        sys.exit(1)

    trace_file = sys.argv[1]
    verbose = '--verbose' in sys.argv

    # Parse total_nodes
    total_nodes = 1000  # default
    if '--nodes' in sys.argv:
        idx = sys.argv.index('--nodes')
        total_nodes = int(sys.argv[idx + 1])

    # Parse output directory (default to /tmp to avoid pollution)
    outdir = '/tmp'
    if '--outdir' in sys.argv:
        idx = sys.argv.index('--outdir')
        outdir = sys.argv[idx + 1]
    os.makedirs(outdir, exist_ok=True)

    print(f"=== Python Reference EASY Backfilling Scheduler ===", file=sys.stderr)
    print(f"Trace: {trace_file}", file=sys.stderr)
    print(f"Total nodes: {total_nodes}", file=sys.stderr)
    print(f"Output directory: {outdir}", file=sys.stderr)
    print(f"Verbose: {verbose}", file=sys.stderr)
    print(file=sys.stderr)

    # Load trace
    jobs = load_trace(trace_file)
    print(f"Loaded {len(jobs)} jobs", file=sys.stderr)

    # Run scheduler
    scheduler = EasyBackfillingScheduler(total_nodes, verbose=verbose)
    jobs = scheduler.simulate(jobs)

    # Write output files to specified directory
    basename = os.path.basename(trace_file).replace('.csv', '')
    output_file = os.path.join(outdir, f'{basename}_reference.csv')
    write_scheduler_output(jobs, output_file)

    # Write resource trace
    resource_file = os.path.join(outdir, f'{basename}_reference_resources.csv')
    write_resource_trace(jobs, resource_file, total_nodes)

    print(f"\nReference output written to: {output_file}", file=sys.stderr)
    print(f"Resource trace written to: {resource_file}", file=sys.stderr)
    print(f"Final simulation time: {scheduler.current_time}", file=sys.stderr)

    # Verify all jobs completed
    completed = sum(1 for job in jobs if job.start_time is not None)
    print(f"Jobs completed: {completed}/{len(jobs)}", file=sys.stderr)

    if completed != len(jobs):
        print("\n⚠ WARNING: Not all jobs completed!", file=sys.stderr)
        for job in jobs:
            if job.start_time is None:
                print(f"  Job {job.idx}: never started", file=sys.stderr)

if __name__ == '__main__':
    main()
