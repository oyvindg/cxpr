# Bulk/Grid Host Pattern

[`klein_gordon_cell.cxpr`](klein_gordon_cell.cxpr) describes the local update
for one cell. It does not define a grid: the host supplies `phi_left`, `phi`,
and `phi_right` columns and decides how boundary cells are populated.

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

Represent complex numbers or small tensors as scalar components (`psi_re`,
`psi_im`, `metric_00`, and so on). This keeps the model portable while the host
chooses AoS/SoA storage and gathers the required components.
