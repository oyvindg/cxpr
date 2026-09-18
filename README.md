# cxpr

[![CI](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml/badge.svg)](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

`cxpr` is an embeddable expression and model language for C. You write formulas
and small stateful models as portable `.cxpr` text; the library parses,
validates, type-checks, and evaluates them at runtime — or generates C for a
fixed deployment — so application logic lives in editable source instead of
recompiled code.

It is a standalone C11 library with no external runtime dependencies, and it
assumes nothing about the application that embeds it. The host owns data,
timing, and I/O; `cxpr` owns the language, validation, execution, state
handling, and optional code generation.

```text
host data -> cxpr expression or model -> typed outputs -> host action
```

The core is domain-independent. The same language can describe a control rule,
a scientific calculation, a game entity, an SLO condition, or a trading signal
without adding those domains to `cxpr`:

```cxpr
# robotics
stable = imu_ok and abs(roll) <= $max_roll

# operations
within(latency_ms, 0, $budget_ms) and error_rate < $max_error_rate

# trading
close > ema(close, 20) and volume > $min_volume
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

## Status and scope

`cxpr` currently provides:

- a lexer and parsers, public expression-AST constructors/accessors, and a
  read-only document-AST view;
- numeric, boolean, string, array, and struct-like values;
- variables, `$parameters`, named fields, custom functions, and host-backed
  sources;
- expression analysis, type checking, tree evaluation, and typed IR execution;
- `.cxpr` model parsing, validation, imports, state, lookback planning,
  compilation, sessions, and C code generation;
- a higher-level tick engine with source hydration, history, roles, watches,
  and transition events;
- artifact plugins for C, CUDA, graph JSON, and metadata JSON;
- a stable descriptor ABI for generated C model evaluators.

The following boundaries are intentional:

- `cxpr` does not fetch data, place orders, control hardware, or define
  application policy.
- CUDA support emits CUDA source. Compiling, loading, launching, and scheduling
  kernels belong to the host.
- The public IR view is for inspection, not bytecode serialization or mutable
  IR construction.
- The plugin API routes artifact bytes through host callbacks. It does not
  prescribe files, caches, package formats, or build systems.

## Architecture

```text
expression source
  -> expression AST
  -> analysis + typecheck
  -> tree evaluator or typed IR program

.cxpr source
  -> document AST
  -> semantic model
  -> compiled model program
  -> model session / generated C / artifact plugin
```

The main types are:

| Type | Purpose |
| --- | --- |
| `cxpr_expr_ast` | Parsed expression tree |
| `cxpr_doc_ast` | Source-oriented tree for a complete `.cxpr` document |
| `cxpr_doc` | Parsed document containing host blocks and optionally a model |
| `cxpr_model` | Validated semantic representation of model declarations |
| `cxpr_expr_compiled` | Compiled typed IR for one expression |
| `cxpr_model_compiled` | Immutable compiled plan for a complete model |
| `cxpr_context` | Runtime variables, parameters, structs, slots, and overlays |
| `cxpr_registry` | Built-in, expression-defined, and host-defined functions |
| `cxpr_model_session` | Mutable state for one execution of a model program |
| `cxpr_engine_program` | Immutable plan for the optional rule-engine layer |
| `cxpr_engine_session` | Mutable cursor, history, source, and watch state |

## Choose the smallest API that fits

| Need | API |
| --- | --- |
| Evaluate one formula | Expression API |
| Re-evaluate a formula efficiently | Compiled expression (typed IR) |
| Named inputs, state, functions, or several outputs | Model API |
| Rising/falling events over a sequence | Engine API |
| Fixed model with minimal runtime work | Generated C |
| Run one generated model over host-owned buffers | Bulk API |
| Metadata, graph, debug map, or CUDA source | Plugin API |

## Public API documentation

This README is an overview and introduction. The [public API index](docs/api/README.md)
is the code-aligned reference for the installed C headers and the `.cxpr`
language, audited against the sources; where this README conflicts with an
installed declaration, the public header and its implementation are
authoritative.

| Goal | Documentation |
| --- | --- |
| Author `.cxpr` expressions and models | [Language reference](docs/api/language.md) |
| Embed `.cxpr` in an application or service | [Host embedding guide](docs/api/embedding.md) |
| Parse, inspect, or execute expressions | [Expressions](docs/api/expressions.md) and [execution](docs/api/execution.md) |
| Parse, compile, and run complete models | [Documents and models](docs/api/documents-models.md) |
| Run stateful host-driven rules | [Engine](docs/api/engine.md) |
| Deploy generated native evaluators | [Generated C](docs/api/generated-c.md) |
| Execute arrays, grids, and CUDA host kernels | [Bulk and grid](docs/api/bulk.md) |
| Bind host data and explicit resampling | [Providers, sources, and resampling](docs/api/providers-sources-resample.md) |
| Emit C, CUDA, graph, metadata, or debug artifacts | [Plugins and code generation](docs/api/plugins-codegen.md) |
| Check lifetimes and compatibility | [Ownership, threading, and versioning](docs/api/ownership-threading-versioning.md) |
| Tune hot paths | [Performance guidance](docs/api/performance.md) |

See also [cross-domain examples](docs/examples.md) for illustrative models
across robotics, operations, games, science, and trading, and
[provider-backed resampling](docs/resample.md) for fixed-duration series,
requirement manifests, host integration, and timeframe migration.

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
cxpr_value inputs[] = {cxpr_num(2.5), cxpr_bool(true)};
cxpr_value params[] = {cxpr_num(1.0)};
cxpr_value outputs[2] = {cxpr_num(0.0), cxpr_num(0.0)};

if (cxpr_generated_model_descriptor_abi_valid(model)) {
    model->tick(state, inputs, params, outputs);
}

free(state);
```

The complete compilable version is
[`examples/generated_descriptor_host.c`](examples/generated_descriptor_host.c)
and is built and run by CTest.

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

`cxpr` requires C11 and CMake 3.21 or newer. See the [public API index](docs/api/README.md)
for the full language and API reference, and [`examples/`](examples/README.md)
for larger runnable integrations.

## Compatibility

The public library version is defined in `<cxpr/version.h>`.

Separate versioned contracts protect generated or inspected data:

- `CXPR_GENERATED_MODEL_ABI_VERSION` for generated model descriptors;
- `CXPR_IR_VIEW_API_VERSION` for the public IR inspection view;
- stable artifact kind strings for graph and metadata plugin output.

Do not serialize internal AST or IR storage. Persist source, generated C/CUDA,
or explicitly versioned plugin artifacts.

See [`CHANGELOG.md`](CHANGELOG.md) and [`docs/release-notes/`](docs/release-notes)
for breaking changes and migration notes.

## License

MIT. See [`LICENSE`](LICENSE).
