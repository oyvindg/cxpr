# AST Snapshot Cytoscape Example

This example shows a single-tick diagnostic snapshot for small engine-backed
expression sets. The HTML page can switch between trading, physics, chemistry,
robotics, quantum, black hole, reactor, and credit policy examples. It renders the
expression-level flow, a full evaluation tree, and an AST drilldown for the
selected expression. Active nodes are colored by result and non-evaluated nodes
show why they were skipped.

The physics scenario also demonstrates expression-defined functions registered
with `cxpr_registry_define_fn`, using `energy_norm(v, mass, cap) => ...`.
The quantum scenario combines expression-defined functions, lookback, arrays,
`contains`, `min`/`max`, ternary expressions, rising samples, and pipe chains.
The black hole scenario models event horizon proximity, photon-sphere distance,
redshift, strong lensing, relativistic orbit, and observation safety.
The reactor scenario models neutron flux, control rod withdrawal, boron
dilution, coolant flow, pressure, thermal margin, and SCRAM conditions.
The credit policy scenario is a small, anonymized loan policy trace with LTV,
debt-to-income, liquidity, eligibility gates, manual review, decline, and
maximum loan amount outputs.

From a configured `libs/cxpr` build:

```bash
cmake --build build --target cxpr_example_ast_snapshot_cytoscape
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.trading.json trading
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.physics.json physics
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.chemistry.json chemistry
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.robotics.json robotics
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.quantum.json quantum
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.blackhole.json blackhole
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.reactor.json reactor
./build/cxpr_example_ast_snapshot_cytoscape examples/ast_snapshot_cytoscape/snapshot.credit_policy.json credit_policy
python3 -m http.server --directory examples/ast_snapshot_cytoscape 8000
```

Open `http://127.0.0.1:8000/`.

To demonstrate host-owned metadata without coupling cxpr to a specific host,
write a snapshot with the optional host hooks enabled:

The emitted `host` objects are opaque to cxpr and can be used by a host such as
dyn to attach roles, source locations, strategy sections, or domain metrics.
The HTML page's Metadata selector loads matching host-demo snapshots named
`snapshot.<scenario>.host.json`:

```bash
./build/cxpr_example_ast_snapshot_cytoscape --host-demo examples/ast_snapshot_cytoscape/snapshot.trading.host.json trading
./build/cxpr_example_ast_snapshot_cytoscape --host-demo examples/ast_snapshot_cytoscape/snapshot.physics.host.json physics
./build/cxpr_example_ast_snapshot_cytoscape --host-demo examples/ast_snapshot_cytoscape/snapshot.chemistry.host.json chemistry
./build/cxpr_example_ast_snapshot_cytoscape --host-demo examples/ast_snapshot_cytoscape/snapshot.robotics.host.json robotics
./build/cxpr_example_ast_snapshot_cytoscape --host-demo examples/ast_snapshot_cytoscape/snapshot.quantum.host.json quantum
./build/cxpr_example_ast_snapshot_cytoscape --host-demo examples/ast_snapshot_cytoscape/snapshot.blackhole.host.json blackhole
./build/cxpr_example_ast_snapshot_cytoscape --host-demo examples/ast_snapshot_cytoscape/snapshot.reactor.host.json reactor
./build/cxpr_example_ast_snapshot_cytoscape --host-demo examples/ast_snapshot_cytoscape/snapshot.credit_policy.host.json credit_policy
```

In Host demo mode the flow graph also renders host-defined output nodes. Trading
marks `entry` and `exit`; the other scenarios expose their own decision or result
outputs such as `Experiment OK`, `Reaction OK`, `Drive Allowed`, and
`Experiment Accept`. The black hole scenario exposes `Inside Event Horizon`,
`Event Horizon Alert`, `Observation Safe`, and `Redshift Factor`. The reactor
scenario exposes `Reactor Stable`, `SCRAM Required`, `Coolant Alert`, and
`Thermal Margin`. The credit policy scenario exposes `Policy Green`,
`Manual Review`, `Decline Required`, `Max Loan Amount`, `Approved Amount`, and
`Decision Code`.
