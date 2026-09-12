#!/usr/bin/env python3
"""
Select samples from trace files within a specified time period.
Works with both trace formats (with or without begin_time/end_time).
Filters by job_submit_time.
"""

import pandas as pd
import sys
import os
from datetime import datetime

# Configuration - modify these at the top of the script
INPUT_FILE = 'traces/21_03_scheduling_trace.csv'  # Input trace file
OUTPUT_FILE = 'selected_trace.csv'  # Output file
START_DATE = None  # Format: 'MM-DD-YYYY' or None for beginning
END_DATE = None    # Format: 'MM-DD-YYYY' or None for end

def parse_date(date_str, timezone='Asia/Tokyo'):
    """Parse date string in MM-DD-YYYY format to epoch timestamp."""
    if date_str is None or date_str == '':
        return None

    try:
        # Parse date
        dt = datetime.strptime(date_str, '%m-%d-%Y')
        # Convert to epoch (assuming start of day in Tokyo timezone)
        # Note: This creates a naive datetime, so epoch is relative to system timezone
        # For more accurate conversion, we approximate by adding timezone offset
        epoch = int(dt.timestamp())
        # Adjust for JST (UTC+9) - subtract 9 hours to get JST midnight
        epoch -= 9 * 3600
        return epoch
    except ValueError as e:
        print(f"ERROR: Invalid date format '{date_str}'. Use MM-DD-YYYY format.")
        print(f"Example: '03-01-2021'")
        sys.exit(1)

def select_period(input_file, output_file, start_date_str, end_date_str):
    """Select samples from trace file within specified period."""

    print("="*70)
    print("TRACE PERIOD SELECTION")
    print("="*70)

    # Check if input file exists
    if not os.path.exists(input_file):
        print(f"\nERROR: Input file not found: {input_file}")
        sys.exit(1)

    print(f"\nInput file: {input_file}")
    print(f"Output file: {output_file}")

    # Parse dates
    start_epoch = parse_date(start_date_str)
    end_epoch = parse_date(end_date_str)

    print(f"\nDate filter (by job_submit_time):")
    if start_date_str:
        start_dt = datetime.fromtimestamp(start_epoch)
        print(f"  Start: {start_date_str} (epoch: {start_epoch}, {start_dt})")
    else:
        print(f"  Start: [beginning of file]")

    if end_date_str:
        end_dt = datetime.fromtimestamp(end_epoch)
        print(f"  End:   {end_date_str} (epoch: {end_epoch}, {end_dt})")
    else:
        print(f"  End:   [end of file]")

    # Load trace file
    print(f"\nLoading trace file...")
    df = pd.read_csv(input_file)

    print(f"  Total rows: {len(df):,}")
    print(f"  Columns: {list(df.columns)}")

    # Check if job_submit_time exists
    if 'job_submit_time' not in df.columns:
        print(f"\nERROR: 'job_submit_time' column not found in input file")
        sys.exit(1)

    # Show original date range
    original_min = df['job_submit_time'].min()
    original_max = df['job_submit_time'].max()
    original_min_dt = datetime.fromtimestamp(original_min)
    original_max_dt = datetime.fromtimestamp(original_max)
    print(f"\nOriginal date range:")
    print(f"  {original_min_dt} to {original_max_dt}")
    print(f"  (epoch: {original_min} to {original_max})")

    # Apply filter
    print(f"\nApplying filter...")

    # Build filter mask
    if start_epoch is not None and end_epoch is not None:
        mask = (df['job_submit_time'] >= start_epoch) & (df['job_submit_time'] <= end_epoch)
        filter_desc = f"jobs between {start_date_str} and {end_date_str}"
    elif start_epoch is not None:
        mask = df['job_submit_time'] >= start_epoch
        filter_desc = f"jobs from {start_date_str} onwards"
    elif end_epoch is not None:
        mask = df['job_submit_time'] <= end_epoch
        filter_desc = f"jobs until {end_date_str}"
    else:
        mask = pd.Series([True] * len(df))
        filter_desc = "all jobs (no date filter)"

    df_filtered = df[mask].copy()

    print(f"  Selected {len(df_filtered):,} {filter_desc}")
    print(f"  Percentage: {len(df_filtered)/len(df)*100:.2f}%")

    if len(df_filtered) == 0:
        print(f"\nWARNING: No jobs found in the specified date range!")
        print(f"  Input file range:  {original_min_dt} to {original_max_dt}")
        print(f"  Requested range:   {start_date_str or 'start'} to {end_date_str or 'end'}")
        sys.exit(1)

    # Show selected date range
    selected_min = df_filtered['job_submit_time'].min()
    selected_max = df_filtered['job_submit_time'].max()
    selected_min_dt = datetime.fromtimestamp(selected_min)
    selected_max_dt = datetime.fromtimestamp(selected_max)

    print(f"\nSelected date range:")
    print(f"  {selected_min_dt} to {selected_max_dt}")
    print(f"  (epoch: {selected_min} to {selected_max})")

    # Save to output file
    print(f"\nSaving to {output_file}...")
    df_filtered.to_csv(output_file, index=False)

    # Show file sizes
    input_size = os.path.getsize(input_file) / (1024**2)
    output_size = os.path.getsize(output_file) / (1024**2)

    print(f"  Input size:  {input_size:.2f} MB")
    print(f"  Output size: {output_size:.2f} MB")

    # Show sample
    print(f"\nSample (first 5 rows):")
    print(df_filtered.head(5).to_string(index=False))

    print("\n" + "="*70)
    print("SELECTION COMPLETE")
    print("="*70)
    print(f"\nSelected {len(df_filtered):,} jobs from {len(df):,} total")
    print(f"Output saved to: {output_file}")

def main():
    print(f"Configuration:")
    print(f"  INPUT_FILE:  {INPUT_FILE}")
    print(f"  OUTPUT_FILE: {OUTPUT_FILE}")
    print(f"  START_DATE:  {START_DATE or '(beginning)'}")
    print(f"  END_DATE:    {END_DATE or '(end)'}")
    print()

    # Allow command-line override
    if len(sys.argv) > 1:
        if sys.argv[1] in ['-h', '--help']:
            print("Usage:")
            print("  python3 select_trace_period.py [input_file] [output_file] [start_date] [end_date]")
            print()
            print("Arguments:")
            print("  input_file:  Input trace CSV file")
            print("  output_file: Output trace CSV file")
            print("  start_date:  Start date (MM-DD-YYYY) or 'None' for beginning")
            print("  end_date:    End date (MM-DD-YYYY) or 'None' for end")
            print()
            print("Example:")
            print("  python3 select_trace_period.py traces/21_03_scheduling_trace.csv output.csv 03-01-2021 03-15-2021")
            print("  python3 select_trace_period.py traces/21_03_scheduling_trace.csv output.csv None 03-15-2021")
            print("  python3 select_trace_period.py traces/21_03_scheduling_trace.csv output.csv 03-15-2021 None")
            print()
            print("Or edit the configuration at the top of this script.")
            sys.exit(0)

        input_file = sys.argv[1]
        output_file = sys.argv[2] if len(sys.argv) > 2 else OUTPUT_FILE
        start_date = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3].lower() != 'none' else None
        end_date = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4].lower() != 'none' else None
    else:
        input_file = INPUT_FILE
        output_file = OUTPUT_FILE
        start_date = START_DATE
        end_date = END_DATE

    select_period(input_file, output_file, start_date, end_date)

if __name__ == '__main__':
    main()
