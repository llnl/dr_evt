#!/usr/bin/env python3
"""
Create scheduling simulation trace from Fugaku parquet files.
Extracts relevant columns and renames them for scheduling simulation.
Filters by submission time (qdt) based on start and end dates.
"""

import pandas as pd
import os
import sys
from pathlib import Path
from datetime import datetime

# Configuration
INPUT_FILES = [
    '21_03.parquet', '21_04.parquet', '21_05.parquet', '21_06.parquet',
    '21_07.parquet', '21_08.parquet', '21_09.parquet', '21_10.parquet',
    '21_11.parquet', '21_12.parquet', '22_01.parquet', '22_02.parquet',
    '22_03.parquet', '22_04.parquet', '22_05.parquet', '22_06.parquet',
    '22_07.parquet', '22_08.parquet', '22_09.parquet', '22_10.parquet',
    '22_11.parquet', '22_12.parquet', '23_01.parquet', '23_02.parquet',
    '23_03.parquet', '23_04.parquet', '23_05.parquet', '23_06.parquet',
    '23_07.parquet', '23_08.parquet', '23_09.parquet', '23_10.parquet',
    '23_11.parquet', '23_12.parquet', '24_01.parquet', '24_02.parquet',
    '24_03.parquet', '24_04.parquet',
]
OUTPUT_DIR = 'traces'

# Date range for filtering (MM-DD-YYYY format)
# Set to None for unlimited (will use first/last record in the file)
START_DATE = None  # e.g., '03-01-2021'
END_DATE = None    # e.g., '03-31-2021'

# Column mapping: original_name -> new_name
COLUMN_MAPPING = {
    'qdt': 'job_submit_time',
    'sdt': 'begin_time',
    'edt': 'end_time',
    'elpl': 'time_limit',
    'nnuma': 'num_nodes',
    'duration': 'duration',
    'avgpcon': 'avgpcon',
    'minpcon': 'minpcon',
    'maxpcon': 'maxpcon',
    'exit state': 'exit_status',
}

def parse_date(date_str, timezone='Asia/Tokyo'):
    """Parse date string in MM-DD-YYYY format to datetime with timezone."""
    if date_str is None:
        return None
    try:
        dt = pd.to_datetime(date_str, format='%m-%d-%Y')
        # Add timezone to match the data
        return dt.tz_localize(timezone)
    except:
        print(f"ERROR: Invalid date format '{date_str}'. Use MM-DD-YYYY format.")
        sys.exit(1)

