#!/usr/bin/env python3
"""
Analyze differences between nnumr, nnuma, and nnumu columns
"""

import pandas as pd
import numpy as np

# Load one parquet file for analysis
PARQUET_FILE = '21_03.parquet'

print(f"Loading {PARQUET_FILE}...")
df = pd.read_parquet(PARQUET_FILE)

# Check if columns exist
required_cols = ['nnumr', 'nnuma', 'nnumu']
available_cols = [col for col in required_cols if col in df.columns]
print(f"Available columns: {available_cols}\n")

if len(available_cols) < 3:
    print(f"Missing columns: {[col for col in required_cols if col not in df.columns]}")
    exit(1)

# Basic statistics
print("="*70)
print("BASIC STATISTICS")
print("="*70)
print(df[['nnumr', 'nnuma', 'nnumu']].describe())

# Check for differences
print("\n" + "="*70)
print("DIFFERENCE ANALYSIS")
print("="*70)

# Compare each pair
df['nnumr_eq_nnuma'] = df['nnumr'] == df['nnuma']
df['nnumr_eq_nnumu'] = df['nnumr'] == df['nnumu']
df['nnuma_eq_nnumu'] = df['nnuma'] == df['nnumu']
df['all_equal'] = df['nnumr_eq_nnuma'] & df['nnumr_eq_nnumu']

print(f"\nTotal rows: {len(df):,}")
print(f"nnumr == nnuma: {df['nnumr_eq_nnuma'].sum():,} ({df['nnumr_eq_nnuma'].sum()/len(df)*100:.2f}%)")
print(f"nnumr == nnumu: {df['nnumr_eq_nnumu'].sum():,} ({df['nnumr_eq_nnumu'].sum()/len(df)*100:.2f}%)")
print(f"nnuma == nnumu: {df['nnuma_eq_nnumu'].sum():,} ({df['nnuma_eq_nnumu'].sum()/len(df)*100:.2f}%)")
print(f"All three equal: {df['all_equal'].sum():,} ({df['all_equal'].sum()/len(df)*100:.2f}%)")

# Analyze cases where they differ
print("\n" + "="*70)
print("CASES WHERE THEY DIFFER")
print("="*70)

diff_df = df[~df['all_equal']].copy()
print(f"\nRows where at least one differs: {len(diff_df):,} ({len(diff_df)/len(df)*100:.2f}%)")

if len(diff_df) > 0:
    print("\nSample of differing rows:")
    print(diff_df[['nnumr', 'nnuma', 'nnumu']].head(10))

    print("\nStatistics for differing rows:")
    print(diff_df[['nnumr', 'nnuma', 'nnumu']].describe())

# Check hypothesis: differences only when nnumr > 384 and nnuma is multiple of 48
print("\n" + "="*70)
print("HYPOTHESIS TEST: nnumr > 384 AND nnuma % 48 == 0")
print("="*70)

# Check condition
df['nnumr_gt_384'] = df['nnumr'] > 384
df['nnuma_mult_48'] = (df['nnuma'] % 48) == 0
df['meets_condition'] = df['nnumr_gt_384'] & df['nnuma_mult_48']

# Cases where values differ
differs = ~df['all_equal']

print(f"\nRows where nnumr > 384: {df['nnumr_gt_384'].sum():,}")
print(f"Rows where nnuma is multiple of 48: {df['nnuma_mult_48'].sum():,}")
print(f"Rows meeting both conditions: {df['meets_condition'].sum():,}")

print(f"\nRows where values differ: {differs.sum():,}")
print(f"  - AND meets condition (nnumr>384 & nnuma%48==0): {(differs & df['meets_condition']).sum():,}")
print(f"  - AND does NOT meet condition: {(differs & ~df['meets_condition']).sum():,}")

# Reverse check
print(f"\nRows meeting condition (nnumr>384 & nnuma%48==0): {df['meets_condition'].sum():,}")
print(f"  - AND values differ: {(df['meets_condition'] & differs).sum():,}")
print(f"  - AND values are equal: {(df['meets_condition'] & ~differs).sum():,}")

# Show examples where hypothesis doesn't hold
print("\n" + "="*70)
print("COUNTER-EXAMPLES TO HYPOTHESIS")
print("="*70)

# Differ but don't meet condition
counter1 = df[differs & ~df['meets_condition']]
if len(counter1) > 0:
    print(f"\nRows that DIFFER but DON'T meet condition: {len(counter1):,}")
    print("Sample:")
    print(counter1[['nnumr', 'nnuma', 'nnumu']].head(10))
else:
    print("\nNo counter-examples: All differing rows meet the condition!")

# Meet condition but don't differ
counter2 = df[df['meets_condition'] & ~differs]
if len(counter2) > 0:
    print(f"\nRows that MEET condition but DON'T differ: {len(counter2):,}")
    print("Sample:")
    print(counter2[['nnumr', 'nnuma', 'nnumu']].head(10))
else:
    print("\nPerfect match: All rows meeting condition have differing values!")

# Additional analysis: when do nnuma and nnumu differ?
print("\n" + "="*70)
print("ADDITIONAL: nnuma vs nnumu DIFFERENCES")
print("="*70)

nnuma_ne_nnumu = df['nnuma'] != df['nnumu']
print(f"\nRows where nnuma != nnumu: {nnuma_ne_nnumu.sum():,} ({nnuma_ne_nnumu.sum()/len(df)*100:.2f}%)")

if nnuma_ne_nnumu.sum() > 0:
    print("\nSample where nnuma != nnumu:")
    print(df[nnuma_ne_nnumu][['nnumr', 'nnuma', 'nnumu']].head(10))
