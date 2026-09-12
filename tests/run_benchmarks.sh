#!/bin/bash
# Reproduces the benchmark sweep. Results are appended to benchmark_results/results.csv.
# Usage: tests/run_benchmarks.sh   (from the repository root, after `make benchmark`)
set -euo pipefail

OUT=benchmark_results
mkdir -p "$OUT"
CSV="$OUT/results.csv"
BIN=./benchmark
LOGDIR="$OUT/logs"
mkdir -p "$LOGDIR"

run() {
    echo
    echo "=== $*"
    (cd "$LOGDIR" && ../../"$BIN" "$@" --csv ../results.csv) | grep -v "Resource Allocation Graph initialized"
}

# A. CPU-only transactions (no work per operation), low contention
run --label cpu_only_low --txns 2000 --ops 4 --items 1000 --write-ratio 0.2 --work-us 0 \
    --threads 1,2,4,8,16,32 --runs 5

# B. 1 ms of simulated I/O per operation, low contention
run --label io1ms_low --txns 1000 --ops 4 --items 1000 --write-ratio 0.2 --work-us 1000 \
    --threads 1,2,4,8,16,32,64,1000 --runs 5

# C. 1 ms of simulated I/O per operation, high contention (20 items, 30% writes)
run --label io1ms_high --txns 1000 --ops 4 --items 20 --write-ratio 0.3 --work-us 1000 \
    --threads 1,2,4,8,16,32,64,1000 --runs 5
