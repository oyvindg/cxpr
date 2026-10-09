# Bulk/Grid Host Pattern

[`klein_gordon_cell.cxpr`](klein_gordon_cell.cxpr) describes the local update
for one cell. It owns the local field equation, nonlinear potential, damping,
source coupling, semi-implicit Euler step, and energy diagnostic. It does not
define a grid: the host supplies `phi_left`, `phi`, `phi_right`, and `source`
columns and decides how boundary cells are populated.

Generate the scalar C evaluator at build time:

```sh
cxpr_model_codegen \
  --model klein_gordon_cell.cxpr \
  --output klein_gordon_cell.gen.c \
  --function klein_gordon_cell_tick
```

The generated descriptor can be passed to `cxpr_bulk_run`. Inputs and outputs
are structure-of-arrays columns, parameters are shared for a launch, and state
is independently strided per cell. A host may call `cxpr_bulk_run_range` on
disjoint ranges from its own worker pool.

## Distance- and flow-field descriptor (version 1)

[`distance_field_cell.cxpr`](distance_field_cell.cxpr) and
[`flow_field_cell.cxpr`](flow_field_cell.cxpr) define the version-1 `grid_relax`
interop contract. The host halo-gathers scalar columns `d_n`, `d_s`, `d_e`, and
`d_w`, followed by `cost`, `is_wall`, and `is_goal`. Blocked and out-of-bounds
neighbors use the finite sentinel `$INF = 1e18`; the model's `sadd` helper keeps
sentinel arithmetic saturated.

The flow output `dir` is an index in the canonical order N, S, E, W (0..3).
For an eight-neighbor descriptor, append NE, NW, SE, SW (4..7). `dir` is
undefined when `next_dist >= $INF`; the host must validate this before movement.
The host remains responsible for occupancy, collision checks, buffer swaps,
convergence, and conflicting movement intents.

```text
dist = goal/wall initialization
repeat:
    halo-gather dist into d_n/d_s/d_e/d_w
    cxpr_bulk_run(grid_relax_v1, current_inputs, next_outputs)
    residual = deterministic host reduction of abs(next_dist - dist)
    swap(dist, next_dist)
until residual == 0 or iteration_limit reached
```

Keep the descriptor version with the host-side schema and reject unknown
versions before binding columns. Iteration is deliberately host-owned; no graph
traversal, heap, or array-valued column crosses the cxpr ABI.

For CUDA, the existing CUDA plugin emits the scalar device evaluator. A future
bulk artifact will add the generic one-thread-per-element wrapper; topology,
allocation, streams, timestep loops, and buffer swaps remain host-owned.

The optional end-to-end CUDA test generates that evaluator from this model,
launches 65,536 cells, and checks the updated field, momentum, acceleration,
and energy density against an independent CPU reference:

```sh
cmake -S libs/cxpr -B build/cxpr-cuda \
  -DCXPR_BUILD_TESTS=ON \
  -DCXPR_BUILD_BENCHMARKS=OFF \
  -DCXPR_BUILD_CUDA_TESTS=ON
cmake --build build/cxpr-cuda --target test_cuda_bulk_runtime -j
ctest --test-dir build/cxpr-cuda -L cuda --output-on-failure
```

`CXPR_BUILD_CUDA_TESTS` is off by default. Enabling it without `nvcc` produces
a configure-time error instead of silently skipping the requested test.

Build and run the resident multi-step benchmark separately from CTest:

```sh
cmake --build build/cxpr-cuda --target cxpr_cuda_bulk_benchmark -j
./build/cxpr-cuda/tests/cxpr_cuda_bulk_benchmark 1048576 1000 256
```

Arguments are `cells`, `steps`, CUDA `block_size`, and optional repetition
count (default 7). The reported throughput uses the median; minimum and maximum
times expose run-to-run variance. The timed region keeps
all field buffers on the GPU, uses ping-pong buffers, applies periodic
boundaries in the host-owned kernel wrapper, and excludes allocation, initial
copies, warmup, final copies, and CPU verification. The executable reports
cell-steps/s and effective memory bandwidth, then verifies the complete final
field against an independent CPU time loop.

For a repeatable 128/256/512 sweep and a repository-local Markdown report:

```sh
libs/cxpr/examples/bulk_grid/run_cuda_benchmark.sh build/cxpr-cuda-gcc13
```

The script prints the report path. Workload can be overridden with
`CXPR_BENCH_CELLS`, `CXPR_BENCH_STEPS`, and `CXPR_BENCH_REPETITIONS`.

## Recorded baseline

Measured at parent commit `5016f82` on 2026-09-12 using an NVIDIA GeForce RTX
3070 Laptop GPU (compute capability 8.6, driver 580.173.02), 1,048,576 cells,
1,000 steps, and seven repetitions:

| CUDA block size | Median kernel time | Throughput | Effective bandwidth | Parity |
| ---: | ---: | ---: | ---: | :---: |
| 128 | 427.309 ms | 2.454 Gcell-steps/s | 176.68 GB/s | OK |
| **256** | **418.431 ms** | **2.506 Gcell-steps/s** | **180.43 GB/s** | **OK** |
| 512 | 419.861 ms | 2.497 Gcell-steps/s | 179.82 GB/s | OK |

Block size 256 is the baseline default for this device. The bandwidth number
is an effective model-traffic estimate, not measured DRAM traffic, and results
on other GPUs, power profiles, drivers, or thermal states are not expected to
match exactly. All configurations matched the independent CPU implementation
after the complete 1,000-step simulation.

Represent complex numbers or small tensors as scalar components (`psi_re`,
`psi_im`, `metric_00`, and so on). This keeps the model portable while the host
chooses AoS/SoA storage and gathers the required components.
