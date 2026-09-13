# Bulk and grid execution

`<cxpr/bulk.h>` maps one generated scalar-model evaluation to each logical
element. It deliberately does not define grids, neighbors, boundary conditions,
timesteps, threads, CUDA streams, or memory placement.

A `cxpr_bulk_view` is host-owned structure-of-arrays storage. Each input and
output stride is measured in `double` elements; an input stride of zero
broadcasts element zero. Writable output strides must be non-zero when more
than one element is executed. Parameters are shared by the launch. `states`
contains one independent state block per element and only `state_stride` is
measured in bytes.

Always call `cxpr_bulk_validate()` before dispatch. Call `cxpr_bulk_reset()`
before the first run unless every state block was fully zero-initialized.
`cxpr_bulk_run()` is serial. A host worker pool may call
`cxpr_bulk_run_range()` concurrently only for disjoint ranges and non-overlapping
writable storage. Range arithmetic, buffer extents, descriptor schema, and state
stride are checked and reported as `cxpr_bulk_status` values.

For a grid, expose local stencil values as named scalar columns, for example
`phi_left`, `phi`, and `phi_right`. The host gathers neighbors, fills halo or
boundary cells, launches ranges or kernels, and swaps input/output buffers after
each timestep. Complex values and tensors remain portable when represented as
scalar components such as `psi_re`, `psi_im`, or `metric_00`.

The CUDA pattern is the same contract at a different dispatch layer: compile
the generated device evaluator, wrap it in a one-thread-per-element kernel, and
keep allocations, transfers, streams, topology, and synchronization in host
code. Validate sizes and initialize state before upload. The repository's
Klein-Gordon example and CUDA parity test demonstrate this division:

```sh
cmake -S libs/cxpr -B build/cxpr-cuda \
  -DCXPR_BUILD_TESTS=ON -DCXPR_BUILD_CUDA_TESTS=ON
cmake --build build/cxpr-cuda --target test_cuda_bulk_runtime -j
ctest --test-dir build/cxpr-cuda -L cuda --output-on-failure
```

See `examples/bulk_grid/`, `tests/bulk.test.c`,
`tests/bulk_codegen_e2e.test.c`, and `tests/cuda_bulk_runtime.cu`.

Persistent `buffer<T>` rings live inside each generated state block, so every
bulk element has independent history. This layout is supported by both CPU
bulk execution and the CUDA dispatch pattern. Exact session/generated-C parity
and cross-element isolation are covered by `tests/bulk_buffer_parity.test.c`;
device-state parity is covered by `tests/cuda_bulk_buffer_runtime.cu`.

## API inventory

| Public symbol | Role |
| --- | --- |
| `cxpr_bulk_validate` | Validate descriptor/schema, required buffers, strides, and overflow-prone index products. |
| `cxpr_bulk_run` / `cxpr_bulk_run_range` | Serial execution of all elements or one checked range. |
| `cxpr_bulk_reset` / `cxpr_bulk_reset_range` | Call descriptor reset or zero each state block. |
| `cxpr_bulk_status_message` | Convert `cxpr_bulk_status` to stable diagnostic text. |
