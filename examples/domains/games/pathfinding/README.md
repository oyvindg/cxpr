# Native CXPR pathfinding and flow fields

Tier: Reference workload  
Domain: Games and robotics

Pathfinding is already supported by native CXPR models and the public
generated-C bulk API. The host owns the grid topology, halo gathering,
iteration to convergence, and path consumption; CXPR owns the scalar cell
relaxation, deterministic direction selection, generated descriptor, and bulk
execution contract.

The canonical authored models are:

- [`grid_relax_4nabo.cxpr`](../../../../tests/fixtures/pathfinding/grid_relax_4nabo.cxpr):
  four-neighbour flow-field relaxation;
- [`grid_relax_8nabo.cxpr`](../../../../tests/fixtures/pathfinding/grid_relax_8nabo.cxpr):
  eight-neighbour relaxation with diagonal costs;
- [`argmin_argmax.cxpr`](../../../../tests/fixtures/pathfinding/argmin_argmax.cxpr):
  deterministic direction/index selection; and
- [`sentinel.cxpr`](../../../../tests/fixtures/pathfinding/sentinel.cxpr):
  unreachable-cell sentinel behavior.

The grid models expose rooted records rather than unrelated flat inputs:

```cxpr
in neighbours { north, south, east, west }
in cell { cost, is_wall, is_goal }
```

Eight-neighbour models extend `neighbours` with diagonal fields and `cell`
with `diagonal_cost`. Generated descriptors scalarize these fields into names
such as `neighbours.north` and `cell.cost`; the bulk host therefore retains
structure-of-arrays memory and performs no runtime record allocation.

The public-contract host in
[`pathfinding_flow_e2e.test.c`](../../../../tests/pathfinding_flow_e2e.test.c)
generates the models to C, evaluates every grid cell through `cxpr_bulk_run`,
iterates until convergence, and compares the complete distance field with an
independent Dijkstra implementation. Four- and eight-neighbour grids are both
covered. [`pathfinding_generated_parity.test.c`](../../../../tests/pathfinding_generated_parity.test.c)
also requires bit parity between model-session execution and generated C for
fold and tie behavior.

From a standalone `libs/cxpr` build:

```bash
cmake --build build --target test_pathfinding_flow_e2e test_pathfinding_generated_parity
ctest --test-dir build -R 'pathfinding_(flow_e2e|generated_parity|sentinel)'
```

CUDA parity exists for the deterministic fold fixtures. Full iterative grid
scheduling remains host-owned and is not implied by CUDA source generation.
