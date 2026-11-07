#!/usr/bin/env bash
set -euo pipefail

validation_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "$validation_dir/../.." && pwd)
sources=${FLUX_SOURCES:-"$repo_dir/external"}
results=${FLUX_VALIDATION_RESULTS:-"$validation_dir/results"}
image=${FLUX_VALIDATION_IMAGE:-dr-evt-flux-validation}
build_volume=${FLUX_VALIDATION_BUILD_VOLUME:-dr-evt-flux-validation-build}

for name in flux-core flux-sched flux-fiction; do
    test -d "$sources/$name/.git" || {
        echo "Missing checkout: $sources/$name" >&2
        exit 1
    }
done
mkdir -p "$results"
docker build -t "$image" "$validation_dir"
docker run --rm \
    --mount "type=bind,src=$sources,dst=/sources,readonly" \
    --mount "type=bind,src=$results,dst=/results" \
    --mount "type=volume,src=$build_volume,dst=/workspace" \
    -e "BUILD_JOBS=${BUILD_JOBS:-4}" "$image"
