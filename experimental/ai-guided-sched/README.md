# AI-guided scheduling prototype

This standalone research prototype uses pandas and NumPy to compare
wall-time, predicted-runtime, and configuration-aware scheduling. Run
`HPDC_scheduling_script.py` from this directory; it reads the CSV fixtures
in `Data/` and writes results under `Results/`.

It does not use the native DR_EVT scheduler or the shared Python EASY
reference. Its backfill selection uses a time-window cutoff, without the
current core's resource-time reservation checks. It has no automated
regression suite establishing equivalence with current DR_EVT scheduling.
The November 2026 compatibility audit leaves its research policies intact;
use the tested schedulers in `../multi-cluster-bf` or the shared Python
reference when current EASY behavior is required.
