# AST Snapshot Cytoscape Example

This example shows a single-tick diagnostic snapshot for small engine-backed
expression sets. The HTML page can switch between trading, physics, chemistry,
robotics, and quantum examples. It renders the expression-level flow, a full
evaluation tree, and an AST drilldown for the selected expression. Active nodes
are colored by result and non-evaluated nodes show why they were skipped.

The physics scenario also demonstrates expression-defined functions registered
with `cxpr_registry_define_fn`, using `energy_norm(v, mass, cap) => ...`.
The quantum scenario combines expression-defined functions, lookback, arrays,
`contains`, `min`/`max`, ternary expressions, rising samples, and pipe chains.

From a configured `libs/cxpr` build:

```bash
cmake --build build --target cxpr_example_ast_snapshot_cytoscape
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.trading.json trading
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.physics.json physics
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.chemistry.json chemistry
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.robotics.json robotics
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.quantum.json quantum
python3 -m http.server --directory examples/ast_snapshot_cytoscape 8000
```

Open `http://127.0.0.1:8000/`.
