# cxpr

`cxpr` is a standalone C11 library for evaluating expressions and executing
small, deterministic models. It has no external runtime dependencies and does
not assume anything about the application that embeds it.

The host provides values and decides when evaluation happens. `cxpr` provides
the language, validation, execution, state handling, and optional code
generation.

```text
host data -> cxpr expression or model -> typed outputs -> host action
```

## Why use it?

- **Host-agnostic:** data access, scheduling, I/O, and policy stay in the host.
- **Portable:** a C11 API with no external runtime dependencies.
- **Deterministic:** the same inputs, parameters, and state produce the same
  outputs.
- **Reusable:** the same calculation can run through the reference runtime or
  be generated as C for a fixed deployment.
- **Inspectable:** parsing, ASTs, type checking, dependency analysis, metadata,
  graphs, and debug maps are available to tooling.
- **Stateful when needed:** models support atomic per-tick state updates,
  history, windows, imports, and multiple outputs.
- **Extensible:** hosts can register C functions, resolve external sources, and
  preserve application-defined metadata without putting domain logic in the
  library.

Typical uses include configurable rules, validation, scoring, simulation,
control calculations, data pipelines, alert conditions, and generated
evaluators for hot loops.

## Choose the smallest API that fits

| Need | API |
| --- | --- |
| Evaluate one formula | Expression API |
| Re-evaluate a formula efficiently | Compiled expression (typed IR) |
| Named inputs, state, functions, or several outputs | Model API |
| Rising/falling events over a sequence | Engine API |
| Fixed model with minimal runtime work | Generated C |
| Metadata, graph, debug map, or CUDA source | Plugin API |

## Embed with CMake

As a subdirectory:

```cmake
add_subdirectory(external/cxpr)
target_link_libraries(my_app PRIVATE cxpr::cxpr)
```

Or from an installed package:

```cmake
find_package(cxpr CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE cxpr::cxpr)
```

Most applications can include the umbrella header:

```c
#include <cxpr/cxpr.h>
```

## 1. Evaluate an expression

Parse once, compile once, then update host values and evaluate repeatedly:

```c
#include <cxpr/cxpr.h>
#include <stdio.h>

int main(void) {
    cxpr_error error = {0};
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_registry* registry = cxpr_registry_new();

    cxpr_expr_ast* ast = cxpr_expr_ast_parse(
        parser, "enabled and value >= $limit", &error);
    cxpr_expr_compiled* expression =
        ast ? cxpr_expr_compile(ast, registry, &error) : NULL;

    cxpr_context_set_bool(context, "enabled", true);
    cxpr_context_set(context, "value", 12.0);
    cxpr_context_set_param(context, "limit", 10.0);

    bool accepted = false;
    if (!expression || !cxpr_expr_compiled_eval_bool(
            expression, context, registry, &accepted, &error)) {
        fprintf(stderr, "cxpr: %s\n", error.message ? error.message : "error");
    }

    printf("accepted=%s\n", accepted ? "true" : "false");

    cxpr_expr_compiled_free(expression);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_context_free(context);
    cxpr_expr_parser_free(parser);
    return accepted ? 0 : 1;
}
```

Runtime names such as `value` come from `cxpr_context`. Names prefixed with
`$`, such as `$limit`, are parameters. Values can be numbers, booleans,
strings, arrays, nulls, or struct-like records.

Hosts can also register ordinary C callbacks as expression functions:

```c
static double clamp(double x, double low, double high) {
    return x < low ? low : x > high ? high : x;
}

cxpr_registry_add_ternary(registry, "clamp", clamp);
```

The expression `clamp(value, 0, 100)` can then use that function like a
built-in. Callback registration also supports variable arity, typed values,
user data, and cleanup callbacks.

## 2. Describe a stateful model

A `.cxpr` model groups its public contract and calculations in one file:

```cxpr
model accumulator

in { sample, enabled }
$scale = 1.0

state {
    total = 0
}

scaled = sample * $scale
next_total = enabled ? total + scaled : total
total := next_total

out { scaled, total }
```

State updates are atomic: expressions in a tick see the old state, and staged
updates become visible on the next tick. This makes results independent of
statement order.

The reference runtime is useful for dynamic models, editor tooling, tests, and
backend parity checks:

```c
cxpr_error error = {0};
cxpr_model* model = cxpr_model_parse(source, &error);
cxpr_model_compiled* program =
    model ? cxpr_model_compile(model, NULL, &error) : NULL;
cxpr_model_session* session =
    program ? cxpr_model_session_new(program, NULL, &error) : NULL;

if (session) {
    cxpr_context* context = cxpr_model_session_context(session);
    cxpr_context_set(context, "sample", 2.5);
    cxpr_context_set_bool(context, "enabled", true);

    if (cxpr_model_session_tick(program, session, NULL, &error)) {
        double total = 0.0;
        cxpr_model_session_get_number(session, "total", &total);
    }
}

cxpr_model_session_free(session);
cxpr_model_compiled_free(program);
cxpr_model_free(model);
```

Models can also contain named functions, records, imports, lookback, rolling
windows, host-defined metadata, and multiple typed outputs.

## 3. Generate C at build time

For a fixed model, generate a C evaluator during the build:

```sh
cxpr_model_codegen \
  --model accumulator.cxpr \
  --output accumulator.gen.c \
  --function accumulator_tick
```

Generated models expose an ordered descriptor for inputs, parameters, outputs,
state size, tick, and reset. The consuming host passes arrays and owns the
state memory:

```c
const cxpr_generated_model_descriptor* model =
    &accumulator_tick_descriptor;

void* state = calloc(1, model->state_size());
double inputs[] = {2.5, 1.0};
double params[] = {1.0};
double outputs[2] = {0};

if (cxpr_generated_model_descriptor_abi_valid(model)) {
    model->tick(state, inputs, params, outputs);
}

free(state);
```

Use the descriptor's published names and type metadata instead of assuming
array order in generic hosts. Generated C avoids parsing and IR execution in
the deployed application while retaining the model as the source of truth.

## Ownership boundary

`cxpr` intentionally does not fetch data, run an event loop, control devices,
send messages, or decide what an output means. A host typically does four
things:

1. Load or compile a model.
2. Bind application data to declared inputs and parameters.
3. Trigger evaluation at the appropriate time.
4. Interpret outputs and perform side effects.

Keeping side effects outside the model makes the calculations portable,
testable, and suitable for more than one execution backend.

## Build and test cxpr

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

`cxpr` requires C11 and CMake 3.20 or newer. See the main [README](README.md)
for the full language and API reference, and [examples](examples/README.md) for
larger runnable integrations.

MIT licensed. See [LICENSE](LICENSE).
