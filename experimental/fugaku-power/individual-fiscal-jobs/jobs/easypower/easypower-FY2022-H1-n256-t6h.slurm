#!/usr/bin/env bash
#SBATCH --job-name=ep-FY2022-H1-n256-t6h
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --exclusive
#SBATCH --time=06:00:00
#SBATCH --output=/p/vast1/f-data/dr_evt/experimental/fugaku-power/individual-fiscal-jobs/logs/%x-%j.out
#SBATCH --error=/p/vast1/f-data/dr_evt/experimental/fugaku-power/individual-fiscal-jobs/logs/%x-%j.err

set -euo pipefail
exec '/p/vast1/f-data/dr_evt/experimental/fugaku-power/individual-fiscal-jobs/run_one.sh' 'easypower' 'FY2022-H1' '256' '21600' '21600s'
