#!/usr/bin/env python3
"""
Analyze and plot histogram of avgpcon/nnumu (average power per node used).
Loads data from original parquet files since trace files don't include nnumu.
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import glob
import os

# Configuration
INPUT_PATTERN = '*.parquet'
OUTPUT_DIR = '.'
OUTPUT_FILE = 'power_per_node_histogram.png'

def load_and_analyze():
    """Load parquet files and calculate avgpcon/nnumu."""

    print("="*70)
    print("POWER PER NODE ANALYSIS")
    print("="*70)

    # Find parquet files
    files = sorted(glob.glob(INPUT_PATTERN))

    if not files:
        print(f"\nERROR: No parquet files found matching '{INPUT_PATTERN}'")
        return None

    print(f"\nFound {len(files)} parquet files")
    print(f"Loading data...")

    # Load and combine data
    all_data = []
    total_jobs = 0

    for i, file in enumerate(files, 1):
        df = pd.read_parquet(file)

        # Extract relevant columns
        subset = df[['avgpcon', 'nnumu']].copy()
        all_data.append(subset)
        total_jobs += len(subset)

        if i % 10 == 0:
            print(f"  Loaded {i}/{len(files)} files ({total_jobs:,} jobs so far)...")

    # Combine all data
    combined = pd.concat(all_data, ignore_index=True)
    print(f"  Total: {len(combined):,} jobs from {len(files)} files")

    return combined

def analyze_power_per_node(df):
    """Calculate and analyze avgpcon/nnumu ratio."""

    print("\n" + "="*70)
    print("CALCULATING POWER PER NODE")
    print("="*70)

    # Remove rows with missing or zero values
    original_len = len(df)
    df = df.dropna(subset=['avgpcon', 'nnumu'])
    df = df[df['nnumu'] > 0]  # Avoid division by zero
    df = df[df['avgpcon'] >= 0]  # Remove corrupted negative values

    removed = original_len - len(df)
    if removed > 0:
        print(f"\nRemoved {removed:,} jobs with missing/invalid data")
        print(f"  ({removed/original_len*100:.2f}% of total)")

    # Calculate power per node
    df['power_per_node'] = df['avgpcon'] / df['nnumu']

    print(f"\nAnalyzing {len(df):,} jobs...")

    # Basic statistics
    print("\nPower per node (watts):")
    print(df['power_per_node'].describe())

    # Percentiles
    print("\nPercentiles:")
    percentiles = [1, 5, 10, 25, 50, 75, 90, 95, 99]
    for p in percentiles:
        val = df['power_per_node'].quantile(p/100)
        print(f"  {p:>2d}th: {val:>8.2f} W")

    # Common ranges
    print("\nDistribution by range:")
    ranges = [
        (0, 50, "0-50 W"),
        (50, 100, "50-100 W"),
        (100, 150, "100-150 W"),
        (150, 200, "150-200 W"),
        (200, 300, "200-300 W"),
        (300, 500, "300-500 W"),
        (500, 1000, "500-1000 W"),
        (1000, float('inf'), ">1000 W")
    ]

    for min_val, max_val, label in ranges:
        count = ((df['power_per_node'] >= min_val) & (df['power_per_node'] < max_val)).sum()
        if count > 0:
            print(f"  {label:>15s}: {count:>10,} jobs ({count/len(df)*100:>5.2f}%)")

    return df

def create_histogram(df, output_dir, output_file):
    """Create histogram plot of power per node."""

    print("\n" + "="*70)
    print("CREATING HISTOGRAM")
    print("="*70)

    power_per_node = df['power_per_node']

    # Create figure with two subplots
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

    # Histogram 1: Linear scale (filter outliers for better visualization)
    # Most nodes should be in 0-300W range
    filtered = power_per_node[power_per_node <= 500]

    ax1.hist(filtered, bins=100, edgecolor='black', alpha=0.7, color='mediumseagreen')
    ax1.set_xlabel('Power per Node (watts)', fontsize=12)
    ax1.set_ylabel('Number of Jobs', fontsize=12)
    ax1.set_title('Distribution of Power per Node Used (avgpcon/nnumu)',
                  fontsize=14, fontweight='bold')
    ax1.grid(axis='y', alpha=0.3)

    # Add statistics text
    stats_text = f'Total jobs: {len(df):,}\n'
    stats_text += f'Median: {power_per_node.median():.1f} W\n'
    stats_text += f'Mean: {power_per_node.mean():.1f} W\n'
    stats_text += f'Std: {power_per_node.std():.1f} W'
    ax1.text(0.98, 0.97, stats_text, transform=ax1.transAxes,
             verticalalignment='top', horizontalalignment='right',
             bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5),
             fontsize=10)

    # Histogram 2: Log scale for full range
    ax2.hist(power_per_node,
             bins=np.logspace(np.log10(max(power_per_node.min(), 0.1)),
                              np.log10(power_per_node.max()), 50),
             edgecolor='black', alpha=0.7, color='mediumseagreen')
    ax2.set_xscale('log')
    ax2.set_yscale('log')
    ax2.set_xlabel('Power per Node (watts, log scale)', fontsize=12)
    ax2.set_ylabel('Number of Jobs (log scale)', fontsize=12)
    ax2.set_title('Distribution - Log Scale', fontsize=14, fontweight='bold')
    ax2.grid(True, alpha=0.3)

    # Add percentile markers
    percentiles = [50, 90, 99]
    colors = ['blue', 'orange', 'red']
    for p, color in zip(percentiles, colors):
        val = power_per_node.quantile(p/100)
        ax2.axvline(x=val, color=color, linestyle='--', linewidth=1.5,
                   label=f'P{p}: {val:.1f}W')
    ax2.legend()

    plt.tight_layout()

    # Save figure
    output_path = os.path.join(output_dir, output_file)
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"\nSaved histogram to: {output_path}")

    plt.close('all')

def main():
    # Load data
    df = load_and_analyze()

    if df is None:
        return

    # Analyze power per node
    df = analyze_power_per_node(df)

    if df is None or len(df) == 0:
        print("\nERROR: No valid data to plot")
        return

    # Create histogram
    create_histogram(df, OUTPUT_DIR, OUTPUT_FILE)

    print("\n" + "="*70)
    print("ANALYSIS COMPLETE")
    print("="*70)
    print(f"\nHistogram saved to: {OUTPUT_FILE}")

if __name__ == '__main__':
    main()
