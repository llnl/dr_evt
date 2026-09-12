#!/usr/bin/env python3
"""
Analyze relationships between time fields:
- Is duration = edt - sdt?
- What is the difference between adt and qdt?
- What is the difference between deldt and edt?
"""

import pandas as pd
import numpy as np

PARQUET_FILE = '21_03.parquet'

print(f"Loading {PARQUET_FILE}...")
df = pd.read_parquet(PARQUET_FILE)

# Convert datetime columns from string to datetime
datetime_cols = ['adt', 'qdt', 'schedsdt', 'sdt', 'edt', 'deldt']
for col in datetime_cols:
    if col in df.columns:
        df[col] = pd.to_datetime(df[col])

# Check available time columns
time_cols = ['adt', 'qdt', 'schedsdt', 'sdt', 'edt', 'deldt', 'duration']
available_time_cols = [col for col in time_cols if col in df.columns]
print(f"Available time columns: {available_time_cols}\n")

print("="*70)
print("TIME FIELD DEFINITIONS (from feature_list.csv)")
print("="*70)
print("adt      : Arrival datetime (submission time)")
print("qdt      : Time of insertion in the job queue")
print("schedsdt : Time of completed scheduling choice")
print("sdt      : Start datetime")
print("edt      : End datetime")
print("deldt    : Time of job deletion")
print("duration : Duration of the job execution in seconds")

# Question 1: Is duration = edt - sdt?
print("\n" + "="*70)
print("QUESTION 1: Is duration = edt - sdt?")
print("="*70)

if 'edt' in df.columns and 'sdt' in df.columns and 'duration' in df.columns:
    df_clean = df[['sdt', 'edt', 'duration']].dropna()

    # Calculate duration from timestamps
    df_clean['calc_duration'] = (df_clean['edt'] - df_clean['sdt']).dt.total_seconds()
    df_clean['diff'] = df_clean['duration'] - df_clean['calc_duration']
    df_clean['abs_diff'] = np.abs(df_clean['diff'])

    print(f"\nRows with all values: {len(df_clean):,}")
    print(f"\nDifference statistics (duration - (edt-sdt)):")
    print(df_clean['diff'].describe())

    print(f"\nRows where duration == (edt-sdt): {(df_clean['diff'] == 0).sum():,} ({(df_clean['diff'] == 0).sum()/len(df_clean)*100:.2f}%)")
    print(f"Rows with difference < 1 second: {(df_clean['abs_diff'] < 1).sum():,} ({(df_clean['abs_diff'] < 1).sum()/len(df_clean)*100:.2f}%)")
    print(f"Rows with difference < 5 seconds: {(df_clean['abs_diff'] < 5).sum():,} ({(df_clean['abs_diff'] < 5).sum()/len(df_clean)*100:.2f}%)")

    print("\nSample comparison:")
    print(df_clean[['duration', 'calc_duration', 'diff']].head(10))

    print("\nConclusion: ", end="")
    if (df_clean['abs_diff'] < 1).sum() / len(df_clean) > 0.99:
        print("YES - duration ≈ edt - sdt (within rounding)")
    else:
        print("NO - there are systematic differences")

# Question 2: What is the difference between adt and qdt?
print("\n" + "="*70)
print("QUESTION 2: What is the difference between adt and qdt?")
print("="*70)

if 'adt' in df.columns and 'qdt' in df.columns:
    df_q2 = df[['adt', 'qdt', 'schedsdt']].dropna(subset=['adt', 'qdt'])

    # Calculate time differences
    df_q2['adt_to_qdt'] = (df_q2['qdt'] - df_q2['adt']).dt.total_seconds()

    print(f"\nRows with both adt and qdt: {len(df_q2):,}")
    print(f"\nTime from submission (adt) to queue (qdt) in seconds:")
    print(df_q2['adt_to_qdt'].describe())

    print(f"\nDistribution:")
    print(f"  Same time (0s):        {(df_q2['adt_to_qdt'] == 0).sum():,} ({(df_q2['adt_to_qdt'] == 0).sum()/len(df_q2)*100:.2f}%)")
    print(f"  < 1 second:            {(df_q2['adt_to_qdt'] < 1).sum():,} ({(df_q2['adt_to_qdt'] < 1).sum()/len(df_q2)*100:.2f}%)")
    print(f"  1-10 seconds:          {((df_q2['adt_to_qdt'] >= 1) & (df_q2['adt_to_qdt'] < 10)).sum():,}")
    print(f"  10-60 seconds:         {((df_q2['adt_to_qdt'] >= 10) & (df_q2['adt_to_qdt'] < 60)).sum():,}")
    print(f"  1-10 minutes:          {((df_q2['adt_to_qdt'] >= 60) & (df_q2['adt_to_qdt'] < 600)).sum():,}")
    print(f"  > 10 minutes:          {(df_q2['adt_to_qdt'] >= 600).sum():,}")

    print("\nSample jobs:")
    print(df_q2[['adt', 'qdt', 'adt_to_qdt']].head(10))

    # Also check schedsdt if available
    if 'schedsdt' in df.columns:
        df_q2_sched = df_q2.dropna(subset=['schedsdt'])
        df_q2_sched['adt_to_schedsdt'] = (df_q2_sched['schedsdt'] - df_q2_sched['adt']).dt.total_seconds()
        df_q2_sched['schedsdt_to_qdt'] = (df_q2_sched['qdt'] - df_q2_sched['schedsdt']).dt.total_seconds()

        print(f"\n\nWith scheduling time (schedsdt):")
        print(f"  adt -> schedsdt (scheduling decision time): {df_q2_sched['adt_to_schedsdt'].median():.2f}s (median)")
        print(f"  schedsdt -> qdt (queue insertion time):     {df_q2_sched['schedsdt_to_qdt'].median():.2f}s (median)")

