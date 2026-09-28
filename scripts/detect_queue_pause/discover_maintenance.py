#!/usr/bin/env python3
"""Infer maintenance, queue pauses, and normal capacity from job traces.

The input must contain at least these columns:
    job_submit_time, begin_time, end_time, num_nodes

Times are Unix seconds.  Only the Python standard library is required.
"""

import argparse
import csv
import glob
import json
import math
import os
import re
import sys
from collections import defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


REQUIRED_COLUMNS = ("job_submit_time", "begin_time", "end_time", "num_nodes")
FIXED_TIMEZONES = {
    "UTC": 0,
    "GMT": 0,
    "PST": -8,
    "PDT": -7,
    "MST": -7,
    "MDT": -6,
    "CST": -6,
    "CDT": -5,
    "EST": -5,
    "EDT": -4,
    "JST": 9,
    "KST": 9,
}


def resolve_timezone(name):
    """Resolve fixed abbreviations and, when available, IANA zone names."""
    normalized = name.upper()
    if normalized in FIXED_TIMEZONES:
        return timezone.utc if normalized in ("UTC", "GMT") else timezone(
            timedelta(hours=FIXED_TIMEZONES[normalized]), normalized
        )
    try:
        from zoneinfo import ZoneInfo
        return ZoneInfo(name)
    except (ImportError, KeyError):
        pass
    try:
        import pytz
    except ImportError:
        pytz = None
    if pytz is not None:
        try:
            return pytz.timezone(name)
        except pytz.UnknownTimeZoneError:
            pass
    raise ValueError(
        "unknown timezone {!r}; use UTC, PST, PDT, JST, a numeric offset "
        "such as +09:00, or an installed IANA name such as Asia/Tokyo".format(name)
    )


def parse_numeric_timezone(name):
    if len(name) != 6 or name[0] not in "+-" or name[3] != ":":
        return None
    try:
        hours = int(name[1:3])
        minutes = int(name[4:6])
    except ValueError:
        return None
    if hours > 23 or minutes > 59:
        return None
    offset = (hours * 60 + minutes) * (1 if name[0] == "+" else -1)
    return timezone(timedelta(minutes=offset), name)


class TimestampFormatter:
    def __init__(self, output_format, timezone_name):
        self.output_format = output_format
        numeric = parse_numeric_timezone(timezone_name)
        self.timezone = numeric if numeric is not None else resolve_timezone(timezone_name)
        self.timezone_name = timezone_name

    def value(self, epoch):
        if self.output_format == "epoch":
            return int(epoch)
        value = datetime.fromtimestamp(epoch, self.timezone).isoformat()
        return value.replace("+00:00", "Z")

    def month(self, epoch):
        return datetime.fromtimestamp(epoch, self.timezone).strftime("%Y-%m")

    def month_bounds(self, year, month):
        next_year, next_month = (year + 1, 1) if month == 12 else (year, month + 1)
        start_naive = datetime(year, month, 1)
        end_naive = datetime(next_year, next_month, 1)
        if hasattr(self.timezone, "localize"):
            start = self.timezone.localize(start_naive)
            end = self.timezone.localize(end_naive)
        else:
            start = start_naive.replace(tzinfo=self.timezone)
            end = end_naive.replace(tzinfo=self.timezone)
        return int(start.timestamp()), int(end.timestamp())


class TraceStats:
    def __init__(
        self,
        rows=0,
        invalid_rows=0,
        invalid_intervals=0,
        min_submit=None,
        max_submit=None,
    ):
        self.rows = rows
        self.invalid_rows = invalid_rows
        self.invalid_intervals = invalid_intervals
        self.min_submit = min_submit
        self.max_submit = max_submit
        self.analysis_start = min_submit
        self.analysis_end = None if max_submit is None else max_submit + 1


