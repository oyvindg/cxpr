#!/bin/sh
# Build and run the cxpr micro-benchmarks, logging everything to a directory so
# the results can be collected and turned into a reference table.
#
# Usage:
#   benchmarks/run_all_bench.sh [BUILD_DIR] [LOG_DIR]
#   TRIALS=10 benchmarks/run_all_bench.sh          # override trial count
#
# Defaults: BUILD_DIR=<repo>/build-bench  LOG_DIR=/tmp/cxpr_bench  TRIALS=5
#
# Each benchmark target is built independently, so one failure does not abort
# the rest. Binaries are located with find(), so a different output layout still
# works. The resample benchmark reuses its own median/p95 runner. A CUDA bulk
# benchmark is run only when nvcc and g++-12 are both available (CUDA 12.3 needs
# host GCC <= 12).

set -u

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-"$repo_dir/build-bench"}
log_dir=${2:-/tmp/cxpr_bench}
trials=${TRIALS:-5}
jobs=$(nproc 2>/dev/null || echo 4)

mkdir -p "$log_dir"

echo "== configuring Release benchmark build =="
cmake -S "$repo_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release -DCXPR_BUILD_BENCHMARKS=ON || {
    echo "configure failed; aborting"; exit 1; }

{
    echo "date=$(date -u +%FT%TZ)"
    echo "host=$(uname -srm)"
    echo "cpu=$(lscpu 2>/dev/null | sed -n 's/^Model name: *//p')"
    echo "gcc=$(gcc --version 2>/dev/null | head -1)"
    echo "build_dir=$build_dir"
    echo "trials=$trials"
} | tee "$log_dir/env.log"

core_benches="cxpr_bench_ir cxpr_bench_model cxpr_bench_engine cxpr_bench_index_history"

echo "== building benchmark targets (independently) =="
for tgt in $core_benches; do
    printf '### %s ... ' "$tgt"
    if cmake --build "$build_dir" -j"$jobs" --target "$tgt" \
            >"$log_dir/build_$tgt.log" 2>&1; then
        echo "built"
    else
        echo "BUILD FAILED (see $log_dir/build_$tgt.log)"
    fi
done

echo "== running benchmarks ($trials trials each) =="
for b in $core_benches; do
    bin=$(find "$build_dir" -type f -perm -u+x -name "$b" ! -name '*_tool' 2>/dev/null | head -1)
    if [ -n "$bin" ] && [ -x "$bin" ]; then
        : > "$log_dir/$b.log"
        t=1
        while [ "$t" -le "$trials" ]; do
            echo "== trial $t ==" >> "$log_dir/$b.log"
            "$bin" >> "$log_dir/$b.log" 2>&1
            t=$((t + 1))
        done
        echo "  $b -> $log_dir/$b.log"
    else
        echo "  SKIP $b (not built; see $log_dir/build_$b.log)"
    fi
done

echo "== resample benchmark (9-trial median/p95 runner) =="
if [ -x "$repo_dir/benchmarks/run_resample_bench.sh" ]; then
    if sh "$repo_dir/benchmarks/run_resample_bench.sh" \
            "$repo_dir/build-resample-bench" "$log_dir/resample" \
            >"$log_dir/build_resample.log" 2>&1; then
        echo "  resample -> $log_dir/resample.summary.txt"
    else
        echo "  resample runner failed (see $log_dir/build_resample.log)"
    fi
fi

echo "== optional: CUDA bulk benchmark =="
if command -v nvcc >/dev/null 2>&1 && command -v g++-12 >/dev/null 2>&1; then
    cuda_build="$repo_dir/build-cuda-bench"
    if CUDAHOSTCXX=/usr/bin/g++-12 cmake -S "$repo_dir" -B "$cuda_build" \
            -DCXPR_BUILD_TESTS=ON -DCXPR_BUILD_CUDA_TESTS=ON \
            -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-12 \
            >"$log_dir/build_cuda.log" 2>&1 \
        && CUDAHOSTCXX=/usr/bin/g++-12 cmake --build "$cuda_build" -j"$jobs" \
            --target cxpr_cuda_bulk_benchmark >>"$log_dir/build_cuda.log" 2>&1; then
        cbin=$(find "$cuda_build" -type f -perm -u+x -name 'cxpr_cuda_bulk_benchmark' 2>/dev/null | head -1)
        if [ -n "$cbin" ]; then
            "$cbin" >"$log_dir/cuda_bulk.log" 2>&1 && echo "  cuda -> $log_dir/cuda_bulk.log"
        fi
    else
        echo "  CUDA build failed (see $log_dir/build_cuda.log) — skipping"
    fi
else
    echo "  nvcc or g++-12 not available — skipping CUDA"
fi

echo
echo "== done. logs in $log_dir =="
ls -la "$log_dir"