# Question 3: What is the difference between deldt and edt?
print("\n" + "="*70)
print("QUESTION 3: What is the difference between deldt and edt?")
print("="*70)

if 'deldt' in df.columns and 'edt' in df.columns:
    # deldt might be null for jobs that weren't deleted
    df_q3 = df[['edt', 'deldt', 'ec']].dropna(subset=['deldt'])

    if len(df_q3) > 0:
        df_q3['edt_to_deldt'] = (df_q3['deldt'] - df_q3['edt']).dt.total_seconds()

        print(f"\nRows with deldt (deleted jobs): {len(df_q3):,} ({len(df_q3)/len(df)*100:.2f}%)")
        print(f"Rows without deldt (normal jobs): {df['deldt'].isna().sum():,} ({df['deldt'].isna().sum()/len(df)*100:.2f}%)")

        print(f"\nTime from end (edt) to deletion (deldt) in seconds:")
        print(df_q3['edt_to_deldt'].describe())

        print(f"\nDistribution:")
        print(f"  Same time (0s):        {(df_q3['edt_to_deldt'] == 0).sum():,} ({(df_q3['edt_to_deldt'] == 0).sum()/len(df_q3)*100:.2f}%)")
        print(f"  < 1 second:            {(df_q3['edt_to_deldt'] < 1).sum():,} ({(df_q3['edt_to_deldt'] < 1).sum()/len(df_q3)*100:.2f}%)")
        print(f"  1-10 seconds:          {((df_q3['edt_to_deldt'] >= 1) & (df_q3['edt_to_deldt'] < 10)).sum():,}")
        print(f"  > 10 seconds:          {(df_q3['edt_to_deldt'] >= 10).sum():,}")

        print("\nSample deleted jobs:")
        print(df_q3[['edt', 'deldt', 'edt_to_deldt', 'ec']].head(10))

        print("\nNote: deldt is only set when a job is explicitly deleted by the user.")
        print("Most jobs complete normally and don't have a deldt value.")
    else:
        print("\nNo jobs with deldt in this dataset.")
        print("deldt is only set when jobs are explicitly deleted by users.")

# Timeline visualization
print("\n" + "="*70)
print("TIMELINE SUMMARY")
print("="*70)

if all(col in df.columns for col in ['adt', 'qdt', 'schedsdt', 'sdt', 'edt']):
    df_timeline = df[['adt', 'qdt', 'schedsdt', 'sdt', 'edt']].dropna().head(1).iloc[0]

    print("\nTypical job timeline:")
    print(f"  1. adt      (submission):     {df_timeline['adt']}")
    print(f"  2. schedsdt (scheduled):      {df_timeline['schedsdt']}")
    print(f"  3. qdt      (queued):         {df_timeline['qdt']}")
    print(f"  4. sdt      (start):          {df_timeline['sdt']}")
    print(f"  5. edt      (end):            {df_timeline['edt']}")

    print("\n  Intervals:")
    print(f"    Submit -> Schedule: {(df_timeline['schedsdt'] - df_timeline['adt']).total_seconds():.2f}s")
    print(f"    Schedule -> Queue:  {(df_timeline['qdt'] - df_timeline['schedsdt']).total_seconds():.2f}s")
    print(f"    Queue -> Start:     {(df_timeline['sdt'] - df_timeline['qdt']).total_seconds():.2f}s")
    print(f"    Start -> End:       {(df_timeline['edt'] - df_timeline['sdt']).total_seconds():.2f}s (= duration)")
