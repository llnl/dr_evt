sim_setup {
  seed: 42
  infile: "test_traces/backfill_test.csv"
  outfile: "test_protobuf_full.out"
  verbose: true

  # Scheduling parameters
  total_nodes: 100
  backfill_policy: "easy"
  priority_policy: "fcfs"
  runtime_mode: "limit"

  # Trace format
  trace_format: "simple"
  timestamp_format: "epoch"

  # Duration (exact mode)
  duration_mode: "exact"
}
