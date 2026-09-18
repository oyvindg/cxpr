# Pathfinding fixtures (R6–R8)

Fixtures backing the field-based pathfinding requirements (see
`plans/field_pathfinding_requirements.md`). They exercise the agnostic
primitives that turn a distance field into a flow field:

- **R6** — `argmin` / `argmax` / fast-arity `sum`
- **R7** — sentinel/∞ contract (`sat_add` + a defined `INF` constant)
- **R8** — host-side bulk all-reduce for convergence

Fixtures that require R6/R7 use not-yet-implemented builtins (`argmin`, `argmax`,
`sat_add`) and therefore **do not parse on `release/3.2.0` yet**. They are loaded
only by the gated test stubs (`CXPR_PATHFINDING_*_READY`, default off), which are
flipped on as each requirement lands.

`distance_field_cell.cxpr` in `examples/bulk_grid/` is the subset that already
compiles today (min-fold relaxation, no direction), and is the "works now"
baseline referenced by the plan.

## Direction convention (B4)

`dir` is a 0-based index into the argument list of `argmin`. The reference
descriptor uses:

- 4-neighbour: `N, S, E, W` → `0, 1, 2, 3`
- 8-neighbour: `N, S, E, W, NE, NW, SE, SW` → `0..7`

`dir` is only meaningful when `next_dist < INF` (B3).
