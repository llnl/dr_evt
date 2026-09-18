#!/usr/bin/env bash
#SBATCH --job-name=ep-FY2021-H2-n128-t1h
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --exclusive
#SBATCH --time=06:00:00
#SBATCH --output=/p/vast1/f-data/dr_evt/experimental/fugaku-power/individual-fiscal-jobs/logs/%x-%j.out
#SBATCH --error=/p/vast1/f-data/dr_evt/experimental/fugaku-power/individual-fiscal-jobs/logs/%x-%j.err

set -euo pipefail
exec '/p/vast1/f-data/dr_evt/experimental/fugaku-power/individual-fiscal-jobs/run_one.sh' 'easypower' 'FY2021-H2' '128' '3600' '3600s'
