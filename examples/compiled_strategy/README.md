# Compiled strategy example

This host-neutral example compiles a stateful, multi-output model to C during
the build, executes the generated descriptor for 512 deterministic ticks, and
compares every output with `cxpr_model_session`.

The host bindings are generic numeric columns (`input_a`, `input_b`, and
`clock`). No application-specific data, indicator, or policy API is part of the
CXPR interface.

From the repository root:

```sh
cmake --build build --target cxpr_compiled_strategy_example
ctest --test-dir build -R cxpr_compiled_strategy_example --output-on-failure
```

The engine is useful for dynamic/reference evaluation. Generated C is the
deployment path for fixed models and hot loops.
