# CXPR executable example matrix

This matrix is the documentation and conformance inventory for stable public
CXPR capability families. `Covered` links to executable public examples or
tests. `Pilot` is covered by the new manifest contract but still needs broader
backend parity. `Gap` is explicit work; a blank cell never implies support.

Backend abbreviations: evaluator (`Eval`), typed IR (`IR`), generated C
(`Gen C`), bulk execution (`Bulk`), Node binding (`Node`), and CUDA (`CUDA`).

| Capability family | Minimal example | Integrated/reference example | Backend evidence | Status |
| --- | --- | --- | --- | --- |
| Scalar values, operators, params, named dependencies | [Expression fundamentals](fundamentals/expression/README.md) | [Scientific dependency graph](scientific.md) | Eval/IR: `expression_evaluator`, `ir_exec_typed`; Gen C: `generated` | Pilot |
| Diagnostics and source locations | [Document tooling](ast_snapshot_cytoscape/README.md) | Native/CLI tooling parity | Eval/tooling: `doc_tooling`, `doc_tooling_cli_native_parity` | Covered |
| Records, arrays, vectors, and field access | [Physics formulas](physics.md) | `vec2_math`, collision and structured-output fixtures | Eval/IR/Gen C: `struct_ops`, `generated_array_parity`, `vec2_math` | Covered |
| Bounded collections, lexical captures, structural sum | Audio collection modules | Polyphonic audio arrangements | Eval/IR/Gen C: collection and codegen regressions; CUDA: explicit capability boundary | Covered |
| Stateful model and atomic updates | [Damped oscillator](domains/physics/oscillator/README.md) | [Compiled strategy](compiled_strategy/README.md) | Eval: pilot; Gen C parity: compiled strategy | Pilot |
| Reset, snapshots, and state stores | Damped oscillator reset | Bulk state and snapshot fixtures | Eval/Bulk: `bulk_snapshot`, `bulk_state_store` | Covered |
| Imports, scoped children, rooted records, composition | Audio modules | Audio DSP and complete arrangements | Eval/Gen C: composition and imported-map tests | Covered |
| Context slots, host functions, and sources | [Engine slots](engine_slots.c) | [Robotics](robotics.md) and scoped sources | Eval/engine: context, registry, and source-plan tests | Covered |
| History, windows, and resampling | [Engine lookback](engine_lookback.c) | Trading and rolling-buffer fixtures | Eval/Gen C: history and resample parity tests | Covered |
| Generated descriptors and ordered batch | [Generated descriptor host](generated_descriptor_host.c) | [Compiled strategy](compiled_strategy/README.md) | Gen C/Bulk: generated batch and buffer parity | Covered |
| Grids and numerical fields | [Bulk grid](bulk_grid/README.md) | Klein-Gordon and pathfinding flow fields | Gen C/Bulk/CUDA: pathfinding and Klein-Gordon fixtures | Covered |
| Graph, metadata, and debug artifacts | AST Cytoscape viewer | Cross-domain snapshots | Plugins: graph/meta/debug-map tests | Covered |
| Optimizer params, constraints, ranking | [Optimizer host](optimize_host.c) | [Multi-objective optimizer](optimize_multiobjective.c) | CPU/CXCU constraint tests | Covered |
| Robotics control and kinematics | [Robotics guards](robotics.md) | Hexapod fixture | Eval: robotics example; composed public host/docs | Gap |
| Native pathfinding and flow fields | [Pathfinding](domains/games/pathfinding/README.md) | Dijkstra/flow-field parity | Eval/Gen C bit parity; Gen C/Bulk full-grid parity; CUDA fold parity | Covered |
| Game collision and projectile simulation | Collision and projectile fixtures | Collision-query and avoidance parity | Eval and typed external-array fixture tests | Gap: public README/host |
| Node projections | Audio renderer | Generated session and batch tests in `cxpr-node` | Node/Gen C | Gap: linkable standalone example |

## Manifested examples

[`manifest.json`](manifest.json) is the machine-readable subset governed by the
new example contract. CMake validates every listed README, `.cxpr` source, C
host, target, and test name at configure time. As existing examples are
normalized, they should be added to the manifest and their matrix status moved
from `Covered` or `Gap` to `Pilot`, then to `Covered` after applicable backend
parity lands.

## Contribution rule

A stable public feature change must update this matrix and either:

1. extend an existing manifested example and its public-contract test; or
2. add a new minimal example, manifest entry, CTest target, and applicable
   backend parity or expected-rejection test.

Experimental capabilities must be labelled explicitly instead of being
represented as stable coverage.