def process_file(parquet_file, output_dir, start_date, end_date):
    """Process a single parquet file and create scheduling trace."""

    print(f"\nProcessing {parquet_file}...")

    # Read parquet file
    df = pd.read_parquet(parquet_file)
    print(f"  Loaded {len(df):,} jobs")

    # Check if all required columns exist
    required_cols = list(COLUMN_MAPPING.keys())
    missing_cols = [col for col in required_cols if col not in df.columns]
    if missing_cols:
        print(f"  ERROR: Missing columns: {missing_cols}")
        return None

    # Convert qdt to datetime if it's not already
    if not pd.api.types.is_datetime64_any_dtype(df['qdt']):
        df['qdt'] = pd.to_datetime(df['qdt'])

    # Determine date range
    original_min = df['qdt'].min()
    original_max = df['qdt'].max()
    print(f"  Original date range: {original_min} to {original_max}")

    # Apply date filtering
    filter_start = start_date if start_date is not None else original_min
    filter_end = end_date if end_date is not None else original_max

    print(f"  Filtering by submission time (qdt):")
    print(f"    Start: {filter_start} {'(unlimited)' if start_date is None else ''}")
    print(f"    End:   {filter_end} {'(unlimited)' if end_date is None else ''}")

    # Filter by date range
    mask = (df['qdt'] >= filter_start) & (df['qdt'] <= filter_end)
    df_filtered = df[mask].copy()

    if len(df_filtered) == 0:
        print(f"  WARNING: No jobs found in the specified date range!")
        return None

    print(f"  Selected {len(df_filtered):,} jobs ({len(df_filtered)/len(df)*100:.1f}%)")

    # Select required columns + econ for power correction
    cols_to_select = required_cols.copy()
    if 'econ' not in cols_to_select:
        cols_to_select.append('econ')
    df_trace = df_filtered[cols_to_select].copy()

    # Remove rows with missing values in critical columns
    original_len = len(df_trace)
    df_trace = df_trace.dropna()
    if len(df_trace) < original_len:
        print(f"  Removed {original_len - len(df_trace):,} rows with missing values")

    # Rename columns
    df_trace = df_trace.rename(columns=COLUMN_MAPPING)

    # Scheduling traces use a binary process-style exit status: successful
    # completion is 0 and every other terminal state is failure (1).
    exit_state = df_trace['exit_status'].astype(str).str.strip().str.upper()
    df_trace['exit_status'] = (~exit_state.isin(['COMPLETED', '0'])).astype('uint8')

    # Fix corrupted power values by estimating from econ
    # Identify corrupted avgpcon (negative or absurdly large)
    corrupted_mask = (df_trace['avgpcon'] < 0) | (df_trace['avgpcon'] > 1e100)
    if corrupted_mask.sum() > 0:
        print(f"  Warning: {corrupted_mask.sum()} jobs have corrupted avgpcon, estimating from econ")
        # Estimate avgpcon from econ: avgpcon = econ * 3600 / duration
        df_trace.loc[corrupted_mask, 'avgpcon'] = (
            df_trace.loc[corrupted_mask, 'econ'] * 3600 / df_trace.loc[corrupted_mask, 'duration']
        )
        # Also fix minpcon and maxpcon if corrupted
        df_trace.loc[corrupted_mask & ((df_trace['minpcon'] < 0) | (df_trace['minpcon'] > 1e100)), 'minpcon'] = \
            df_trace.loc[corrupted_mask, 'avgpcon'] * 0.95  # Conservative estimate
        df_trace.loc[corrupted_mask & ((df_trace['maxpcon'] < 0) | (df_trace['maxpcon'] > 1e100)), 'maxpcon'] = \
            df_trace.loc[corrupted_mask, 'avgpcon'] * 1.05  # Conservative estimate

    # Drop econ column as it's no longer needed
    if 'econ' in df_trace.columns:
        df_trace = df_trace.drop(columns=['econ'])

    # Convert datetime columns to epoch (Unix timestamp in seconds)
    datetime_columns = ['job_submit_time', 'begin_time', 'end_time']
    for col in datetime_columns:
        if col in df_trace.columns:
            epoch_seconds = pd.to_datetime(df_trace[col]).astype('int64') // 10**9
            if (epoch_seconds < 0).any():
                raise ValueError(f"{col} contains a timestamp before the Unix epoch")
            df_trace[col] = epoch_seconds.astype('uint64')

    # Convert time_limit to integer (seconds)
    if 'time_limit' in df_trace.columns:
        df_trace['time_limit'] = df_trace['time_limit'].round().astype('uint32')

    # Sort by submission time
    df_trace = df_trace.sort_values('job_submit_time').reset_index(drop=True)

    # Generate output filename
    base_name = Path(parquet_file).stem

    # Add date range to filename if specified
    if start_date or end_date:
        start_str = start_date.strftime('%Y%m%d') if start_date else 'start'
        end_str = end_date.strftime('%Y%m%d') if end_date else 'end'
        output_file = os.path.join(output_dir, f'{base_name}_scheduling_trace_{start_str}_to_{end_str}.csv')
    else:
        output_file = os.path.join(output_dir, f'{base_name}_scheduling_trace.csv')

    # Save to CSV
    df_trace.to_csv(output_file, index=False)
    print(f"  Saved {len(df_trace):,} jobs to {output_file}")

    # Display statistics
    print(f"\n  Statistics:")
    # Convert epoch back to datetime for display
    submit_min = pd.to_datetime(df_trace['job_submit_time'], unit='s', utc=True).min()
    submit_max = pd.to_datetime(df_trace['job_submit_time'], unit='s', utc=True).max()
    print(f"    Date range: {submit_min} to {submit_max}")
    print(f"    Epoch range: {df_trace['job_submit_time'].min()} to {df_trace['job_submit_time'].max()}")
    print(f"    Nodes: {df_trace['num_nodes'].min():.0f} - {df_trace['num_nodes'].max():.0f} (median: {df_trace['num_nodes'].median():.0f})")
    print(f"    Duration: {df_trace['duration'].min():.0f}s - {df_trace['duration'].max():.0f}s (median: {df_trace['duration'].median():.0f}s)")
    print(f"    Time limit: {df_trace['time_limit'].min():.0f}s - {df_trace['time_limit'].max():.0f}s (median: {df_trace['time_limit'].median():.0f}s)")

    # Filter out extreme outliers for power statistics display
    avgpcon_valid = df_trace['avgpcon'][(df_trace['avgpcon'] > 0) & (df_trace['avgpcon'] < 1e6)]
    if len(avgpcon_valid) > 0:
        print(f"    Avg power: {avgpcon_valid.min():.2f}W - {avgpcon_valid.max():.2f}W (median: {avgpcon_valid.median():.2f}W)")
    else:
        print(f"    Avg power: {df_trace['avgpcon'].min():.2f}W - {df_trace['avgpcon'].max():.2f}W (median: {df_trace['avgpcon'].median():.2f}W)")

    # Show sample
    print(f"\n  Sample (first 3 rows):")
    print(df_trace.head(3).to_string(index=False))

    # Check for data quality issues
    print(f"\n  Data quality checks:")
    issues = 0

    # Epoch timestamps are already integers, compare directly
    submit_time = df_trace['job_submit_time']
    begin_time = df_trace['begin_time']
    end_time = df_trace['end_time']

    # Check if begin_time >= job_submit_time
    invalid_begin = begin_time < submit_time
    if invalid_begin.sum() > 0:
        print(f"    WARNING: {invalid_begin.sum():,} jobs have begin_time < job_submit_time")
        issues += 1

    # Check if end_time >= begin_time
    invalid_end = end_time < begin_time
    if invalid_end.sum() > 0:
        print(f"    WARNING: {invalid_end.sum():,} jobs have end_time < begin_time")
        issues += 1

    # Check if duration ≈ end_time - begin_time (already in seconds)
    calc_duration = end_time - begin_time
    duration_mismatch = (df_trace['duration'] - calc_duration).abs() > 2
    if duration_mismatch.sum() > 0:
        print(f"    WARNING: {duration_mismatch.sum():,} jobs have duration mismatch > 2s")
        issues += 1

    # Check if time_limit >= duration
    exceeded_limit = df_trace['duration'] > df_trace['time_limit']
    if exceeded_limit.sum() > 0:
        print(f"    NOTE: {exceeded_limit.sum():,} jobs exceeded time_limit (might be killed jobs)")

    if issues == 0:
        print(f"    ✓ No critical data quality issues found")

    return output_file

