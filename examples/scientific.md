# Scientific Computing Example

Related test: [`../tests/examples/scientific.test.c`](../tests/examples/scientific.test.c)

This example shows the **named-expression evaluator** resolving an interdependent
set of formulas in automatic topological order, in a single evaluation pass. The
expressions span four domains — general relativity, special relativity, quantum
mechanics, and chemical kinetics — to make the point that `cxpr` is a
domain-agnostic expression engine: it only sees numbers, identifiers, and
functions.

`time_dilation` depends on `orbit_radius`, which depends on
`schwarzschild_radius`; they are declared out of order on purpose and the
evaluator sorts them.

```c
#include <cxpr/cxpr.h>
#include <stdio.h>

int main(void) {
    cxpr_registry*  reg = cxpr_registry_new();
    cxpr_evaluator* ev  = cxpr_evaluator_new(reg);
    cxpr_context*   ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_register_defaults(reg);

    // Physical constants (SI) and inputs.
    cxpr_context_set_array(ctx, (cxpr_context_entry[]) {
        {"G", 6.67430e-11}, {"c", 299792458.0}, {"M", 8.54e36}, // Sagittarius A*
        {"v", 0.6 * 299792458.0},                              // 0.6 c
        {"hbar", 1.054571817e-34}, {"omega", 1.0e15}, {"n", 2.0},
        {"A", 1.0e13}, {"Ea", 75000.0}, {"R", 8.314462618}, {"T", 298.15},
        {NULL, 0.0}
    });
    cxpr_context_set_param(ctx, "r_over_rs", 4.0); // observer at 4 Schwarzschild radii

    const cxpr_expression_def defs[] = {
        // General relativity
        { "time_dilation",        "sqrt(1 - schwarzschild_radius / orbit_radius)" },
        { "orbit_radius",         "$r_over_rs * schwarzschild_radius" },
        { "schwarzschild_radius", "2 * G * M / c^2" },
        { "outside_horizon",      "orbit_radius > schwarzschild_radius" },
        // Special relativity
        { "lorentz_factor",       "1 / sqrt(1 - (v / c)^2)" },
        // Quantum mechanics: harmonic-oscillator energy level
        { "oscillator_energy",    "hbar * omega * (n + 0.5)" },
        // Chemistry: Arrhenius reaction rate
        { "reaction_rate",        "A * exp(-Ea / (R * T))" },
    };

    if (!cxpr_expressions_add(ev, defs, 7, &err) ||
        !cxpr_evaluator_compile(ev, &err)) {
        fprintf(stderr, "setup error: %s\n", err.message);
        return 1;
    }
    cxpr_evaluator_eval(ev, ctx, &err);

    printf("time dilation factor = %.6f\n",
           cxpr_expression_get_double(ev, "time_dilation", NULL));   // sqrt(0.75)
    printf("Lorentz factor       = %.6f\n",
           cxpr_expression_get_double(ev, "lorentz_factor", NULL));  // 1.25 at 0.6c
    printf("oscillator energy    = %.3e J\n",
           cxpr_expression_get_double(ev, "oscillator_energy", NULL));
    printf("reaction rate        = %.3e /s\n",
           cxpr_expression_get_double(ev, "reaction_rate", NULL));

    cxpr_context_free(ctx);
    cxpr_evaluator_free(ev);
    cxpr_registry_free(reg);
    return 0;
}
```

The expression set, by domain:

```text
# General relativity — gravitational time dilation outside a black hole
schwarzschild_radius = 2 * G * M / c^2
orbit_radius         = $r_over_rs * schwarzschild_radius
time_dilation        = sqrt(1 - schwarzschild_radius / orbit_radius)
outside_horizon      = orbit_radius > schwarzschild_radius

# Special relativity — Lorentz factor
lorentz_factor       = 1 / sqrt(1 - (v / c)^2)

# Quantum mechanics — harmonic-oscillator energy level E_n
oscillator_energy    = hbar * omega * (n + 0.5)

# Chemistry — Arrhenius reaction rate
reaction_rate        = A * exp(-Ea / (R * T))
```

## Run Test

From `libs/cxpr/`:

```bash
cmake --build build --target test_examples_scientific
./build/tests/test_examples_scientific
```
