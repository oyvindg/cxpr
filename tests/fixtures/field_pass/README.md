# Field-pass fixtures (proposed syntax)

These fixtures pin the **proposed** syntax for topology-agnostic field passes.
They are design artifacts, not runnable tests yet: none of them parse with the
current front end, because they use surface that does not exist today.

Design sources:

- [docs/read_only_peer_state.md](../../../docs/read_only_peer_state.md) — peer
  reads (`peers { }`, `slot.field`) over a frozen previous snapshot.
- [docs/shared_and_local_pass.md](../../../docs/shared_and_local_pass.md) — a
  shared collective pass (`shared { reduce ... }`) that runs once per step
  before the local per-element pass.

## Fixtures

| Fixture | Exercises | New surface |
|---------|-----------|-------------|
| `flow_field_cell.cxpr` | peer reads only | `peers { }`, `slot.field`, inferred visibility |
| `mean_field.cxpr` | shared+local, no peers | `shared { in { } sum(...) }`, `shared.<name>` |
| `gross_pitaevskii.cxpr` | shared+local + peers | union of both, 3D face peers, complex as `(re, im)` |

## Status

Every requirement here is **proposed** and gated. It must not be wired into a
running CTest until the corresponding language and runtime work lands. Track
status in `acceptance.json`; mirror the pathfinding convention where a fixture
graduates from `proposed` to a real `cpu_test` only once it compiles and runs.

Suggested graduation order, matching the implementation phases in the peer
document:

1. `flow_field_cell.cxpr` — once `peers`/`slot.field` parse and the two-buffer
   local pass runs on the reference backend.
2. `mean_field.cxpr` — once `shared { reduce }` and `shared.<name>` land.
3. `gross_pitaevskii.cxpr` — once the shared pass composes with peer reads and
   3D connectivity, across reference/IR/generated-C (and CUDA when present).

## Conventions carried over

- Peer visibility of a state field is inferred from its `slot.field` reads; no
  `export`/`shared` keyword marks it.
- The `shared { ... }` block owns the shared phase: a nested `in { }` (read bare,
  like params) plus collective bindings, which are read as `shared.<name>` in the
  local phase.
- Both phases read the same immutable previous snapshot; only the local pass
  writes next-state. The host owns the outer loop and convergence.
- Reduction determinism follows the backend parity contract; a parity proof
  gates graduation, exactly as R6 required.
