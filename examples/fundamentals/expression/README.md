# Expression fundamentals

Tier: Minimal  
Domain: Fundamentals

This example compares two supported workflows over identical inputs:

| Authored model | Programmatic C API |
| --- | --- |
| [`model.cxpr`](model.cxpr) declares inputs, a parameter, named expressions, and outputs. | [`host.c`](host.c) constructs the same named expressions through `cxpr_evaluator`. |
| The C host loads and compiles the file once. | The C host registers and compiles the expression definitions once. |
| Both are evaluated with `mass = 2`, `velocity = 3`. | Both must produce energy `9` and `over_speed = false`. |

The ordinary C code is host integration, not a hand-written replacement for
the model. It uses public `cxpr_*` APIs and asserts parity between the two CXPR
authoring paths.

From a standalone `libs/cxpr` build:

```bash
cmake --build build --target cxpr_example_fundamentals_expression
./build/examples/cxpr_example_fundamentals_expression
ctest --test-dir build -R examples_fundamentals_expression
```

Expected output:

```text
expression: energy=9.0 over_speed=false; .cxpr/API parity OK
```
