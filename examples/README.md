# cxpr Examples

This folder contains longer examples that are useful for understanding how `cxpr` can be embedded in an application.

## Included

- [`trading.md`](trading.md): expression composition and dependency ordering
  Test: [`../tests/examples/trading.test.c`](../tests/examples/trading.test.c)
- [`robotics.md`](robotics.md): guard conditions, thresholds, and distance helpers
  Test: [`../tests/examples/robotics.test.c`](../tests/examples/robotics.test.c)
- [`physics.md`](physics.md): analytical expressions and struct-based field access
  Test: [`../tests/examples/physics.test.c`](../tests/examples/physics.test.c)
- [`scientific.md`](scientific.md): interdependent expressions resolved in topological
  order across relativity, quantum mechanics, and chemistry
  Test: [`../tests/examples/scientific.test.c`](../tests/examples/scientific.test.c)
- [`engine_slots.c`](engine_slots.c): `cxpr_engine` driven from host-owned
  hot-loop values via pre-bound `cxpr_context_slot` handles
- [`load_model.c`](load_model.c): load and run a model from a [`.cxpr`
  file](load_model.cxpr) through the public document API
- [`ast_snapshot_cytoscape/`](ast_snapshot_cytoscape/): single-tick AST
  snapshot rendered as a Cytoscape tree

These examples are illustrative. They show expression shapes and integration patterns, not validated domain models.

## Loading A `.cxpr` File

The public document API loads and parses a model file in one call:

```c
cxpr_error err = {0};
cxpr_doc* doc = cxpr_doc_load_model("model.cxpr", &err);
```

The complete example also demonstrates error reporting, keeping the document
alive while its borrowed model and compiled program are in use, setting the
`price` input through the session context, evaluating one tick, and reading the
`above_threshold` output.
From `libs/cxpr/`:

```bash
cc examples/load_model.c -Iinclude -Lbuild -lcxpr -lm -o /tmp/cxpr_load_model
/tmp/cxpr_load_model examples/load_model.cxpr
```

Use `cxpr_doc_load_manifest(path, &err)` instead for a manifest-only file.

## Engine Examples

The `engine_*.c` files are standalone embedding examples for `cxpr_engine`.
They are intentionally small and can be compiled directly from `libs/cxpr/`
against a local `build` directory:

```bash
cc examples/engine_slots.c -Iinclude -Lbuild -lcxpr -lm -o /tmp/cxpr_engine_slots
/tmp/cxpr_engine_slots
```

## Running The Example Tests

From `libs/cxpr/`:

```bash
cmake --build build --target test_examples_trading
./build/tests/test_examples_trading

cmake --build build --target test_examples_robotics
./build/tests/test_examples_robotics

cmake --build build --target test_examples_physics
./build/tests/test_examples_physics

cmake --build build --target test_examples_scientific
./build/tests/test_examples_scientific
```

To run the full test suite instead:

```bash
cmake --build build --target test
```
