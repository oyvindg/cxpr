# cxpr

[![CI](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml/badge.svg)](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

`cxpr` is a small C11 library for runtime expressions.
It parses expressions, evaluates them against a context, and optionally compiles them for repeated execution.
It also includes an expression evaluator for named expressions with dependency resolution.

No external dependencies.

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

## Installation

### As a subdirectory

```cmake
add_subdirectory(libs/cxpr)
target_link_libraries(my_target PRIVATE cxpr::cxpr)
```

Tests are only built when `cxpr` is the top-level project.

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

static double deg2rad(double d) {
    return d * 3.14159265358979323846 / 180.0;
}

static double clamp(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static cxpr_value within_limit(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_fv_bool(args[0] < args[1]);
}

int main(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_context_set(ctx, "angle_deg", 120.0);
    cxpr_context_set_param(ctx, "limit", 1.2);

    cxpr_registry_add_unary(reg, "deg2rad", deg2rad);
    cxpr_registry_add_ternary(reg, "clamp", clamp);
    cxpr_registry_add_value(reg, "within_limit", within_limit, 2, 2, NULL, NULL);

    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser,
        "within_limit(clamp(deg2rad(angle_deg), 0.0, 1.57), $limit)",
        &err);
    if (!ast) {
        fprintf(stderr, "parse error at %zu:%zu: %s\n", err.line, err.column, err.message);
        return 1;
    }

    bool result = false;
    if (!cxpr_eval_ast_bool(ast, ctx, reg, &result, &err)) {
        fprintf(stderr, "eval error at %zu:%zu: %s\n", err.line, err.column, err.message);
        cxpr_ast_free(ast);
        cxpr_parser_free(parser);
        cxpr_context_free(ctx);
        cxpr_registry_free(reg);
        return 1;
    }

    printf("result = %s\n", result ? "true" : "false");

    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    return 0;
}
```

Compile once when the same expression will be evaluated many times:

```c
cxpr_program* prog = cxpr_compile(ast, reg, &err);

bool result = false;
if (!cxpr_eval_program_bool(prog, ctx, reg, &result, &err)) {
    /* handle error */
}

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
- Logic: `and`, `or`, `not`, `&&`, `||`, `!`
- Ternary: `condition ? a : b`
- Function calls: `sqrt(x)`, `clamp(v, lo, hi)`
- Params with `$` prefix: `$threshold`
- Field access for named structs and produced structs: `quote.mid`, `body.velocity.x`

## Custom Functions

Register C functions directly before parsing expressions that call them:

```c
static double deg2rad(double d) {
    return d * 3.14159265358979323846 / 180.0;
}

static double clamp(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static cxpr_value within_limit(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_fv_bool(args[0] < args[1]);
}

cxpr_registry_add_unary(reg, "deg2rad", deg2rad);
cxpr_registry_add_ternary(reg, "clamp", clamp);
cxpr_registry_add_value(reg, "within_limit", within_limit, 2, 2, NULL, NULL);

cxpr_ast* ast = cxpr_parse(parser,
    "within_limit(clamp(deg2rad(angle_deg), 0.0, 1.57), $limit)",
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
cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);

const cxpr_expression_def defs[] = {
    { "wide", "spread > $threshold" },
    { "entry", "wide and mid > $min_mid" },
    { "score", "mid + spread * 10" }
};

cxpr_error err = {0};
cxpr_expressions_add(evaluator, defs, 3, &err);
cxpr_evaluator_compile(evaluator, &err);
cxpr_evaluator_eval(evaluator, ctx, &err);
```

## Analysis And Errors

`cxpr` can inspect an expression before execution:

- Which variables and `$params` it uses
- Which functions it calls
- Whether the result is numeric, boolean, or struct-like
- Whether it can short-circuit
- Whether there are unknown functions or invalid references

Failures return `cxpr_error` with code, message, line, column, and byte position.

## Examples

Longer integration examples are in [examples/README.md](examples/README.md).

## Benchmark

Build and run the benchmark like this:

```bash
cmake -S . -B build -DCXPR_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release /
cmake --build build --target cxpr_bench_ir /
./build/benchmarks/cxpr_bench_ir
```

Use `-DCMAKE_BUILD_TYPE=Release` when benchmarking for meaningful timings.
