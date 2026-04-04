# Physics Example

Related test: [`../tests/examples/physics.test.c`](../tests/examples/physics.test.c)

This example shows analytical expressions and context-backed struct access. The setup below defines the scalar inputs and parameters used by the expressions.

```c
#include <cxpr/cxpr.h>
#include <stdio.h>

int main(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_register_builtins(reg);

    cxpr_context_set(ctx, "mass",          2.0);
    cxpr_context_set(ctx, "velocity",      3.0);
    cxpr_context_set(ctx, "damping",       0.15);
    cxpr_context_set(ctx, "t",             1.25);
    cxpr_context_set(ctx, "omega",         2.5);
    cxpr_context_set(ctx, "acceleration",  7.0);
    cxpr_context_set(ctx, "temperature", 420.0);

    cxpr_context_set_param(ctx, "max_acceleration", 9.0);
    cxpr_context_set_param(ctx, "meltdown_limit",  500.0);

    const char* fields[] = {"x", "y", "vx", "vy"};
    double vals[] = {1.2, -0.5, 0.8, 1.1};
    cxpr_context_set_fields(ctx, "body", fields, vals, 4);

    cxpr_ast* ast = cxpr_parse(
        parser,
        "abs(acceleration) > $max_acceleration or temperature >= $meltdown_limit",
        &err
    );

    printf("alarm=%d\n", cxpr_ast_eval_bool(ast, ctx, reg, &err));

    cxpr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);
    return 0;
}
```

```text
0.5 * mass * velocity^2
exp(-damping * t) * cos(omega * t)
sqrt(body.vx^2 + body.vy^2)
abs(acceleration) > $max_acceleration or temperature >= $meltdown_limit
```

## Run Test

From `libs/cxpr/`:

```bash
cmake --build build --target test_examples_physics
./build/tests/test_examples_physics
```
