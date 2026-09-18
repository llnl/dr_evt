#!/usr/bin/env bash
#SBATCH --job-name=cap-FY2022-H2
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --exclusive
#SBATCH --time=06:00:00
#SBATCH --output=/p/vast1/f-data/dr_evt/experimental/fugaku-power/individual-fiscal-jobs/logs/%x-%j.out
#SBATCH --error=/p/vast1/f-data/dr_evt/experimental/fugaku-power/individual-fiscal-jobs/logs/%x-%j.err

set -euo pipefail
exec '/p/vast1/f-data/dr_evt/experimental/fugaku-power/individual-fiscal-jobs/run_one.sh' 'capacity' 'FY2022-H2'
