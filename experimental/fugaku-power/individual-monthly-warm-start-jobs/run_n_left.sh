  n=12
  submitted=0
  while IFS=$'\t' read -r kind month window time_window script prerequisite; do
      [[ $kind == easypower ]] || continue

      case "$time_window" in
          unlimited) slug=unlimited ;;
          1h)        slug=3600s ;;
          6h)        slug=21600s ;;
      esac

      result="../results/individual-monthly-warm-start-sweep/$month/job-window-$window/$month/easypower_n${window}_t${slug}"

      if [[ ! -f "$result/.complete" ||
            ! -s "$result/jobs.csv" ||
            ! -s "$result/resources.csv" ||
            ! -s "$result/target_horizon.csv" ]]; then
          sbatch "$script"
          ((submitted += 1))
      fi
      if (( submitted >= $n )); then
        break
      fi
  done < manifest.tsv

  echo "Submitted $submitted jobs"
