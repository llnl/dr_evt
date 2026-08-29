sim_setup {
  seed: 42
  infile: "test_traces/backfill_test.csv"
  outfile: "test_protobuf_conservative.out"

  # Conservative backfilling with shortest-job-first
  total_nodes: 100
  backfill_policy: "conservative"
  priority_policy: "sjf"
  runtime_mode: "limit"

  # Trace format
  trace_format: "simple"
  timestamp_format: "epoch"

  # Exact durations
  duration_mode: "exact"
}
