#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-"$repo_dir/build-resample-bench"}
result_prefix=${2:-"$repo_dir/benchmarks/results/resample_latest"}

cmake -S "$repo_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release -DCXPR_BUILD_BENCHMARKS=ON
cmake --build "$build_dir" --target cxpr_bench_resample -j2

mkdir -p "$(dirname -- "$result_prefix")"
raw_file="$result_prefix.txt"
trial_file="$result_prefix.trials.txt"
summary_file="$result_prefix.summary.txt"
: > "$trial_file"
for trial in 1 2 3 4 5 6 7 8 9; do
    echo "trial=$trial" >> "$trial_file"
    "$build_dir/benchmarks/cxpr_bench_resample" >> "$trial_file"
done
cp "$trial_file" "$raw_file"

metric_summary() {
    label=$1
    values_file="$result_prefix.metric.tmp"
    grep -F "$label" "$trial_file" | sed -n 's/.*[[:space:]]\([0-9][0-9.]*\)$/\1/p' | sort -n > "$values_file"
    median=$(sed -n '5p' "$values_file")
    p95=$(sed -n '9p' "$values_file")
    throughput=$(awk -v value="$median" 'BEGIN { if (value > 0) printf "%.2f", 1000000000/value; else print "n/a" }')
    printf '%s median_ns=%s p95_ns=%s throughput_per_s=%s\n' "$label" "$median" "$p95" "$throughput" >> "$summary_file"
    rm -f "$values_file"
}
: > "$summary_file"
metric_summary 'scoped timeframe AST'
metric_summary 'resample AST'
metric_summary 'scoped timeframe IR'
metric_summary 'resample IR'
metric_summary 'generated C/CUDA ABI'
metric_summary 'uncached 2x current+2x [1]'
metric_summary 'CSE locals current/[1]'

compiler=$(cmake -LA -N "$build_dir" 2>/dev/null | sed -n 's/^CMAKE_C_COMPILER:FILEPATH=//p')
compiler_version=$($compiler --version 2>/dev/null | sed -n '1p')
cpu=$(sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo | sed -n '1p')
gpu=unavailable
if command -v nvidia-smi >/dev/null 2>&1; then
    if detected_gpu=$(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null); then
        gpu=$(printf '%s\n' "$detected_gpu" | sed -n '1p')
        test -n "$gpu" || gpu=unavailable
    fi
fi
timestamp=$(date -u '+%Y-%m-%dT%H:%M:%SZ')

{
    echo "# cxpr resample benchmark"
    echo
    echo "- Timestamp: $timestamp"
    echo "- Build: Release"
    echo "- Compiler: $compiler_version"
    echo "- CPU: $cpu"
    echo "- GPU/driver: $gpu"
    echo "- Dataset: 4096 samples; 500000 timed evaluations/trial; 9 trials; 1000 planning repetitions/trial"
    echo "- Gate: median CPU metrics must remain within 15% of the accepted baseline; GPU metrics require a CUDA-capable runner and are gated separately."
    echo
    echo '```text'
    sed -n '1,$p' "$raw_file"
    echo '```'
    echo
    echo "## CPU distribution summary"
    echo
    echo '```text'
    sed -n '1,$p' "$summary_file"
    echo '```'
    echo
    echo "- Allocations and peak host memory: not measured by this harness."
    echo "- Generated artifact size is reported per trial in the raw output."
} > "$result_prefix.md"

escape_json() { sed 's/\\/\\\\/g; s/"/\\"/g' | awk '{printf "%s\\n", $0}'; }
raw_json=$(escape_json < "$raw_file")
summary_json=$(escape_json < "$summary_file")
cat > "$result_prefix.json" <<EOF
{
  "schema": "cxpr-resample-benchmark/v2",
  "timestamp_utc": "$timestamp",
  "build_type": "Release",
  "compiler": "$(printf '%s' "$compiler_version" | escape_json)",
  "cpu": "$(printf '%s' "$cpu" | escape_json)",
  "gpu": "$(printf '%s' "$gpu" | escape_json)",
  "sample_count": 4096,
  "evaluation_count": 500000,
  "trial_count": 9,
  "planning_repetitions": 1000,
  "cpu_regression_gate_percent": 15,
  "host_allocations": null,
  "host_peak_memory_bytes": null,
  "workloads": ["one_interval_current", "one_interval_lookback_1", "nested_indicator", "repeated_deduplicated", "multiple_intervals", "warmup_gap", "non_trading_path"],
  "cpu_distribution_summary": "$summary_json",
  "raw": "$raw_json"
}
EOF

echo "Wrote $result_prefix.md, $result_prefix.json, and $raw_file"
