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

These examples are illustrative. They show expression shapes and integration patterns, not validated domain models.

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