class TimelineBuilder:
    """Accumulate exact node/job seconds without expanding every interval."""

    def __init__(self, bin_seconds: int) -> None:
        self.bin_seconds = bin_seconds
        self.run_edge = defaultdict(float)  # type: Dict[int, float]
        self.run_full_delta = defaultdict(float)  # type: Dict[int, float]
        self.run_boundary_delta = defaultdict(float)  # type: Dict[int, float]
        self.pending_job_edge = defaultdict(float)  # type: Dict[int, float]
        self.pending_job_full_delta = defaultdict(float)  # type: Dict[int, float]
        self.pending_node_edge = defaultdict(float)  # type: Dict[int, float]
        self.pending_node_full_delta = defaultdict(float)  # type: Dict[int, float]
        self.starts = defaultdict(int)  # type: Dict[int, int]
        self.start_nodes = defaultdict(int)  # type: Dict[int, int]
        self.submits = defaultdict(int)  # type: Dict[int, int]

    def _add_interval(
        self,
        start: int,
        end: int,
        weight: float,
        edges: Dict[int, float],
        full_delta: Dict[int, float],
    ) -> None:
        if end <= start:
            return
        width = self.bin_seconds
        first = start // width
        last = (end - 1) // width
        if first == last:
            edges[first] += (end - start) * weight
            return
        edges[first] += ((first + 1) * width - start) * weight
        edges[last] += (end - last * width) * weight
        if last > first + 1:
            full_delta[first + 1] += weight
            full_delta[last] -= weight

    def add_job(
        self, submit: int, begin: int, end: int, nodes: int,
        window: Optional[Tuple[int, int]] = None,
    ) -> None:
        width = self.bin_seconds
        window_start, window_end = window if window is not None else (-sys.maxsize, sys.maxsize)
        if window_start <= submit < window_end:
            self.submits[submit // width] += 1
        if window_start <= begin < window_end:
            self.starts[begin // width] += 1
            self.start_nodes[begin // width] += nodes
        run_start = max(begin, window_start)
        run_end = min(end, window_end)
        self._add_interval(
            run_start, run_end, nodes,
            self.run_edge, self.run_full_delta,
        )
        # Exact occupancy at each bin boundary. Unlike running_nodes, this is
        # an instantaneous sample and is not averaged over the bin.
        if run_end > run_start:
            first_boundary = (run_start + width - 1) // width
            after_last_boundary = (run_end + width - 1) // width
            if first_boundary < after_last_boundary:
                self.run_boundary_delta[first_boundary] += nodes
                self.run_boundary_delta[after_last_boundary] -= nodes
        self._add_interval(
            max(submit, window_start),
            min(begin, window_end),
            1.0,
            self.pending_job_edge,
            self.pending_job_full_delta,
        )
        self._add_interval(
            max(submit, window_start),
            min(begin, window_end),
            nodes,
            self.pending_node_edge,
            self.pending_node_full_delta,
        )


def expand_paths(patterns: Sequence[str]) -> List[str]:
    paths = []  # type: List[str]
    for pattern in patterns:
        matches = sorted(glob.glob(pattern))
        if matches:
            paths.extend(matches)
        elif os.path.isfile(pattern):
            paths.append(pattern)
        else:
            raise FileNotFoundError(f"no input files match {pattern!r}")
    # Preserve order while eliminating overlap between explicit paths and globs.
    return list(dict.fromkeys(os.path.abspath(path) for path in paths))


def filename_month_window(path, timestamps):
    match = re.match(r"^(\d{2}|\d{4})_(\d{2})_scheduling_trace\.csv$", os.path.basename(path))
    if not match:
        raise ValueError(
            "{}: --overlap-policy filename-month requires a "
            "YY_MM_scheduling_trace.csv filename".format(path)
        )
    year = int(match.group(1))
    if year < 100:
        year += 2000
    month = int(match.group(2))
    if not 1 <= month <= 12:
        raise ValueError("{}: invalid month in filename".format(path))
    return timestamps.month_bounds(year, month)


def read_traces(
    paths: Sequence[str], builder: TimelineBuilder,
    overlap_policy="combine", timestamps=None, analysis_window=None,
) -> TraceStats:
    stats = TraceStats()
    if analysis_window is not None:
        stats.analysis_start, stats.analysis_end = analysis_window
    for path in paths:
        window = analysis_window
        if window is None and overlap_policy == "filename-month":
            window = filename_month_window(path, timestamps)
            stats.analysis_start = (
                window[0] if stats.analysis_start is None
                else min(stats.analysis_start, window[0])
            )
            stats.analysis_end = (
                window[1] if stats.analysis_end is None
                else max(stats.analysis_end, window[1])
            )
        with open(path, "r", newline="", encoding="utf-8-sig") as stream:
            reader = csv.reader(stream)
            try:
                header = next(reader)
            except StopIteration:
                continue
            names = {name.strip(): idx for idx, name in enumerate(header)}
            missing = [name for name in REQUIRED_COLUMNS if name not in names]
            if missing:
                raise ValueError(f"{path}: missing columns: {', '.join(missing)}")
            indices = [names[name] for name in REQUIRED_COLUMNS]
            max_index = max(indices)
            for row in reader:
                stats.rows += 1
                try:
                    if len(row) <= max_index:
                        raise ValueError
                    submit, begin, end, nodes = (
                        int(float(row[index])) for index in indices
                    )
                except (ValueError, OverflowError):
                    stats.invalid_rows += 1
                    continue
                if nodes <= 0 or begin < submit or end < begin:
                    stats.invalid_intervals += 1
                    continue
                stats.min_submit = submit if stats.min_submit is None else min(stats.min_submit, submit)
                stats.max_submit = submit if stats.max_submit is None else max(stats.max_submit, submit)
                builder.add_job(submit, begin, end, nodes, window)
    if not stats.rows or stats.min_submit is None or stats.max_submit is None:
        raise ValueError("no valid job records found")
    if analysis_window is None and overlap_policy == "combine":
        stats.analysis_start = stats.min_submit
        stats.analysis_end = stats.max_submit + 1
    return stats


def quantile(values: Iterable[float], q: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    position = (len(ordered) - 1) * q
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def build_bins(builder: TimelineBuilder, stats: TraceStats) -> List[Dict[str, object]]:
    width = builder.bin_seconds
    first = stats.analysis_start // width
    # In combine mode, stop at the last submission so the drained tail of a
    # truncated trace does not look like a shutdown. Filename-month mode instead
    # uses the nonoverlapping ownership windows established from the filenames.
    last = (stats.analysis_end - 1) // width
    run_full = pending_jobs_full = pending_nodes_full = 0.0
    instantaneous_running = 0.0
    bins = []  # type: List[Dict[str, object]]
    for index in range(first, last + 1):
        run_full += builder.run_full_delta.get(index, 0.0)
        pending_jobs_full += builder.pending_job_full_delta.get(index, 0.0)
        pending_nodes_full += builder.pending_node_full_delta.get(index, 0.0)
        instantaneous_running += builder.run_boundary_delta.get(index, 0.0)
        bins.append(
            {
                "index": index,
                "start_epoch": index * width,
                "running_nodes": run_full + builder.run_edge.get(index, 0.0) / width,
                "instantaneous_running_nodes": instantaneous_running,
                "pending_jobs": pending_jobs_full
                + builder.pending_job_edge.get(index, 0.0) / width,
                "pending_nodes": pending_nodes_full
                + builder.pending_node_edge.get(index, 0.0) / width,
                "jobs_started": builder.starts.get(index, 0),
                "nodes_started": builder.start_nodes.get(index, 0),
                "jobs_submitted": builder.submits.get(index, 0),
            }
        )
    return bins


def infer_capacity(
    bins: Sequence[Dict[str, object]], q: float, known_capacity: Optional[int]
) -> Tuple[float, str]:
    if known_capacity is not None:
        return float(known_capacity), "user supplied"
    running = [float(item["running_nodes"]) for item in bins if item["running_nodes"]]
    capacity = quantile(running, q)
    if capacity <= 0:
        raise ValueError("could not infer capacity: no running jobs in analysis interval")
    return capacity, f"{q:g} quantile of observed hourly-equivalent node allocation"


def capacity_windows(
    bins: Sequence[Dict[str, object]], q: float, global_capacity: float,
    timestamps: TimestampFormatter,
) -> List[Dict[str, object]]:
    grouped = defaultdict(list)  # type: Dict[str, List[Dict[str, object]]]
    for item in bins:
        grouped[timestamps.month(int(item["start_epoch"]))].append(item)
    result = []  # type: List[Dict[str, object]]
    for month, items in sorted(grouped.items()):
        pressure_floor = max(1.0, global_capacity * 0.02)
        pressured = [
            item
            for item in items
            if float(item["pending_nodes"]) >= pressure_floor
            or float(item["pending_jobs"]) >= 10
        ]
        sample = pressured if len(pressured) >= 24 else items
        estimate = quantile((float(item["running_nodes"]) for item in sample), q)
        partial_month = len(items) < 24 * 27
        if partial_month:
            confidence = "partial-window lower bound"
        elif len(pressured) >= 24:
            confidence = "high"
        else:
            confidence = "low-demand lower bound"
        result.append(
            {
                "month": month,
                "observed_capacity_nodes": round(estimate, 1),
                "fraction_of_global": round(estimate / global_capacity, 4),
                "bin_count": len(items),
                "pressured_bin_count": len(pressured),
                "confidence": confidence,
            }
        )
    return result


def bridge_candidates(candidate: List[bool], gap_bins: int) -> List[bool]:
    if gap_bins <= 0:
        return candidate
    bridged = candidate[:]
    previous = None
    for i, value in enumerate(candidate):
        if not value:
            continue
        if previous is not None and i - previous - 1 <= gap_bins:
            for j in range(previous + 1, i):
                bridged[j] = True
        previous = i
    return bridged


def load_backfill_evidence(path, bins):
    by_start = {int(item["start_epoch"]): item for item in bins}
    fields = (
        "direct_opportunity_jobs", "backfill_opportunity_jobs",
        "missed_opportunity_jobs", "missed_nonrecurring_jobs",
        "missed_nonrecurring_families", "missed_backfill_jobs",
        "missed_nonrecurring_backfill_jobs", "queue_scan_truncated",
        "missed_nonrecurring_direct_jobs",
        "missed_nonrecurring_direct_families",
        "missed_nonrecurring_backfill_families",
        "min_missed_nonrecurring_nodes",
        "min_missed_nonrecurring_backfill_nodes",
        "observed_running_nodes", "observed_free_nodes",
        "reclaiming_nodes", "unavailable_nodes",
        "size_controlled_direct_jobs", "size_controlled_backfill_jobs",
        "size_controlled_opportunity_jobs",
        "grace_seconds", "release_delay_seconds",
        "successful_size_control_enabled",
    )
    id_fields = (
        "backfill_opportunity_job_ids", "missed_backfill_job_ids",
        "missed_nonrecurring_backfill_job_ids",
    )
    matched = 0
    with open(path, newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if "start" not in (reader.fieldnames or []):
            raise ValueError("{}: backfill evidence has no start column".format(path))
        for row in reader:
            try:
                item = by_start.get(int(row["start"]))
            except (TypeError, ValueError):
                continue
            if item is None:
                continue
            for field in fields:
                try:
                    item[field] = int(row.get(field, 0) or 0)
                except ValueError:
                    item[field] = 0
            for field in id_fields:
                value = row.get(field)
                item[field] = set(value.split(";")) if value else set()
            item["has_distinct_backfill_job_ids"] = all(
                field in (reader.fieldnames or []) for field in id_fields
            )
            matched += 1
    return matched


def detect_periods(
    bins: Sequence[Dict[str, object]],
    capacity: float,
    width: int,
    min_duration_hours: float,
    min_pending_jobs: float,
    min_pending_fraction: float,
    shutdown_fraction: float,
    reduced_fraction: float,
    pause_start_fraction: float,
    bridge_bins_count: int,
    timestamps: TimestampFormatter,
    require_backfill_evidence=False,
    min_missed_jobs=3,
    min_missed_families=2,
    min_backfill_miss_fraction=0.5,
) -> List[Dict[str, object]]:
    demand_start_counts = [
        float(item["jobs_started"])
        for item in bins
        if (
            float(item["pending_jobs"]) >= min_pending_jobs
            or float(item["pending_nodes"]) >= capacity * min_pending_fraction
        )
        and float(item["jobs_started"]) > 0
        and float(item["running_nodes"]) >= capacity * reduced_fraction
    ]
    typical_starts = quantile(demand_start_counts, 0.5)
    if typical_starts == 0:
        typical_starts = quantile(
            (float(item["jobs_started"]) for item in bins if item["jobs_started"]), 0.5
        )

    raw = []  # type: List[bool]
    flags = []  # type: List[Tuple[bool, bool, bool]]
    for item in bins:
        running_fraction = float(item["running_nodes"]) / capacity
        demand = (
            float(item["pending_jobs"]) >= min_pending_jobs
            or float(item["pending_nodes"]) >= capacity * min_pending_fraction
        )
        general_opportunity_evidence = (
            not require_backfill_evidence
            or (
                int(item.get("missed_nonrecurring_jobs", 0)) >= min_missed_jobs
                and int(item.get("missed_nonrecurring_families", 0)) >= min_missed_families
            )
        )
        has_distinct_ids = bool(item.get("has_distinct_backfill_job_ids", False))
        if has_distinct_ids:
            opportunity_ids = set(item.get("backfill_opportunity_job_ids", set()))
            missed_ids = set(item.get("missed_backfill_job_ids", set()))
            nonrecurring_missed_ids = set(
                item.get("missed_nonrecurring_backfill_job_ids", set())
            )
            total_backfill_opportunities = len(opportunity_ids)
            total_missed_backfills = len(missed_ids)
            nonrecurring_missed_backfills = len(nonrecurring_missed_ids)
        else:
            opportunity_ids = set()
            missed_ids = set()
            nonrecurring_missed_ids = set()
            total_backfill_opportunities = int(
                item.get("backfill_opportunity_jobs", 0)
            )
            total_missed_backfills = int(item.get("missed_backfill_jobs", 0))
            nonrecurring_missed_backfills = int(
                item.get("missed_nonrecurring_backfill_jobs", 0)
            )
        # Remove known recurring/habitually-held misses from the denominator.
        # Successful starts by those families remain, making this ratio
        # conservative when family identity is uncertain.
        adjusted_eligible_backfills = max(
            0,
            total_backfill_opportunities
            - max(0, total_missed_backfills - nonrecurring_missed_backfills),
        )
        backfill_miss_fraction = (
            nonrecurring_missed_backfills / float(adjusted_eligible_backfills)
            if adjusted_eligible_backfills else 0.0
        )
        item["eligible_nonheld_backfill_jobs"] = adjusted_eligible_backfills
        if has_distinct_ids:
            item["eligible_nonheld_backfill_job_ids"] = opportunity_ids - (
                missed_ids - nonrecurring_missed_ids
            )
        item["backfill_miss_fraction"] = backfill_miss_fraction
        backfill_opportunity_evidence = (
            not require_backfill_evidence
            or (
                nonrecurring_missed_backfills >= min_missed_jobs
                and int(item.get("missed_nonrecurring_backfill_families", 0))
                >= min_missed_families
                # "Most" is strict: exactly half is not a majority.
                and backfill_miss_fraction > min_backfill_miss_fraction
            )
        )
        shutdown = (
            demand and general_opportunity_evidence
            and running_fraction <= shutdown_fraction and item["jobs_started"] == 0
        )
        reduced = (
            demand and backfill_opportunity_evidence
            and running_fraction <= reduced_fraction
        )
        paused = (
            demand and general_opportunity_evidence
            and running_fraction < 0.85
            and float(item["jobs_started"]) <= typical_starts * pause_start_fraction
        )
        flags.append((shutdown, paused, reduced))
        # Shutdowns and queue pauses use combined direct-start and backfill
        # evidence, while reduced-capacity and backfill-suppression bins use
        # the stricter backfill evidence.  Do not globally replace the
        # state-specific gates with the backfill-only gate when an evidence
        # file is present.
        raw.append(
            shutdown or paused or reduced
            or (require_backfill_evidence and backfill_opportunity_evidence)
        )

    candidate = bridge_candidates(raw, bridge_bins_count)
    minimum_bins = max(1, math.ceil(min_duration_hours * 3600 / width))
    periods = []  # type: List[Dict[str, object]]
    i = 0
    while i < len(bins):
        if not candidate[i]:
            i += 1
            continue
        j = i + 1
        while j < len(bins) and candidate[j]:
            j += 1
        if j - i < minimum_bins:
            i = j
            continue
        segment = bins[i:j]
        real_flags = flags[i:j]
        shutdown_share = sum(flag[0] for flag in real_flags) / len(real_flags)
        pause_share = sum(flag[1] for flag in real_flags) / len(real_flags)
        reduced_share = sum(flag[2] for flag in real_flags) / len(real_flags)
        avg_running = sum(float(item["running_nodes"]) for item in segment) / len(segment)
        avg_pending_jobs = sum(float(item["pending_jobs"]) for item in segment) / len(segment)
        avg_pending_nodes = sum(float(item["pending_nodes"]) for item in segment) / len(segment)
        avg_missed_jobs = sum(
            float(item.get("missed_nonrecurring_jobs", 0)) for item in segment
        ) / len(segment)
        peak_missed_jobs = max(
            int(item.get("missed_nonrecurring_jobs", 0)) for item in segment
        )
        avg_missed_families = sum(
            float(item.get("missed_nonrecurring_families", 0)) for item in segment
        ) / len(segment)
        use_distinct_jobs = all(
            bool(item.get("has_distinct_backfill_job_ids", False))
            for item in segment
        )
        if use_distinct_jobs:
            eligible_job_ids = set()
            missed_job_ids = set()
            for item in segment:
                eligible_job_ids.update(
                    item.get("eligible_nonheld_backfill_job_ids", set())
                )
                missed_job_ids.update(
                    item.get("missed_nonrecurring_backfill_job_ids", set())
                )
            eligible_backfills = len(eligible_job_ids)
            missed_backfills = len(missed_job_ids)
        else:
            eligible_backfills = sum(
                int(item.get("eligible_nonheld_backfill_jobs", 0)) for item in segment
            )
            missed_backfills = sum(
                int(item.get("missed_nonrecurring_backfill_jobs", 0)) for item in segment
            )
        period_backfill_miss_fraction = (
            missed_backfills / float(eligible_backfills) if eligible_backfills else 0.0
        )
        jobs_started = sum(int(item["jobs_started"]) for item in segment)
        capacity_fraction = avg_running / capacity
        if shutdown_share >= 0.6 and jobs_started == 0:
            state = "full_shutdown"
            evidence = "queued demand, almost no node allocation, and no job starts"
        elif pause_share >= 0.6:
            state = "queue_pause_or_maintenance"
            evidence = "queued demand and unusually few starts while limited work continued"
        elif reduced_share >= 0.6:
            state = "reduced_capacity"
            evidence = "sustained allocation collapse despite queued demand"
        else:
            state = "backfill_suppression"
            evidence = "multiple eligible backfill jobs were not started"
        # The aggregate strict-majority check protects inferences based on
        # backfill evidence.  Shutdown and queue-pause classifications instead
        # use the combined opportunity evidence already enforced per bin.
        if (
            require_backfill_evidence
            and state in ("reduced_capacity", "backfill_suppression")
            and period_backfill_miss_fraction <= min_backfill_miss_fraction
        ):
            i = j
            continue
        if require_backfill_evidence:
            evidence += "; multiple nonrecurring jobs missed reference EASY opportunities"
        observed_peak = max(
            float(
                item.get(
                    "instantaneous_running_nodes",
                    item.get("observed_running_nodes", item["running_nodes"]),
                )
            )
            for item in segment
        )
        lower_candidates = []
        for item in segment:
            missed_jobs = int(item.get("missed_nonrecurring_backfill_jobs", 0))
            missed_families = int(
                item.get("missed_nonrecurring_backfill_families", 0)
            )
            if (
                missed_jobs < min_missed_jobs
                or missed_families < min_missed_families
                or float(item.get("backfill_miss_fraction", 0))
                <= min_backfill_miss_fraction
            ):
                continue
            minimum_job_nodes = int(
                item.get("min_missed_nonrecurring_backfill_nodes", 0)
            )
            if minimum_job_nodes <= 0:
                continue
            occupancy = float(
                item.get(
                    "instantaneous_running_nodes",
                    item.get("observed_running_nodes", item["running_nodes"]),
                )
            )
            lower_candidates.append(occupancy + minimum_job_nodes - 1)
        effective_lower = max([observed_peak] + lower_candidates)
        bound_status = "evidence_lower_bound_only"
        duration_hours = len(segment) * width / 3600
        pressure = min(1.0, max(avg_pending_jobs / max(min_pending_jobs, 1), avg_pending_nodes / max(capacity * min_pending_fraction, 1)) / 5)
        severity = min(1.0, max(0.0, 1.0 - capacity_fraction))
        longevity = min(1.0, duration_hours / 24)
        if require_backfill_evidence:
            opportunity_strength = min(
                1.0,
                max(
                    avg_missed_jobs / max(min_missed_jobs * 5.0, 1),
                    avg_missed_families / max(min_missed_families * 5.0, 1),
                ),
            )
            confidence = min(
                0.99,
                0.25 + 0.15 * pressure + 0.2 * severity
                + 0.15 * longevity + 0.25 * opportunity_strength,
            )
        else:
            confidence = min(
                0.99, 0.4 + 0.2 * pressure + 0.25 * severity + 0.15 * longevity
            )
        periods.append(
            {
                "start": timestamps.value(int(segment[0]["start_epoch"])),
                "end": timestamps.value(int(segment[-1]["start_epoch"]) + width),
                "duration_hours": round(duration_hours, 2),
                "state": state,
                "maintenance_candidate": bool(require_backfill_evidence),
                "confidence": round(confidence, 3),
                "average_running_nodes": round(avg_running, 1),
                "peak_bin_running_nodes": round(max(float(x["running_nodes"]) for x in segment), 1),
                "fraction_of_normal_capacity": round(capacity_fraction, 4),
                "average_pending_jobs": round(avg_pending_jobs, 1),
                "average_pending_nodes": round(avg_pending_nodes, 1),
                "jobs_started": jobs_started,
                "average_missed_nonrecurring_jobs": round(avg_missed_jobs, 2),
                "peak_missed_nonrecurring_jobs": peak_missed_jobs,
                "average_missed_nonrecurring_families": round(avg_missed_families, 2),
                "eligible_nonheld_backfill_jobs": eligible_backfills,
                "missed_nonrecurring_backfill_jobs": missed_backfills,
                "backfill_miss_fraction": round(period_backfill_miss_fraction, 4),
                "backfill_evidence_count_basis": (
                    "distinct_jobs" if use_distinct_jobs else "hourly_observations"
                ),
                "potential_effective_capacity_lower_nodes": round(effective_lower, 1),
                "effective_capacity_bound_status": bound_status,
                "capacity_bound_evidence_bins": len(lower_candidates),
                "capacity_bound_conflicting_bins": 0,
                "evidence": evidence,
            }
        )
        i = j
    return periods


def write_bins(
    path: str,
    bins: Sequence[Dict[str, object]],
    width: int,
    capacity: float,
    timestamps: TimestampFormatter,
) -> None:
    fields = [
        "start", "end", "running_nodes", "capacity_fraction", "pending_jobs",
        "pending_nodes", "jobs_submitted", "jobs_started", "nodes_started",
        "direct_opportunity_jobs", "backfill_opportunity_jobs",
        "missed_opportunity_jobs", "missed_nonrecurring_jobs",
        "missed_nonrecurring_families", "missed_backfill_jobs",
        "missed_nonrecurring_backfill_jobs", "queue_scan_truncated",
        "missed_nonrecurring_direct_jobs",
        "missed_nonrecurring_direct_families",
        "missed_nonrecurring_backfill_families",
        "min_missed_nonrecurring_nodes",
        "min_missed_nonrecurring_backfill_nodes",
        "instantaneous_running_nodes",
        "observed_running_nodes", "observed_free_nodes",
        "reclaiming_nodes", "unavailable_nodes",
        "size_controlled_direct_jobs", "size_controlled_backfill_jobs",
        "size_controlled_opportunity_jobs",
        "eligible_nonheld_backfill_jobs", "backfill_miss_fraction",
    ]
    with open(path, "w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for item in bins:
            epoch = int(item["start_epoch"])
            writer.writerow(
                {
                    "start": timestamps.value(epoch),
                    "end": timestamps.value(epoch + width),
                    "running_nodes": round(float(item["running_nodes"]), 3),
                    "capacity_fraction": round(float(item["running_nodes"]) / capacity, 6),
                    "pending_jobs": round(float(item["pending_jobs"]), 3),
                    "pending_nodes": round(float(item["pending_nodes"]), 3),
                    "jobs_submitted": item["jobs_submitted"],
                    "jobs_started": item["jobs_started"],
                    "nodes_started": item["nodes_started"],
                    "instantaneous_running_nodes": round(
                        float(item.get("instantaneous_running_nodes", 0)), 3
                    ),
                    "direct_opportunity_jobs": item.get("direct_opportunity_jobs", 0),
                    "backfill_opportunity_jobs": item.get("backfill_opportunity_jobs", 0),
                    "missed_opportunity_jobs": item.get("missed_opportunity_jobs", 0),
                    "missed_nonrecurring_jobs": item.get("missed_nonrecurring_jobs", 0),
                    "missed_nonrecurring_families": item.get("missed_nonrecurring_families", 0),
                    "missed_backfill_jobs": item.get("missed_backfill_jobs", 0),
                    "missed_nonrecurring_backfill_jobs": item.get(
                        "missed_nonrecurring_backfill_jobs", 0
                    ),
                    "missed_nonrecurring_direct_jobs": item.get(
                        "missed_nonrecurring_direct_jobs", 0
                    ),
                    "missed_nonrecurring_direct_families": item.get(
                        "missed_nonrecurring_direct_families", 0
                    ),
                    "missed_nonrecurring_backfill_families": item.get(
                        "missed_nonrecurring_backfill_families", 0
                    ),
                    "min_missed_nonrecurring_nodes": item.get(
                        "min_missed_nonrecurring_nodes", 0
                    ),
                    "min_missed_nonrecurring_backfill_nodes": item.get(
                        "min_missed_nonrecurring_backfill_nodes", 0
                    ),
                    "observed_running_nodes": item.get("observed_running_nodes", 0),
                    "observed_free_nodes": item.get("observed_free_nodes", 0),
                    "reclaiming_nodes": item.get("reclaiming_nodes", 0),
                    "unavailable_nodes": item.get("unavailable_nodes", 0),
                    "size_controlled_direct_jobs": item.get(
                        "size_controlled_direct_jobs", 0
                    ),
                    "size_controlled_backfill_jobs": item.get(
                        "size_controlled_backfill_jobs", 0
                    ),
                    "size_controlled_opportunity_jobs": item.get(
                        "size_controlled_opportunity_jobs", 0
                    ),
                    "eligible_nonheld_backfill_jobs": item.get(
                        "eligible_nonheld_backfill_jobs", 0
                    ),
                    "backfill_miss_fraction": round(
                        float(item.get("backfill_miss_fraction", 0)), 6
                    ),
                    "queue_scan_truncated": item.get("queue_scan_truncated", 0),
                }
            )


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Infer downtime, queue pauses, and normal node capacity from job traces."
    )
    parser.add_argument("inputs", nargs="+", help="CSV paths or glob patterns")
    parser.add_argument("-o", "--output", help="JSON report (default: stdout)")
    parser.add_argument("--bins-output", help="optional CSV containing the reconstructed timeline")
    output_filter = parser.add_mutually_exclusive_group()
    output_filter.add_argument(
        "--only-queue-pause", dest="only_queue_pause", action="store_true",
        default=True,
        help=(
            "write only queue_pause_or_maintenance periods (default); the "
            "optional timeline CSV remains complete"
        ),
    )
    output_filter.add_argument(
        "--all-states", dest="only_queue_pause", action="store_false",
        help="write every detected period state to the JSON report",
    )
    parser.add_argument(
        "--backfill-evidence",
        help="CSV from backfill_opportunity_audit.py; requires multi-job evidence",
    )
    parser.add_argument(
        "--min-missed-jobs", type=int, default=3,
        help="minimum nonrecurring missed opportunities per bin (default: 3)",
    )
    parser.add_argument(
        "--min-missed-families", type=int, default=2,
        help="minimum distinct nonrecurring families per bin (default: 2)",
    )
    parser.add_argument(
        "--min-backfill-miss-fraction", type=float, default=0.50,
        help=(
            "maintenance requires a miss fraction greater than this value "
            "(default: 0.50, i.e. a strict majority)"
        ),
    )
    parser.add_argument(
        "--time-format", choices=("epoch", "iso"), default="epoch",
        help="timestamp representation (default: epoch)",
    )
    parser.add_argument(
        "--timezone", default="UTC",
        help="timezone for ISO timestamps/month boundaries, e.g. PST, JST, or Asia/Tokyo (default: UTC)",
    )
    parser.add_argument(
        "--overlap-policy", choices=("combine", "filename-month"), default="combine",
        help="combine all jobs, or stitch each YY_MM trace to its filename month (default: combine)",
    )
    parser.add_argument("--bin-minutes", type=int, default=60, help="timeline resolution (default: 60)")
    parser.add_argument("--analysis-start", type=int,
                        help="inclusive epoch-second analysis boundary")
    parser.add_argument("--analysis-end", type=int,
                        help="exclusive epoch-second analysis boundary")
    parser.add_argument("--known-capacity", type=int, help="normal node capacity; skips inference")
    parser.add_argument("--capacity-quantile", type=float, default=0.995, help="capacity quantile (default: 0.995)")
    parser.add_argument("--min-duration-hours", type=float, default=1.0, help="shortest reported anomaly (default: 1)")
    parser.add_argument("--min-pending-jobs", type=float, default=10.0, help="demand threshold (default: 10)")
    parser.add_argument("--min-pending-fraction", type=float, default=0.02, help="pending-node demand as capacity fraction (default: 0.02)")
    parser.add_argument("--shutdown-fraction", type=float, default=0.005, help="allocation fraction considered shut down (default: 0.005)")
    parser.add_argument("--reduced-fraction", type=float, default=0.60, help="allocation fraction considered reduced (default: 0.60)")
    parser.add_argument("--pause-start-fraction", type=float, default=0.10, help="start rate fraction considered paused (default: 0.10)")
    parser.add_argument("--bridge-bins", type=int, default=1, help="merge anomalies over this many normal bins (default: 1)")
    args = parser.parse_args(argv)
    if args.bin_minutes <= 0 or args.min_duration_hours <= 0:
        parser.error("bin size and minimum duration must be positive")
    if args.known_capacity is not None and args.known_capacity <= 0:
        parser.error("--known-capacity must be positive")
    if (
        args.min_pending_jobs < 0 or args.bridge_bins < 0
        or args.min_missed_jobs < 1 or args.min_missed_families < 1
    ):
        parser.error("pending-job and bridge-bin thresholds cannot be negative")
    if (args.analysis_start is None) != (args.analysis_end is None):
        parser.error("--analysis-start and --analysis-end must be used together")
    if args.analysis_start is not None and args.analysis_end <= args.analysis_start:
        parser.error("--analysis-end must be later than --analysis-start")
    for name in (
        "capacity_quantile", "min_pending_fraction", "shutdown_fraction",
        "reduced_fraction", "pause_start_fraction", "min_backfill_miss_fraction",
    ):
        value = getattr(args, name)
        if not 0 <= value <= 1:
            parser.error(f"--{name.replace('_', '-')} must be between 0 and 1")
    return args


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        timestamps = TimestampFormatter(args.time_format, args.timezone)
        paths = expand_paths(args.inputs)
        width = args.bin_minutes * 60
        builder = TimelineBuilder(width)
        analysis_window = ((args.analysis_start, args.analysis_end)
                           if args.analysis_start is not None else None)
        stats = read_traces(paths, builder, args.overlap_policy, timestamps,
                            analysis_window)
        bins = build_bins(builder, stats)
        evidence_bins = 0
        evidence_parameters = {}
        if args.backfill_evidence:
            evidence_bins = load_backfill_evidence(args.backfill_evidence, bins)
            evidence_row = next(
                (item for item in bins if "successful_size_control_enabled" in item),
                None,
            )
            if evidence_row is not None:
                evidence_parameters = {
                    "grace_seconds": evidence_row.get("grace_seconds", 0),
                    "release_delay_seconds": evidence_row.get(
                        "release_delay_seconds", 0
                    ),
                    "successful_size_control_enabled": bool(
                        evidence_row.get("successful_size_control_enabled", 0)
                    ),
                    "period_count_basis": (
                        "distinct_jobs"
                        if evidence_row.get("has_distinct_backfill_job_ids", False)
                        else "hourly_observations"
                    ),
                }
        capacity, capacity_basis = infer_capacity(bins, args.capacity_quantile, args.known_capacity)
        periods = detect_periods(
            bins, capacity, width, args.min_duration_hours,
            args.min_pending_jobs, args.min_pending_fraction,
            args.shutdown_fraction, args.reduced_fraction,
            args.pause_start_fraction, args.bridge_bins,
            timestamps,
            bool(args.backfill_evidence), args.min_missed_jobs,
            args.min_missed_families, args.min_backfill_miss_fraction,
        )
        if args.only_queue_pause:
            periods = [
                period for period in periods
                if period["state"] == "queue_pause_or_maintenance"
            ]
        report = {
            "schema_version": 1,
            "analysis_interval": {
                "start": timestamps.value((stats.analysis_start // width) * width),
                "end": timestamps.value(((stats.analysis_end - 1) // width + 1) * width),
                "bin_minutes": args.bin_minutes,
                "time_format": args.time_format,
                "timezone": args.timezone,
                "overlap_policy": args.overlap_policy,
            },
            "input": {
                "files": paths,
                "rows": stats.rows,
                "invalid_rows": stats.invalid_rows,
                "invalid_intervals": stats.invalid_intervals,
                "backfill_evidence": (
                    None if not args.backfill_evidence else {
                        "path": os.path.abspath(args.backfill_evidence),
                        "matched_bins": evidence_bins,
                        "parameters": evidence_parameters,
                    }
                ),
            },
            "normal_capacity": {
                "inferred_nodes": round(capacity, 1),
                "basis": capacity_basis,
                "interpretation": (
                    "user-supplied total system capacity"
                    if args.known_capacity is not None
                    else "observed schedulable capacity; a lower bound when demand is insufficient"
                ),
            },
            "detection_parameters": {
                "minimum_duration_hours": args.min_duration_hours,
                "minimum_pending_jobs": args.min_pending_jobs,
                "minimum_pending_node_fraction": args.min_pending_fraction,
                "shutdown_capacity_fraction": args.shutdown_fraction,
                "reduced_capacity_fraction": args.reduced_fraction,
                "pause_start_rate_fraction": args.pause_start_fraction,
                "bridged_normal_bins": args.bridge_bins,
                "requires_backfill_evidence": bool(args.backfill_evidence),
                "minimum_missed_nonrecurring_jobs": args.min_missed_jobs,
                "minimum_missed_nonrecurring_families": args.min_missed_families,
                "minimum_backfill_miss_fraction": args.min_backfill_miss_fraction,
                "output_state_filter": (
                    "queue_pause_or_maintenance" if args.only_queue_pause else None
                ),
            },
            "capacity_by_month": capacity_windows(
                bins, args.capacity_quantile, capacity, timestamps
            ),
            "periods": periods,
            "limitations": [
                "The input schema has no queue/partition field, so queue_pause_or_maintenance is behavioral evidence, not a proven queue identity.",
                "Pending demand is reconstructed from jobs present in the trace; canceled or omitted jobs are invisible.",
                "Node allocation measures reserved resources, not CPU utilization or physical node health.",
                "Backfill replay cannot observe queue eligibility, explicit holds, dependencies, reservations, or node topology.",
                "Recurring and habitually missed families are inferred from resource, time-limit, power, and submission patterns.",
            ],
        }
        rendered = json.dumps(report, indent=2) + "\n"
        if args.output:
            Path(args.output).write_text(rendered, encoding="utf-8")
        else:
            sys.stdout.write(rendered)
        if args.bins_output:
            write_bins(args.bins_output, bins, width, capacity, timestamps)
    except (OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
