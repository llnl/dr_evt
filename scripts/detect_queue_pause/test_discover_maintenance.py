import os
import tempfile
import unittest

from discover_maintenance import (
    TimelineBuilder,
    TimestampFormatter,
    TraceStats,
    build_bins,
    detect_periods,
    filename_month_window,
    parse_args,
    read_traces,
)


class DetectorTests(unittest.TestCase):
    def test_only_queue_pause_option(self):
        self.assertTrue(parse_args(["trace.csv"]).only_queue_pause)
        self.assertTrue(
            parse_args(["trace.csv", "--only-queue-pause"]).only_queue_pause
        )
        self.assertFalse(parse_args(["trace.csv", "--all-states"]).only_queue_pause)

    def test_interval_accounting_across_bins(self):
        builder = TimelineBuilder(3600)
        builder.add_job(0, 1800, 5400, 10)
        bins = build_bins(builder, TraceStats(rows=1, min_submit=0, max_submit=7200))
        self.assertEqual([x["running_nodes"] for x in bins], [5, 5, 0])
        self.assertEqual(
            [x["instantaneous_running_nodes"] for x in bins], [0, 10, 0]
        )
        self.assertEqual(bins[0]["pending_jobs"], 0.5)

    def test_period_majority_includes_bridged_hour(self):
        bins = []
        for hour, opportunities, misses in ((0, 4, 3), (1, 100, 0), (2, 4, 3)):
            bins.append(
                {
                    "start_epoch": hour * 3600,
                    "running_nodes": 20,
                    "pending_jobs": 20,
                    "pending_nodes": 200,
                    "jobs_started": 5,
                    "backfill_opportunity_jobs": opportunities,
                    "missed_backfill_jobs": misses,
                    "missed_nonrecurring_backfill_jobs": misses,
                    "missed_nonrecurring_backfill_families": 2 if misses else 0,
                    "missed_nonrecurring_jobs": misses,
                    "missed_nonrecurring_families": 2 if misses else 0,
                }
            )
        periods = detect_periods(
            bins, 100, 3600, 1, 10, 0.02, 0.005, 0.6, 0.1, 1,
            TimestampFormatter("epoch", "UTC"), True, 3, 2, 0.5,
        )
        self.assertEqual(periods, [])

    def test_detects_backlogged_shutdown(self):
        bins = []
        for hour in range(24):
            shutdown = 8 <= hour < 16
            bins.append(
                {
                    "start_epoch": hour * 3600,
                    "running_nodes": 0 if shutdown else 100,
                    "pending_jobs": 50 if shutdown else 0,
                    "pending_nodes": 500 if shutdown else 0,
                    "jobs_started": 0 if shutdown else 20,
                }
            )
        periods = detect_periods(
            bins, 100, 3600, 6, 10, 0.02, 0.005, 0.6, 0.1, 0,
            TimestampFormatter("epoch", "UTC"),
        )
        self.assertEqual(len(periods), 1)
        self.assertEqual(periods[0]["state"], "full_shutdown")
        self.assertEqual(periods[0]["duration_hours"], 8)

    def test_queue_pause_uses_combined_evidence_when_backfill_is_enabled(self):
        item = {
            "start_epoch": 0,
            "running_nodes": 20,
            "pending_jobs": 20,
            "pending_nodes": 200,
            "jobs_started": 0,
            "missed_nonrecurring_jobs": 3,
            "missed_nonrecurring_families": 2,
            "backfill_opportunity_jobs": 0,
            "missed_backfill_jobs": 0,
            "missed_nonrecurring_backfill_jobs": 0,
            "missed_nonrecurring_backfill_families": 0,
        }
        periods = detect_periods(
            [item], 100, 3600, 1, 10, 0.02, 0.005, 0.6, 0.1, 0,
            TimestampFormatter("epoch", "UTC"), True, 3, 2, 0.5,
        )
        self.assertEqual(len(periods), 1)
        self.assertEqual(periods[0]["state"], "queue_pause_or_maintenance")
        self.assertEqual(periods[0]["backfill_miss_fraction"], 0.0)

    def test_shutdown_uses_combined_evidence_when_backfill_is_enabled(self):
        item = {
            "start_epoch": 0,
            "running_nodes": 0,
            "pending_jobs": 20,
            "pending_nodes": 200,
            "jobs_started": 0,
            "missed_nonrecurring_jobs": 3,
            "missed_nonrecurring_families": 2,
            "backfill_opportunity_jobs": 0,
            "missed_backfill_jobs": 0,
            "missed_nonrecurring_backfill_jobs": 0,
            "missed_nonrecurring_backfill_families": 0,
        }
        periods = detect_periods(
            [item], 100, 3600, 1, 10, 0.02, 0.005, 0.6, 0.1, 0,
            TimestampFormatter("epoch", "UTC"), True, 3, 2, 0.5,
        )
        self.assertEqual(len(periods), 1)
        self.assertEqual(periods[0]["state"], "full_shutdown")
        self.assertEqual(periods[0]["backfill_miss_fraction"], 0.0)

    def test_idle_without_backlog_is_not_maintenance(self):
        bins = [
            {
                "start_epoch": hour * 3600,
                "running_nodes": 0,
                "pending_jobs": 0,
                "pending_nodes": 0,
                "jobs_started": 0,
            }
            for hour in range(12)
        ]
        periods = detect_periods(
            bins, 100, 3600, 6, 10, 0.02, 0.005, 0.6, 0.1, 1,
            TimestampFormatter("epoch", "UTC"),
        )
        self.assertEqual(periods, [])

    def test_reduced_capacity_requires_multi_family_backfill_evidence(self):
        item = {
            "start_epoch": 0,
            "running_nodes": 20,
            "pending_jobs": 20,
            "pending_nodes": 200,
            "jobs_started": 5,
            "missed_nonrecurring_backfill_jobs": 2,
            "missed_nonrecurring_backfill_families": 2,
            "missed_nonrecurring_jobs": 2,
            "missed_nonrecurring_families": 2,
            "backfill_opportunity_jobs": 3,
            "missed_backfill_jobs": 2,
        }
        arguments = (
            [item], 100, 3600, 1, 10, 0.02, 0.005, 0.6, 0.1, 0,
            TimestampFormatter("epoch", "UTC"), True, 3, 2,
        )
        self.assertEqual(detect_periods(*arguments), [])
        item["missed_nonrecurring_backfill_jobs"] = 3
        item["missed_backfill_jobs"] = 3
        periods = detect_periods(*arguments)
        self.assertEqual(len(periods), 1)
        self.assertEqual(periods[0]["state"], "reduced_capacity")

    def test_backfill_evidence_is_maintenance_above_reduced_threshold(self):
        item = {
            "start_epoch": 0,
            "running_nodes": 80,
            "pending_jobs": 20,
            "pending_nodes": 200,
            "jobs_started": 5,
            "missed_nonrecurring_backfill_jobs": 3,
            "missed_nonrecurring_backfill_families": 2,
            "missed_nonrecurring_jobs": 3,
            "missed_nonrecurring_families": 2,
            "backfill_opportunity_jobs": 5,
            "missed_backfill_jobs": 3,
            "min_missed_nonrecurring_backfill_nodes": 2,
            "observed_running_nodes": 80,
            "instantaneous_running_nodes": 90,
        }
        periods = detect_periods(
            [item], 100, 3600, 1, 10, 0.02, 0.005, 0.6, 0.1, 0,
            TimestampFormatter("epoch", "UTC"), True, 3, 2,
        )
        self.assertEqual(len(periods), 1)
        self.assertEqual(periods[0]["state"], "backfill_suppression")
        self.assertEqual(periods[0]["potential_effective_capacity_lower_nodes"], 91)
        self.assertNotIn("potential_effective_capacity_upper_nodes", periods[0])

    def test_capacity_lower_bound_is_maximum_missed_fit_threshold(self):
        bins = []
        for hour, occupancy, smallest_job in ((0, 20, 10), (1, 30, 2)):
            bins.append(
                {
                    "start_epoch": hour * 3600,
                    "running_nodes": occupancy,
                    "instantaneous_running_nodes": occupancy,
                    "pending_jobs": 20,
                    "pending_nodes": 200,
                    "jobs_started": 5,
                    "backfill_opportunity_jobs": 5,
                    "missed_backfill_jobs": 3,
                    "missed_nonrecurring_backfill_jobs": 3,
                    "missed_nonrecurring_backfill_families": 2,
                    "missed_nonrecurring_jobs": 3,
                    "missed_nonrecurring_families": 2,
                    "min_missed_nonrecurring_backfill_nodes": smallest_job,
                }
            )
        periods = detect_periods(
            bins, 100, 3600, 1, 10, 0.02, 0.005, 0.6, 0.1, 0,
            TimestampFormatter("epoch", "UTC"), True, 3, 2, 0.5,
        )
        self.assertEqual(len(periods), 1)
        self.assertEqual(periods[0]["potential_effective_capacity_lower_nodes"], 31)
        self.assertNotIn("potential_effective_capacity_upper_nodes", periods[0])

    def test_backfill_minimum_counts_without_majority_are_not_maintenance(self):
        item = {
            "start_epoch": 0,
            "running_nodes": 20,
            "pending_jobs": 20,
            "pending_nodes": 200,
            "jobs_started": 5,
            "backfill_opportunity_jobs": 10,
            "missed_backfill_jobs": 3,
            "missed_nonrecurring_backfill_jobs": 3,
            "missed_nonrecurring_backfill_families": 2,
            "missed_nonrecurring_jobs": 3,
            "missed_nonrecurring_families": 2,
        }
        periods = detect_periods(
            [item], 100, 3600, 1, 10, 0.02, 0.005, 0.6, 0.1, 0,
            TimestampFormatter("epoch", "UTC"), True, 3, 2, 0.5,
        )
        self.assertEqual(periods, [])
        self.assertEqual(item["eligible_nonheld_backfill_jobs"], 10)
        self.assertEqual(item["backfill_miss_fraction"], 0.3)

    def test_strict_backfill_majority_is_maintenance(self):
        item = {
            "start_epoch": 0,
            "running_nodes": 20,
            "pending_jobs": 20,
            "pending_nodes": 200,
            "jobs_started": 5,
            "backfill_opportunity_jobs": 10,
            "missed_backfill_jobs": 6,
            "missed_nonrecurring_backfill_jobs": 6,
            "missed_nonrecurring_backfill_families": 2,
            "missed_nonrecurring_jobs": 6,
            "missed_nonrecurring_families": 2,
        }
        periods = detect_periods(
            [item], 100, 3600, 1, 10, 0.02, 0.005, 0.6, 0.1, 0,
            TimestampFormatter("epoch", "UTC"), True, 3, 2, 0.5,
        )
        self.assertEqual(len(periods), 1)
        self.assertEqual(periods[0]["backfill_miss_fraction"], 0.6)

    def test_exactly_half_of_backfills_is_not_a_majority(self):
        item = {
            "start_epoch": 0,
            "running_nodes": 20,
            "pending_jobs": 20,
            "pending_nodes": 200,
            "jobs_started": 5,
            "backfill_opportunity_jobs": 6,
            "missed_backfill_jobs": 3,
            "missed_nonrecurring_backfill_jobs": 3,
            "missed_nonrecurring_backfill_families": 2,
            "missed_nonrecurring_jobs": 3,
            "missed_nonrecurring_families": 2,
        }
        periods = detect_periods(
            [item], 100, 3600, 1, 10, 0.02, 0.005, 0.6, 0.1, 0,
            TimestampFormatter("epoch", "UTC"), True, 3, 2, 0.5,
        )
        self.assertEqual(periods, [])

    def test_period_majority_counts_distinct_jobs_not_hourly_duplicates(self):
        bins = []
        for hour, successful_ids in enumerate((("d", "e"), ("f", "g"))):
            opportunity_ids = {"a", "b", "c"} | set(successful_ids)
            bins.append(
                {
                    "start_epoch": hour * 3600,
                    "running_nodes": 20,
                    "pending_jobs": 20,
                    "pending_nodes": 200,
                    "jobs_started": 5,
                    "backfill_opportunity_jobs": 5,
                    "missed_backfill_jobs": 3,
                    "missed_nonrecurring_backfill_jobs": 3,
                    "missed_nonrecurring_backfill_families": 2,
                    "missed_nonrecurring_jobs": 3,
                    "missed_nonrecurring_families": 2,
                    "has_distinct_backfill_job_ids": True,
                    "backfill_opportunity_job_ids": opportunity_ids,
                    "missed_backfill_job_ids": {"a", "b", "c"},
                    "missed_nonrecurring_backfill_job_ids": {"a", "b", "c"},
                }
            )
        periods = detect_periods(
            bins, 100, 3600, 1, 10, 0.02, 0.005, 0.6, 0.1, 0,
            TimestampFormatter("epoch", "UTC"), True, 3, 2, 0.5,
        )
        # Each hour is 3/5, but the merged period is only 3/7 distinct jobs.
        self.assertEqual(periods, [])

    def test_timestamp_formats_and_fixed_timezones(self):
        self.assertEqual(TimestampFormatter("epoch", "JST").value(0), 0)
        self.assertEqual(
            TimestampFormatter("iso", "JST").value(0),
            "1970-01-01T09:00:00+09:00",
        )
        self.assertEqual(
            TimestampFormatter("iso", "PST").value(0),
            "1969-12-31T16:00:00-08:00",
        )

    def test_filename_month_uses_selected_timezone(self):
        start, end = filename_month_window(
            "23_08_scheduling_trace.csv", TimestampFormatter("epoch", "JST")
        )
        self.assertEqual(start, 1690815600)
        self.assertEqual(end, 1693494000)

    def test_rows_repeated_across_files_are_counted_once(self):
        header = "job_submit_time,begin_time,end_time,num_nodes\n"
        crossing = "0,0,7200,10\n"
        with tempfile.TemporaryDirectory() as temporary:
            first = os.path.join(temporary, "a.csv")
            second = os.path.join(temporary, "b.csv")
            with open(first, "w") as stream:
                stream.write(header + crossing + "0,0,3600,5\n")
            with open(second, "w") as stream:
                stream.write(header + crossing + "3600,3600,7200,2\n")
            builder = TimelineBuilder(3600)
            stats = read_traces([first, second], builder)
        self.assertEqual(stats.rows, 4)
        self.assertEqual(stats.duplicate_rows, 1)
        self.assertEqual(sum(builder.start_nodes.values()), 17)

    def test_identical_rows_within_one_file_are_distinct_jobs(self):
        header = "job_submit_time,begin_time,end_time,num_nodes\n"
        with tempfile.TemporaryDirectory() as temporary:
            path = os.path.join(temporary, "a.csv")
            with open(path, "w") as stream:
                stream.write(header + "0,0,3600,4\n" * 2)
            builder = TimelineBuilder(3600)
            stats = read_traces([path], builder)
        self.assertEqual(stats.rows, 2)
        self.assertEqual(stats.duplicate_rows, 0)
        self.assertEqual(sum(builder.start_nodes.values()), 8)


if __name__ == "__main__":
    unittest.main()
