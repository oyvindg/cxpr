# Pathfinding fixtures (R6–R8)

Fixtures for the agnostic primitives that turn a distance field into a flow
field:

- **R6** — `argmin` / `argmax` / fast-arity `sum` (new builtins)
- **R7** — sentinel/∞ convention: an `$INF` constant + `fn sadd(a, b) = min(a + b, $INF)`.
  This is a plain cxpr function, **not** a builtin, so it lowers to generated-C/CUDA
  and needs no codegen change. R7 is convention/documentation, not language work.
- **R8** — host-side bulk all-reduce for convergence

R7 remains a plain `fn`; R6 is implemented by scalar folds in tree-eval, IR,
generated C, and the CUDA-inherited C path. Therefore:

- `sentinel.cxpr`, `grid_dist_4nabo.cxpr`, and `examples/bulk_grid/distance_field_cell.cxpr`
  compile and run without direction selection. `pathfinding_sentinel.test.c` runs ungated.
- `grid_relax_4nabo.cxpr`, `grid_relax_8nabo.cxpr`, and
  `examples/bulk_grid/flow_field_cell.cxpr` add `dir = argmin(...)` and exercise R6.

Acceptance coverage is executable rather than source-text-only:

- `acceptance.json` maps each implemented requirement to its fixture and CTest;
  CMake fails configuration if a listed CPU fixture or test disappears.

- `pathfinding_generated_parity` compiles and runs generated C from
  `argmin_argmax.cxpr` and `ties.cxpr`, comparing output bit patterns with IR.
- `pathfinding_flow_e2e` runs both grid fixtures through generated-C bulk until
  convergence and compares every distance and meaningful direction with Dijkstra.
- `cuda_pathfinding_parity` compiles the same fixtures with NVCC and executes them
  when a CUDA device exists; absence of a device is reported as CTest `Skipped`.
- `bulk_reduce` is also explicitly `Skipped` while optional R8 remains deferred.

## Direction convention (B4)

`dir` is a 0-based index into the argument list of `argmin`. The reference
descriptor uses:

- 4-neighbour: `N, S, E, W` → `0, 1, 2, 3`
- 8-neighbour: `N, S, E, W, NE, NW, SE, SW` → `0..7`

`dir` is only meaningful when `next_dist < $INF` (B3).
