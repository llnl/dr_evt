#!/usr/bin/env python3
"""
Create scheduling traces without begin_time and end_time columns.
Useful for scheduling simulations where execution times should be predicted
rather than known in advance.
"""

import pandas as pd
import os
import glob
from pathlib import Path

# Configuration
INPUT_DIR = 'traces'
OUTPUT_DIR = 'traces_no_times'
INPUT_PATTERN = '*_scheduling_trace.csv'

# Columns to exclude
EXCLUDE_COLUMNS = ['begin_time', 'end_time']
EPOCH_COLUMNS = ['job_submit_time', 'begin_time', 'end_time']

def process_trace_file(input_file, output_dir):
    """Process a single trace file and create version without execution times."""

    filename = os.path.basename(input_file)
    print(f"\nProcessing {filename}...")

    # Read the trace file
    df = pd.read_csv(input_file)

    # Keep epoch timestamps as unsigned integer seconds in the output, even
    # when an input CSV happened to encode them with a decimal point.
    for column in EPOCH_COLUMNS:
        if column in df.columns:
            values = pd.to_numeric(df[column], errors='raise')
            if (values < 0).any() or (values % 1 != 0).any():
                raise ValueError(f"{column} contains an invalid epoch timestamp")
            df[column] = values.astype('uint64')

    # Convert time_limit to integer (seconds)
    if 'time_limit' in df.columns:
        df['time_limit'] = pd.to_numeric(df['time_limit'], errors='raise').round().astype('uint32')

    # Match conventional process exit codes: COMPLETED is success; every
    # other scheduler terminal state is failure.
    if 'exit_status' in df.columns:
        exit_state = df['exit_status'].astype(str).str.strip().str.upper()
        df['exit_status'] = (~exit_state.isin(['COMPLETED', '0'])).astype('uint8')

    print(f"  Original columns: {list(df.columns)}")
    print(f"  Rows: {len(df):,}")

    # Remove the time columns
    columns_to_keep = [col for col in df.columns if col not in EXCLUDE_COLUMNS]
    df_filtered = df[columns_to_keep]

    print(f"  Removed columns: {EXCLUDE_COLUMNS}")
    print(f"  Remaining columns: {list(df_filtered.columns)}")

    # Generate output filename
    output_file = os.path.join(output_dir, filename)

    # Save to CSV
    df_filtered.to_csv(output_file, index=False)

    # Get file sizes
    input_size = os.path.getsize(input_file) / (1024**2)
    output_size = os.path.getsize(output_file) / (1024**2)
    size_reduction = (1 - output_size/input_size) * 100

    print(f"  Saved to {output_file}")
    print(f"  Size: {input_size:.2f} MB -> {output_size:.2f} MB ({size_reduction:.1f}% reduction)")

    return output_file, len(df_filtered), output_size

def main():
    print("="*70)
    print("CREATE TRACES WITHOUT EXECUTION TIMES")
    print("="*70)

    # Create output directory
    Path(OUTPUT_DIR).mkdir(exist_ok=True)
    print(f"\nInput directory: {INPUT_DIR}")
    print(f"Output directory: {OUTPUT_DIR}")
    print(f"Looking for files: {INPUT_PATTERN}")

    # Find all trace files
    input_pattern = os.path.join(INPUT_DIR, INPUT_PATTERN)
    input_files = sorted(glob.glob(input_pattern))

    if not input_files:
        print(f"\nERROR: No files found matching {input_pattern}")
        return

    print(f"\nFound {len(input_files)} trace files to process")

    # Process each file
    results = []
    total_jobs = 0

    for input_file in input_files:
        try:
            output_file, num_jobs, size_mb = process_trace_file(input_file, OUTPUT_DIR)
            results.append((os.path.basename(output_file), num_jobs, size_mb))
            total_jobs += num_jobs
        except Exception as e:
            print(f"  ERROR processing {input_file}: {e}")
            continue

    # Summary
    print("\n" + "="*70)
    print("SUMMARY")
    print("="*70)

    print(f"\nSuccessfully processed {len(results)} file(s)")
    print(f"Total jobs: {total_jobs:,}")

    print(f"\nOutput files in '{OUTPUT_DIR}/':")
    total_size = 0
    for filename, num_jobs, size_mb in results:
        print(f"  {filename}: {num_jobs:,} jobs, {size_mb:.2f} MB")
        total_size += size_mb

    print(f"\nTotal output size: {total_size:.2f} MB ({total_size/1024:.2f} GB)")

    print("\n" + "="*70)
    print("COLUMNS IN OUTPUT FILES")
    print("="*70)
    print("\nRemaining columns:")
    print("  - job_submit_time (epoch)")
    print("  - time_limit (seconds)")
    print("  - num_nodes")
    print("  - duration (seconds)")
    print("  - avgpcon (watts)")
    print("  - minpcon (watts)")
    print("  - maxpcon (watts)")
    print("  - exit_status (0=completed, 1=failed)")

    print("\nRemoved columns:")
    print("  - begin_time (epoch)")
    print("  - end_time (epoch)")

    print("\nUse case:")
    print("  These traces are for simulations where job execution times")
    print("  and completion times should be predicted/simulated rather")
    print("  than read from historical data.")

if __name__ == '__main__':
    main()
