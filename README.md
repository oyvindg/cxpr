# cxpr

[![CI](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml/badge.svg)](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

`cxpr` is a C11 library for runtime expression evaluation. Given an expression string such as
`"rsi < 30 and volume > $min_volume"`, it parses it into an AST, evaluates it against a
context of variables and parameters, and optionally compiles it to an IR for repeated
execution without re-parsing.

It supports numbers, booleans, struct-like values, custom C callbacks, and
expression-defined functions. A named-expression evaluator manages sets of interdependent
expressions with automatic topological ordering and cycle detection. Host integrations can
also expose provider metadata, scoped sources, runtime-resolved series, and source plans for
bar-by-bar materialization outside the expression engine.

No external dependencies. C11 required.

## What The Library Provides

- A parser that turns expression strings into an AST
- Evaluation of numbers, booleans, and struct-like values
- AST and typed IR execution paths for expressions you run many times
- A context API for variables, `$params`, named structs, cached structs, overlays, slots, and prehashed updates
- A registry for scalar, typed, AST-level, time-series, struct-producing, built-in, basket, and expression-defined functions
- Expression evaluation with dependency ordering and cycle detection
- AST analysis for references, parameters, functions, producer fields, result shape, and short-circuit behavior
- Provider metadata for host-backed functions, named arguments, record fields, scoped series, and direct sources
- Runtime-call and source-plan helpers for host integrations that materialize series data outside `cxpr`
- Structured errors with source position information

Use `cxpr` when you need an embeddable expression evaluator in plain C without bringing in a scripting runtime.

## Core Concepts

- `cxpr_parser`: parses source text into an AST
- `cxpr_context`: holds runtime variables, params, and structs
- `cxpr_registry`: holds built-in and custom functions
- `cxpr_program`: compiled form for hot paths
- `cxpr_evaluator`: manages named expressions and their dependencies
- `cxpr_struct_value`: owned struct with named fields, used by contexts and callbacks
- `cxpr_provider`: describes host-backed function/source inventories
- `cxpr_source_plan_ast`: parsed provider source tree with canonical identity and bound runtime arguments
- `cxpr_scope_resolver`: host callback used by low-level scoped-source runtime adapters

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

// 3. Dump the compiled IR for debugging.
cxpr_program_dump(prog, stdout);

// 4. Free the compiled program when you are done with it.
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
- Named arguments for calls that preserve argument names in the AST/provider path:
  `close(timeframe="1d")`, `macd(fast=12, slow=26, signal=9).hist`
- Forward pipe: `x |> f |> g(1)` (desugars to `g(f(x), 1)`, RHS must be callable)
- Params with `$` prefix: `$threshold`
- Field access for named structs and produced structs: `quote.mid`, `body.velocity.x`
- String literals: `"1d"`, used as named-argument values such as `close(timeframe="1d")`
- Postfix lookback syntax: `close[1]`, `macd(12, 26, 9).signal[2]`

## Values, Structs, and Contexts

Runtime values are typed as `CXPR_VALUE_NUMBER`, `CXPR_VALUE_BOOL`, or
`CXPR_VALUE_STRUCT`. Use `cxpr_fv_double`, `cxpr_fv_bool`, and
`cxpr_fv_struct` when returning typed values from callbacks.

Contexts hold normal variables, `$params`, and named struct values:

```c
cxpr_context_set(ctx, "close", 101.5);
cxpr_context_set_bool(ctx, "market_open", true);
cxpr_context_set_param(ctx, "threshold", 0.8);

const char* fields[] = {"bid", "ask"};
cxpr_value values[] = {cxpr_fv_double(101.4), cxpr_fv_double(101.6)};
cxpr_struct_value* quote = cxpr_struct_value_new(fields, values, 2);
cxpr_context_set_struct(ctx, "quote", quote);
cxpr_struct_value_free(quote);
```

Additional context operations:

- `cxpr_context_clone` creates an independent copy of a context.
- `cxpr_context_set_fields` sets multiple struct fields at once.
- `cxpr_context_clear` removes all bindings from a context.

For hot loops, prefer the bulk and stable-binding update paths:

- `cxpr_context_set_array` and `cxpr_context_set_param_array` update several values at once.
- `cxpr_context_set_prehashed` and `cxpr_context_set_param_prehashed` reuse hashes from `cxpr_hash_string`.
- `cxpr_context_slot_bind` gives direct mutable access to a context value slot.
- `cxpr_context_overlay_new` creates a context that reads through to a parent and can override selected bindings.
- `cxpr_context_set_cached_struct` stores per-evaluation struct results; clear them with `cxpr_context_clear_cached_structs`.

Slot binding example for a tight evaluation loop:

```c
// Pre-bind slots once after the context is populated.
cxpr_context_set(ctx, "close", 0.0);
cxpr_context_set(ctx, "volume", 0.0);

cxpr_context_slot close_slot, volume_slot;
cxpr_context_slot_bind(ctx, "close", &close_slot);
cxpr_context_slot_bind(ctx, "volume", &volume_slot);

// In the hot loop, update through slots instead of by name.
for (size_t i = 0; i < bar_count; i++) {
    cxpr_context_slot_set(&close_slot, bars[i].close);
    cxpr_context_slot_set(&volume_slot, bars[i].volume);

    bool result = false;
    cxpr_eval_program_bool(prog, ctx, reg, &result, NULL);
    // ...
}
```

## Lookback Evaluation

`cxpr` represents postfix lookbacks as native AST nodes. The public syntax is `expr[n]`.

To evaluate lookbacks at runtime, install a registry lookback resolver:

```c
cxpr_registry_set_lookback_resolver(reg, my_lookback_resolver, my_userdata, NULL);
```

Without a resolver, evaluating expressions such as `close[1]` or
`zigzag(threshold=0.03).line[1]` fails because `CXPR_NODE_LOOKBACK` must be handled by the host.

The offset helpers let a host evaluate ASTs relative to an external series cursor:

```c
cxpr_value value = {0};
if (!cxpr_eval_ast_at_offset(ast, 3, ctx, reg, &value, &err)) {
    // Handle error.
}
```

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

The registry has several callback tiers:

- `cxpr_registry_add`, `cxpr_registry_add_unary`, `cxpr_registry_add_binary`,
  `cxpr_registry_add_ternary`, and `cxpr_registry_add_nullary` register scalar numeric callbacks.
- `cxpr_registry_add_value` registers callbacks that return a typed `cxpr_value`.
- `cxpr_registry_add_typed` accepts typed arguments and declares a typed return value.
- `cxpr_registry_add_ast` receives the original call AST and can evaluate arguments itself.
- `cxpr_registry_add_ast_overlay` is like `add_ast` but evaluates in an overlay context.
- `cxpr_registry_add_timeseries` is the semantic wrapper for AST-level time-series functions.
- `cxpr_registry_add_fn` registers a struct-aware scalar function with metadata.
- `cxpr_registry_add_struct` registers struct producers that expose fields through
  `producer(...).field` and can also return the whole struct.
- `cxpr_registry_set_param_names` attaches stable parameter names for introspection and
  named-argument aware host integration.

Register a struct producer so expressions can write `bb(close, 20, 2.0).upper`:

```c
// A Bollinger Bands producer that writes upper, middle, lower into out[].
static void bb_producer(const double* args, size_t argc,
                        cxpr_value* out, size_t field_count, void* userdata) {
    (void)argc; (void)userdata;
    double close = args[0], period = args[1], mult = args[2];
    double mid = close;          // simplified for illustration
    double band = mult * period; // placeholder for real stddev logic
    out[0] = cxpr_fv_double(mid + band); // upper
    out[1] = cxpr_fv_double(mid);        // middle
    out[2] = cxpr_fv_double(mid - band); // lower
}

const char* bb_fields[] = {"upper", "middle", "lower"};
cxpr_registry_add_struct(reg, "bb", bb_producer, 3, 3, bb_fields, 3, NULL, NULL);

// Now expressions like bb(close, 20, 2.0).upper evaluate through the producer.
```

`cxpr_register_defaults` installs standard math helpers such as `sqrt`, `abs`, `min`, and
`max`. `cxpr_register_basket_builtins` installs basket aggregate helpers used by Dynasty-style
multi-symbol expressions. Use `cxpr_basket_is_builtin`, `cxpr_basket_is_aggregate_function`,
`cxpr_ast_uses_basket_aggregates`, and `cxpr_expression_uses_basket_aggregates` when a host
needs to detect those aggregate forms before execution.

## Providers and Host-Backed Sources

Provider metadata describes functions and direct sources that live in the host application.
`cxpr` uses this metadata to register parse-time signatures, preserve named arguments,
describe record fields, and decode scoped series selectors.

Important provider pieces:

- `cxpr_provider_fn_spec` describes a host-backed function, its numeric arity, optional source
  input shape, named parameters, output fields, primary field, flags, and scope selector.
- `cxpr_provider_source_spec` describes a direct source such as `close`, `temperature`, or
  `requests`.
- `cxpr_provider_scope_spec` describes optional named selectors. Trading providers usually
  use `timeframe`; generic providers may use names such as `selector`, `region`, or another
  provider-specific partition key.
- `cxpr_host_config` supplies runtime scalar resolution and optional hooks for arity overrides,
  source descriptor filtering, and scoped-source error reporting.

Register provider signatures like this:

```c
cxpr_host_config host = {
    .runtime_required_scalar = my_scalar_resolver,
    .userdata = my_host_state,
};

cxpr_register_provider_signatures(reg, &provider, &host);
```

Runtime-call helpers turn AST call nodes into a small host-neutral view:

```c
cxpr_runtime_call call = {0};
if (cxpr_parse_runtime_call_provider(&provider, call_ast, &call)) {
    // call.name, call.field_name, call.scope_value, and call.value_arg_count are borrowed.
}
```

Use `cxpr_provider_runtime_call_arg` and `cxpr_provider_eval_runtime_call_number_args` to bind
or evaluate value arguments while excluding named selector arguments. Use
`cxpr_resolve_expression_scope` when a host needs to find the first provider-declared scoped
call inside a larger expression.

## Source Plans and Scoped Sources

Source plans parse provider source expressions into a host-materializable tree. They are useful
when expressions such as `close`, `ema(close, 14)`, `ema(close(timeframe="1d"), 14)[2]`, or an
arbitrary nested source expression must be evaluated bar-by-bar outside `cxpr`.

```c
cxpr_source_plan_ast plan = {0};
if (cxpr_parse_provider_source_plan_ast(&provider, ast, &plan)) {
    // plan.root describes the source tree.
    // plan.canonical is an owned canonical rendering.
    // plan.bound_arg_asts contains borrowed ASTs for runtime numeric arguments.
    cxpr_free_source_plan_ast(&plan);
}
```

Evaluate a plan's bound numeric arguments with `cxpr_eval_source_plan_bound_args`.
Each source-plan node has a stable `node_id`, kind, name, optional field, optional
`scope_value`, bound argument slots, optional lookback slot, and optional child source.
Node kinds are `CXPR_SOURCE_PLAN_FIELD` (direct source), `CXPR_SOURCE_PLAN_INDICATOR`
(host function), `CXPR_SOURCE_PLAN_SMOOTHING` (post-processing wrapper), and
`CXPR_SOURCE_PLAN_EXPRESSION` (arbitrary expression fallback).

For lower-level runtime registration, `cxpr_scoped_source_functions_register` exposes direct
source names in a registry and delegates value lookup to a host callback:

```c
static const cxpr_provider_scope_spec scope = {"timeframe", true};
static const cxpr_scoped_source_spec sources[] = {
    {"close", 0, 1, &scope},
};

cxpr_scope_resolver resolver = {
    .resolve = my_source_resolver,
    .userdata = my_series_store,
};

cxpr_scoped_source_functions_register(reg, sources, 1, &resolver, NULL);
```

See [examples/scoped_sources.md](examples/scoped_sources.md) for a runnable scoped-source
example.

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

Use `cxpr_expression_eval_order` to inspect the resolved dependency order after compilation.
`cxpr_analyze_expressions` runs static analysis across all expressions in the evaluator.

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
- Which producer fields are referenced
- Whether the result is numeric, boolean, or struct-like
- Whether it can short-circuit
- Whether there are unknown functions or invalid references

The lower-level AST API also exposes constructors, accessors, printers, and reference
collection helpers:

- `cxpr_ast_to_string` and `cxpr_ast_dump` render parsed trees.
- `cxpr_ast_references`, `cxpr_ast_variables_used`, and `cxpr_ast_functions_used` collect names.
- `cxpr_ast_producer_fields_used` reports record-field dependencies.
- `cxpr_analyze` and `cxpr_analyze_expr` validate expression shape against a registry.

```c
// Analyze an expression to discover its shape and dependencies.
cxpr_analysis info = {0};
cxpr_error err = {0};
if (cxpr_analyze_expr("rsi < 30 and volume > $min_volume", reg, &info, &err)) {
    printf("result type:  %s\n", info.result_type == CXPR_EXPR_BOOL ? "bool" : "number");
    printf("uses params:  %s\n", info.uses_parameters ? "yes" : "no");
    printf("can short-circuit: %s\n", info.can_short_circuit ? "yes" : "no");
    printf("references:   %zu\n", info.reference_count);   // rsi, volume
    printf("parameters:   %zu\n", info.parameter_count);   // min_volume
}

// Collect the actual names used in a parsed AST.
cxpr_ast* ast = cxpr_parse(parser, "ema_fast > ema_slow and rsi < $limit", &err);
const char* refs[8];
size_t n = cxpr_ast_references(ast, refs, 8);      // ema_fast, ema_slow, rsi
const char* params[8];
size_t p = cxpr_ast_variables_used(ast, params, 8); // limit
const char* fns[8];
size_t f = cxpr_ast_functions_used(ast, fns, 8);    // (none here)
```

## Examples

Longer integration examples are in [examples/README.md](examples/README.md).

## Benchmark

Build and run the benchmark like this:

```bash
cmake -S . -B build -DCXPR_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release && \
cmake --build build --target cxpr_bench_ir && \
./build/benchmarks/cxpr_bench_ir
```

When `cxpr` is built as a subdirectory from the Dynasty repo root, the benchmark binary is at
`./build/libs/cxpr/benchmarks/cxpr_bench_ir`.

Use `-DCMAKE_BUILD_TYPE=Release` when benchmarking for meaningful timings.

Example output from `./build/benchmarks/cxpr_bench_ir`:

```text
cxpr AST vs IR benchmark

Scalar
case                     iters   AST ns/eval    IR ns/eval   speedup
simple_arith            500000         58.03         48.05      1.21x
nested_expr             400000         97.82         79.35      1.23x
function_call           250000        154.72         59.49      2.60x
defined_fn              200000        247.60         50.06      4.95x
native_fn               200000        125.59         51.82      2.42x
defined_chain           120000        292.27         73.57      3.97x
native_chain            120000        145.80         64.50      2.26x
mixed_chain             120000        208.75         70.90      2.94x
deep_defined             80000        272.29         77.85      3.50x
deep_native              80000        372.05         68.13      5.46x
context_churn           200000        132.41        112.79      1.17x

Typed Struct
case                     iters   AST ns/eval    IR ns/eval   speedup
producer_field          150000        129.96         89.84      1.45x
producer_struct         150000        315.61         66.42      4.75x

IR-only
case                     iters   AST ns/eval    IR ns/eval   speedup
context_slot            200000             -         88.44         -

Context Update Paths
case                     iters     set ns/op     alt ns/op   speedup
base_array              500000        101.45         86.73      1.17x
mutate_array            500000         45.70         39.85      1.15x
mutate_prehashed        500000         45.70         32.57      1.40x
mutate_slot             500000         45.70         10.79      4.24x

Param Update Paths
case                     iters     set ns/op     alt ns/op   speedup
base_param_array        500000         61.62         57.80      1.07x
mutate_param_array      500000         62.39         58.10      1.07x
mutate_param_hash       500000         62.39         50.34      1.24x
sink=66507643.882822
```

These numbers are machine-dependent, but they show the expected shape: IR evaluation
is usually faster than AST evaluation, and slot/prehashed update paths help the hottest loops.
