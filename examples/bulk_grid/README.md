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

Arguments are `cells`, `steps`, and CUDA `block_size`. The timed region keeps
all field buffers on the GPU, uses ping-pong buffers, applies periodic
boundaries in the host-owned kernel wrapper, and excludes allocation, initial
copies, warmup, final copies, and CPU verification. The executable reports
cell-steps/s and effective memory bandwidth, then verifies the complete final
field against an independent CPU time loop.

Represent complex numbers or small tensors as scalar components (`psi_re`,
`psi_im`, `metric_00`, and so on). This keeps the model portable while the host
chooses AoS/SoA storage and gathers the required components.
