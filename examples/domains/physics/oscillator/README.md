# Stateful damped oscillator

Tier: Integrated  
Domain: Physics

[`model.cxpr`](model.cxpr) implements a damped harmonic oscillator using a
semi-implicit Euler step. The model owns position and velocity state; the
[`host.c`](host.c) owns the tick loop, external force, lifecycle, and
validation.

The example uses dimensionless values for clarity:

- mass `1`, spring coefficient `4`, damping coefficient `0.2`;
- initial position `1` and velocity `0`;
- time step `0.005`; and
- zero external force for `2000` steps.

The host checks finite values, bounded motion, decay of the mechanical-energy
envelope, final energy below `0.30`, and deterministic lifecycle reset by
creating a fresh session from the same immutable compiled program. This is an
integration and state-semantics example, not a general-purpose physics engine.

From a standalone `libs/cxpr` build:

```bash
cmake --build build --target cxpr_example_physics_oscillator
./build/examples/cxpr_example_physics_oscillator
ctest --test-dir build -R examples_physics_oscillator
```

The evaluator-backed model session is the initial reference. Generated-C and
bulk parity are tracked explicitly as planned in the capability matrix; CUDA
must reject the state shape until a supported lowering exists.
