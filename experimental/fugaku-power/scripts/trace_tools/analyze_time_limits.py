#!/usr/bin/env python3
"""
Analyze time_limit and duration/time_limit ratio across all trace files.
Creates histograms to understand user behavior in setting time limits.
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import glob
import os

# Configuration
INPUT_DIR = 'traces_no_times'
INPUT_PATTERN = '*_scheduling_trace.csv'
OUTPUT_DIR = '.'

def load_all_traces(input_dir, pattern):
    """Load and combine all trace files."""

    input_pattern = os.path.join(input_dir, pattern)
    files = sorted(glob.glob(input_pattern))

    if not files:
        print(f"ERROR: No files found matching {input_pattern}")
        return None

    print(f"Loading {len(files)} trace files...")

    dfs = []
    total_rows = 0

    for i, file in enumerate(files, 1):
        df = pd.read_csv(file)
        dfs.append(df)
        total_rows += len(df)

        if i % 10 == 0:
            print(f"  Loaded {i}/{len(files)} files ({total_rows:,} jobs so far)...")

    combined = pd.concat(dfs, ignore_index=True)
    print(f"  Total: {len(combined):,} jobs from {len(files)} files")

    return combined

def analyze_time_limits(df):
    """Analyze time_limit distribution."""

    print("\n" + "="*70)
    print("TIME LIMIT ANALYSIS")
    print("="*70)

    # Basic statistics
    print("\nBasic statistics (seconds):")
    print(df['time_limit'].describe())

    # Convert to hours for easier interpretation
    df['time_limit_hours'] = df['time_limit'] / 3600

    print("\nBasic statistics (hours):")
    print(df['time_limit_hours'].describe())

    # Common values
    print("\nMost common time limits:")
    top_limits = df['time_limit'].value_counts().head(10)
    for limit, count in top_limits.items():
        hours = limit / 3600
        print(f"  {limit:>8.0f}s ({hours:>6.1f}h): {count:>8,} jobs ({count/len(df)*100:>5.2f}%)")

    # Distribution
    print("\nDistribution by range:")
    ranges = [
        (0, 600, "0-10 minutes"),
        (600, 3600, "10min-1h"),
        (3600, 7200, "1-2 hours"),
        (7200, 21600, "2-6 hours"),
        (21600, 43200, "6-12 hours"),
        (43200, 86400, "12-24 hours"),
        (86400, 259200, "1-3 days"),
        (259200, float('inf'), ">3 days")
    ]

    for min_val, max_val, label in ranges:
        count = ((df['time_limit'] >= min_val) & (df['time_limit'] < max_val)).sum()
        if count > 0:
            print(f"  {label:>15s}: {count:>8,} jobs ({count/len(df)*100:>5.2f}%)")

def analyze_utilization(df):
    """Analyze duration/time_limit ratio (utilization)."""

    print("\n" + "="*70)
    print("TIME LIMIT UTILIZATION ANALYSIS")
    print("="*70)

    # Calculate ratio
    df['utilization'] = df['duration'] / df['time_limit']

    # Basic statistics
    print("\nUtilization ratio (duration / time_limit):")
    print(df['utilization'].describe())

    # Distribution
    print("\nDistribution:")
    ranges = [
        (0, 0.1, "0-10%"),
        (0.1, 0.25, "10-25%"),
        (0.25, 0.5, "25-50%"),
        (0.5, 0.75, "50-75%"),
        (0.75, 0.9, "75-90%"),
        (0.9, 1.0, "90-100%"),
        (1.0, float('inf'), ">100% (exceeded)")
    ]

    for min_val, max_val, label in ranges:
        count = ((df['utilization'] >= min_val) & (df['utilization'] < max_val)).sum()
        if count > 0:
            print(f"  {label:>15s}: {count:>8,} jobs ({count/len(df)*100:>5.2f}%)")

    # Jobs that exceeded time limit
    exceeded = df[df['utilization'] > 1.0]
    print(f"\nJobs exceeding time limit: {len(exceeded):,} ({len(exceeded)/len(df)*100:.3f}%)")

    # Analyze by exit status
    if 'exit_status' in df.columns:
        print("\nUtilization by exit status:")
        for status in df['exit_status'].unique():
            subset = df[df['exit_status'] == status]
            print(f"  {status}:")
            print(f"    Mean: {subset['utilization'].mean():.3f}")
            print(f"    Median: {subset['utilization'].median():.3f}")
            print(f"    >90%: {(subset['utilization'] > 0.9).sum():,} ({(subset['utilization'] > 0.9).sum()/len(subset)*100:.2f}%)")
            print(f"    >100%: {(subset['utilization'] > 1.0).sum():,} ({(subset['utilization'] > 1.0).sum()/len(subset)*100:.2f}%)")

def create_histograms(df, output_dir):
    """Create histogram plots."""

    print("\n" + "="*70)
    print("CREATING HISTOGRAMS")
    print("="*70)

    # Create figure with two subplots
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

    # Histogram 1: Time Limit (in hours, log scale)
    time_limit_hours = df['time_limit'] / 3600

    # Filter out extreme outliers for better visualization
    time_limit_filtered = time_limit_hours[time_limit_hours <= 72]  # Up to 3 days

    ax1.hist(time_limit_filtered, bins=100, edgecolor='black', alpha=0.7, color='steelblue')
    ax1.set_xlabel('Time Limit (hours)', fontsize=12)
    ax1.set_ylabel('Number of Jobs', fontsize=12)
    ax1.set_title('Distribution of Job Time Limits', fontsize=14, fontweight='bold')
    ax1.grid(axis='y', alpha=0.3)

    # Add statistics text
    stats_text = f'Total jobs: {len(df):,}\n'
    stats_text += f'Median: {time_limit_hours.median():.1f}h\n'
    stats_text += f'Mean: {time_limit_hours.mean():.1f}h\n'
    stats_text += f'Max: {time_limit_hours.max():.1f}h'
    ax1.text(0.98, 0.97, stats_text, transform=ax1.transAxes,
             verticalalignment='top', horizontalalignment='right',
             bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5),
             fontsize=10)

    # Histogram 2: Utilization Ratio
    df['utilization'] = df['duration'] / df['time_limit']
    utilization = df['utilization']

    # Filter out extreme outliers (>200%)
    utilization_filtered = utilization[utilization <= 2.0]

    ax2.hist(utilization_filtered, bins=100, edgecolor='black', alpha=0.7, color='coral')
    ax2.set_xlabel('Duration / Time Limit', fontsize=12)
    ax2.set_ylabel('Number of Jobs', fontsize=12)
    ax2.set_title('Distribution of Time Limit Utilization', fontsize=14, fontweight='bold')
    ax2.axvline(x=1.0, color='red', linestyle='--', linewidth=2, label='Time Limit')
    ax2.grid(axis='y', alpha=0.3)
    ax2.legend()

    # Add statistics text
    stats_text = f'Median: {utilization.median():.3f}\n'
    stats_text += f'Mean: {utilization.mean():.3f}\n'
    stats_text += f'>90%: {(utilization > 0.9).sum()/len(df)*100:.1f}%\n'
    stats_text += f'>100%: {(utilization > 1.0).sum()/len(df)*100:.1f}%'
    ax2.text(0.98, 0.97, stats_text, transform=ax2.transAxes,
             verticalalignment='top', horizontalalignment='right',
             bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5),
             fontsize=10)

    plt.tight_layout()

    # Save figure
    output_file = os.path.join(output_dir, 'time_limit_analysis.png')
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"\nSaved histogram to: {output_file}")

    # Create additional log-scale plots
    fig2, (ax3, ax4) = plt.subplots(1, 2, figsize=(14, 5))

    # Log scale histogram for time limits
    ax3.hist(time_limit_hours, bins=np.logspace(np.log10(time_limit_hours.min()),
                                                  np.log10(time_limit_hours.max()), 50),
             edgecolor='black', alpha=0.7, color='steelblue')
    ax3.set_xscale('log')
    ax3.set_yscale('log')
    ax3.set_xlabel('Time Limit (hours, log scale)', fontsize=12)
    ax3.set_ylabel('Number of Jobs (log scale)', fontsize=12)
    ax3.set_title('Time Limits - Log Scale', fontsize=14, fontweight='bold')
    ax3.grid(True, alpha=0.3)

    # Utilization on linear scale with more detail
    ax4.hist(utilization_filtered, bins=np.linspace(0, 1.5, 100),
             edgecolor='black', alpha=0.7, color='coral')
    ax4.set_xlabel('Duration / Time Limit', fontsize=12)
    ax4.set_ylabel('Number of Jobs', fontsize=12)
    ax4.set_title('Utilization - Detailed View (0-150%)', fontsize=14, fontweight='bold')
    ax4.axvline(x=1.0, color='red', linestyle='--', linewidth=2, label='Time Limit')
    ax4.grid(axis='y', alpha=0.3)
    ax4.legend()

    plt.tight_layout()

    output_file2 = os.path.join(output_dir, 'time_limit_analysis_logscale.png')
    plt.savefig(output_file2, dpi=300, bbox_inches='tight')
    print(f"Saved log-scale histogram to: {output_file2}")

    plt.close('all')

def main():
    print("="*70)
    print("TIME LIMIT AND UTILIZATION ANALYSIS")
    print("="*70)

    # Load all traces
    df = load_all_traces(INPUT_DIR, INPUT_PATTERN)

    if df is None:
        return

    # Remove rows with missing values
    original_len = len(df)
    df = df.dropna(subset=['time_limit', 'duration'])
    if len(df) < original_len:
        print(f"\nRemoved {original_len - len(df):,} rows with missing values")

    # Analyze time limits
    analyze_time_limits(df)

    # Analyze utilization
    analyze_utilization(df)

    # Create histograms
    create_histograms(df, OUTPUT_DIR)

    print("\n" + "="*70)
    print("ANALYSIS COMPLETE")
    print("="*70)

if __name__ == '__main__':
    main()
