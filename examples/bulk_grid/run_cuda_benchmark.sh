#!/bin/sh
set -eu

build_dir=${1:-build/cxpr-cuda-gcc13}
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
report=${2:-build/benchmarks/cxpr_cuda_bulk_${timestamp}.md}
cells=${CXPR_BENCH_CELLS:-1048576}
steps=${CXPR_BENCH_STEPS:-1000}
repetitions=${CXPR_BENCH_REPETITIONS:-7}
binary=${build_dir}/tests/cxpr_cuda_bulk_benchmark

if [ ! -x "$binary" ]; then
    echo "Missing benchmark executable: $binary" >&2
    echo "Build it with: cmake --build $build_dir --target cxpr_cuda_bulk_benchmark -j" >&2
    exit 2
fi

mkdir -p "$(dirname "$report")"
{
    echo "# CXPR resident CUDA bulk benchmark"
    echo
    echo "- UTC: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "- Commit: $(git rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "- Cells: $cells"
    echo "- Steps: $steps"
    echo "- Repetitions: $repetitions"
    if command -v nvidia-smi >/dev/null 2>&1; then
        echo "- Driver/GPU: $(nvidia-smi --query-gpu=driver_version,name --format=csv,noheader | head -n 1)"
    fi
    echo
    for block in 128 256 512; do
        echo "## Block size $block"
        echo
        echo '```text'
        "$binary" "$cells" "$steps" "$block" "$repetitions"
        echo '```'
        echo
    done
} > "$report"

echo "Benchmark report written to $report"
