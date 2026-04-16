# cxpr

[![CI](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml/badge.svg)](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

`cxpr` is a C11 library for runtime expression evaluation. Given an expression string such as
`"rsi < 30 and volume > $min_volume"`, it parses it into an AST, evaluates it against a
context of variables and parameters, and optionally compiles it to an IR for repeated
execution without re-parsing.

It supports numbers, booleans, struct-like values, custom C callbacks, and
expression-defined functions. A named-expression evaluator manages sets of interdependent
expressions with automatic topological ordering and cycle detection.

No external dependencies. C11 required.

## What The Library Provides

- A parser that turns expression strings into an AST
- Evaluation of numbers, booleans, and struct-like values
- A compiled execution path for expressions you run many times
- A context API for variables, `$params`, and named structs
- A registry for C callbacks, built-in math functions, and expression-defined functions
- Expression evaluation with dependency ordering and cycle detection
- AST analysis for references, parameters, functions, and result shape
- Structured errors with source position information

Use `cxpr` when you need an embeddable expression evaluator in plain C without bringing in a scripting runtime.

## Core Concepts

- `cxpr_parser`: parses source text into an AST
- `cxpr_context`: holds runtime variables, params, and structs
- `cxpr_registry`: holds built-in and custom functions
- `cxpr_program`: compiled form for hot paths
- `cxpr_evaluator`: manages named expressions and their dependencies

## Building and Testing

```bash
cmake --preset default          # configure (Release)
cmake --build --preset default  # build
ctest --preset default          # run tests
```

Additional presets:

| Preset   | Purpose                              |
| -------- | ------------------------------------ |
| `strict` | Strict compiler warnings (`-Werror`) |
| `asan`   | AddressSanitizer (Debug)             |
| `ubsan`  | UndefinedBehaviorSanitizer (Debug)   |

## Installation

### As a subdirectory

```cmake
add_subdirectory(libs/cxpr)
target_link_libraries(my_target PRIVATE cxpr::cxpr)
```

Tests are built by default when `cxpr` is the top-level project.
When embedding `cxpr`, enable them explicitly with `-DCXPR_BUILD_TESTS=ON`.

### Via FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(cxpr
    GIT_REPOSITORY https://github.com/oyvindg/cxpr.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(cxpr)
target_link_libraries(my_target PRIVATE cxpr::cxpr)
```

### Via `find_package`

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

```cmake
find_package(cxpr CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE cxpr::cxpr)
```

## Quick Start

```c
#include <cxpr/cxpr.h>
#include <stdio.h>

// 1. Define some functions the expression can call.
// deg2rad converts degrees to radians.
static double deg2rad(double d) {
    return d * 3.14159265358979323846 / 180.0;
}

// clamp keeps a value inside the inclusive [lo, hi] interval.
static double clamp(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

// within_limit returns true when the first argument is below the second.
static cxpr_value within_limit(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_fv_bool(args[0] < args[1]);
}

int main(void) {
    // 2. Create the core cxpr objects:
    // parser parses source text into an AST,
    // ctx stores runtime variables and $params,
    // reg holds built-in and custom functions.
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();

    // Register the built-in standard library functions like sqrt, abs, min, and max.
    cxpr_register_defaults(reg);

    // 3. Populate runtime data.
    // angle_deg is a normal context variable referenced as angle_deg in expressions.
    cxpr_context_set(ctx, "angle_deg", 120.0);

    // limit is a parameter referenced as $limit in expressions.
    cxpr_context_set_param(ctx, "limit", 1.2);

    // 4. Register those functions under the names used in the expression.
    cxpr_registry_add_unary(reg, "deg2rad", deg2rad);
    cxpr_registry_add_ternary(reg, "clamp", clamp);
    cxpr_registry_add_value(reg, "within_limit", within_limit, 2, 2, NULL, NULL);

    cxpr_error err = {0};
    // 5. Parse an expression that converts angle_deg, clamps it, and checks $limit.
    cxpr_ast* ast = cxpr_parse(parser,
        "within_limit(clamp(deg2rad(angle_deg), 0.0, 1.57), $limit)",
        &err);
    if (!ast) {
        fprintf(stderr, "parse error at %zu:%zu: %s\n", err.line, err.column, err.message);

        // Free the core objects before returning on parse error.
        cxpr_parser_free(parser);
        cxpr_context_free(ctx);
        cxpr_registry_free(reg);
        return 1;
    }

    // 6. Evaluate the parsed AST with the current context and parameters.
    bool result = false;
    if (!cxpr_eval_ast_bool(ast, ctx, reg, &result, &err)) {
        fprintf(stderr, "eval error at %zu:%zu: %s\n", err.line, err.column, err.message);

        // Free anything already created before returning on error.
        cxpr_ast_free(ast);
        cxpr_parser_free(parser);
        cxpr_context_free(ctx);
        cxpr_registry_free(reg);
        return 1;
    }

    printf("result = %s\n", result ? "true" : "false");

    // 7. Free the AST and the core cxpr objects when you are done.
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    return 0;
}
```

Compile once when the same expression will be evaluated many times:

```c
// 1. Compile the AST into a reusable program.
cxpr_program* prog = cxpr_compile(ast, reg, &err);

// 2. Evaluate the compiled program with the current context and registry.
bool result = false;
if (!cxpr_eval_program_bool(prog, ctx, reg, &result, &err)) {
    // Handle error.
}

// 3. Free the compiled program when you are done with it.
cxpr_program_free(prog);
```

## Expression Language

Examples:

```text
(a + b) * c / d
sqrt(x^2 + y^2)
rsi < 30 and volume > $min_volume
signal > $threshold ? 1.0 : 0.0
body.position.x + body.velocity.x
```

Supported language features:

- Arithmetic: `+`, `-`, `*`, `/`, `%`, `^`, `**`
- Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`
- Range membership: `x in [10, 20]`, `x not in [min=10, max=20]`
- Logic: `and`, `or`, `not`, `&&`, `||`, `!`
- Ternary: `condition ? a : b`
- Function calls: `sqrt(x)`, `clamp(v, lo, hi)`
- Forward pipe: `x |> f |> g(1)` (desugars to `g(f(x), 1)`, RHS must be callable)
- Params with `$` prefix: `$threshold`
- Field access for named structs and produced structs: `quote.mid`, `body.velocity.x`
- Postfix lookback syntax: `close[1]`, `macd(12, 26, 9).signal[2]`

## Lookback Evaluation

`cxpr` represents postfix lookbacks as native AST nodes. The public syntax is `expr[n]`;
there is no `lag_*` language fallback in the evaluator.

To evaluate lookbacks at runtime, install a registry lookback resolver:

```c
cxpr_registry_set_lookback_resolver(reg, my_lookback_resolver, my_userdata, NULL);
```

Without a resolver, evaluating expressions such as `close[1]` or
`zigzag(0.03).line[1]` fails because `CXPR_NODE_LOOKBACK` must be handled by the host.

## Custom Functions

Register C functions directly before parsing expressions that call them:

```c
// 1. Define some functions the expression can call.
// deg2rad converts degrees to radians.
static double deg2rad(double d) {
    return d * 3.14159265358979323846 / 180.0;
}

// clamp keeps a value inside the inclusive [lo, hi] interval.
static double clamp(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

// within_limit returns true when the first argument is below the second.
static cxpr_value within_limit(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_fv_bool(args[0] < args[1]);
}

// 2. Register those functions under the names used in the expression.
cxpr_registry_add_unary(reg, "deg2rad", deg2rad);
cxpr_registry_add_ternary(reg, "clamp", clamp);
cxpr_registry_add_value(reg, "within_limit", within_limit, 2, 2, NULL, NULL);

// 3. Parse a pipe-style expression with the same steps as the nested-call example.
// This reads left-to-right:
// angle_deg -> deg2rad(...) -> clamp(..., 0.0, 1.57) -> within_limit(..., $limit)
cxpr_ast* ast = cxpr_parse(parser,
    "angle_deg |> deg2rad |> clamp(0.0, 1.57) |> within_limit($limit)",
    &err);
```

Or define functions in the expression language itself:

```c
cxpr_error err = cxpr_registry_define_fn(reg, "sq(x) => x * x");
err = cxpr_registry_define_fn(reg, "hyp2(a, b) => sqrt(sq(a) + sq(b))");
```

## Expression Evaluator

The expression evaluator is for named expressions that depend on each other.
It parses the set, validates references, resolves evaluation order, and reports cycles.

```c
// 1. Create an evaluator for a set of named expressions.
cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);

// 2. Define expressions that can reference variables, params, and each other.
const cxpr_expression_def defs[] = {
    { "wide",  "spread > $threshold" },
    { "entry", "wide and mid > $min_mid" },
    { "score", "mid + spread * 10" },
};

// 3. Add the expressions and compile them into dependency order.
cxpr_error err = {0};
if (!cxpr_expressions_add(evaluator, defs, 3, &err)
    || !cxpr_evaluator_compile(evaluator, &err)) {
    fprintf(stderr, "error: %s\n", err.message);

    // Free the evaluator before returning on error.
    cxpr_evaluator_free(evaluator);
    return 1;
}

// 4. Evaluate all compiled expressions against the current context.
cxpr_evaluator_eval(evaluator, ctx, &err);

// 5. Read back results by expression name.
// Use cxpr_expression_get(...) when you want the raw cxpr_value.
// The returned value is borrowed from the evaluator; if it is a struct result,
// do not free it yourself and do not keep it after freeing or re-evaluating the evaluator.
cxpr_value raw_entry = cxpr_expression_get(evaluator, "entry", NULL);
bool   entry = cxpr_expression_get_bool(evaluator,   "entry", NULL);
double score = cxpr_expression_get_double(evaluator, "score", NULL);

// 6. Free the evaluator when you are done.
cxpr_evaluator_free(evaluator);
```

`cxpr_evaluator_eval` writes results back into `ctx` and also makes them available via
`cxpr_expression_get`, `cxpr_expression_get_double`, and `cxpr_expression_get_bool`.

## Errors

All public functions that can fail accept a `cxpr_error*` output parameter (pass `NULL` to
ignore). On failure, `err.code` is one of:

| Code                           | Meaning                                |
| ------------------------------ | -------------------------------------- |
| `CXPR_ERR_SYNTAX`              | Malformed expression or bad argument   |
| `CXPR_ERR_UNKNOWN_IDENTIFIER`  | Variable or parameter not in context   |
| `CXPR_ERR_UNKNOWN_FUNCTION`    | Function not found in registry         |
| `CXPR_ERR_WRONG_ARITY`         | Wrong number of arguments              |
| `CXPR_ERR_DIVISION_BY_ZERO`    | Division or modulo by zero             |
| `CXPR_ERR_CIRCULAR_DEPENDENCY` | Cycle in named-expression dependencies |
| `CXPR_ERR_TYPE_MISMATCH`       | Value type incompatible with operation |
| `CXPR_ERR_OUT_OF_MEMORY`       | Allocation failure                     |

`err.message`, `err.line`, `err.column`, and `err.position` give further detail.

## Analysis

`cxpr` can inspect an expression before execution:

- Which variables and `$params` it uses
- Which functions it calls
- Whether the result is numeric, boolean, or struct-like
- Whether it can short-circuit
- Whether there are unknown functions or invalid references

## Examples

Longer integration examples are in [examples/README.md](examples/README.md).

## Benchmark

Build and run the benchmark like this:

```bash
cmake -S . -B build -DCXPR_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release && \
cmake --build build --target cxpr_bench_ir && \
./build/benchmarks/cxpr_bench_ir
```

Use `-DCMAKE_BUILD_TYPE=Release` when benchmarking for meaningful timings.
