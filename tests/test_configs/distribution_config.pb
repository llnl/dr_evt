sim_setup {
  seed: 42
  infile: "test_traces/backfill_test.csv"
  outfile: "test_protobuf_dist.out"

  # System configuration
  total_nodes: 100
  backfill_policy: "easy"
  priority_policy: "fcfs"

  # Trace format
  trace_format: "simple"
  timestamp_format: "epoch"

  # Duration with statistical sampling
  duration_mode: "distribution"
  duration_distribution: "normal"
  duration_scale: 0.8        # Jobs run 80% of time_limit on average
  duration_stddev: 0.1       # 10% standard deviation
}