def main():
    # Create output directory if it doesn't exist
    Path(OUTPUT_DIR).mkdir(exist_ok=True)

    print("="*70)
    print("CREATING SCHEDULING SIMULATION TRACE")
    print("="*70)

    # Parse dates
    start_date = parse_date(START_DATE)
    end_date = parse_date(END_DATE)

    print(f"\nInput files: {INPUT_FILES}")
    print(f"Output directory: {OUTPUT_DIR}")
    print(f"\nDate filter (by submission time):")
    print(f"  Start: {START_DATE if START_DATE else 'unlimited (from first record)'}")
    print(f"  End:   {END_DATE if END_DATE else 'unlimited (to last record)'}")

    print(f"\nColumn mapping:")
    for orig, new in COLUMN_MAPPING.items():
        print(f"  {orig:12s} -> {new}")

    # Process each file
    output_files = []
    for parquet_file in INPUT_FILES:
        if not os.path.exists(parquet_file):
            print(f"\nERROR: File not found: {parquet_file}")
            continue

        output_file = process_file(parquet_file, OUTPUT_DIR, start_date, end_date)
        if output_file:
            output_files.append(output_file)

    # Summary
    print("\n" + "="*70)
    print("SUMMARY")
    print("="*70)
    print(f"\nProcessed {len(output_files)} file(s)")
    print(f"Output files:")
    for f in output_files:
        size_mb = os.path.getsize(f) / (1024**2)
        print(f"  - {f} ({size_mb:.2f} MB)")

    print(f"\n" + "="*70)
    print("USAGE NOTES")
    print("="*70)
    print(f"\nTo change the date range, edit START_DATE and END_DATE at the top of this script:")
    print(f"  START_DATE = '03-01-2021'  # MM-DD-YYYY format")
    print(f"  END_DATE = '03-15-2021'    # MM-DD-YYYY format")
    print(f"  START_DATE = None          # unlimited (use first record)")
    print(f"  END_DATE = None            # unlimited (use last record)")
    print(f"\nTo process multiple months, edit INPUT_FILES:")
    print(f"  INPUT_FILES = ['21_03.parquet', '21_04.parquet', '21_05.parquet']")

if __name__ == '__main__':
    main()
