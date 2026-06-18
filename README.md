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

## Contents

- [What The Library Provides](#what-the-library-provides)
- [Core Concepts](#core-concepts)
- [Building and Testing](#building-and-testing)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Expression Language](#expression-language)
- [Values, Structs, and Contexts](#values-structs-and-contexts)
- [Context Overlays](#context-overlays)
- [Slot Binding](#slot-binding)
- [Lookback Evaluation](#lookback-evaluation)
- [Custom Functions](#custom-functions)
- [Providers and Host-Backed Sources](#providers-and-host-backed-sources)
- [Source Plans and Scoped Sources](#source-plans-and-scoped-sources)
- [Expression Evaluator](#expression-evaluator)
- [Errors](#errors)
- [Concurrency](#concurrency)
- [Analysis](#analysis)
- [Examples](#examples)
- [Benchmark](#benchmark)

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
- Thread-safe for per-thread evaluation: immutable registries/programs shared, contexts per thread (see [Concurrency](#concurrency))

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

| Preset     | Purpose                                        |
| ---------- | ---------------------------------------------- |
| `strict`   | Strict compiler warnings (`-Werror`)           |
| `asan`     | AddressSanitizer (Debug)                       |
| `ubsan`    | UndefinedBehaviorSanitizer (Debug)             |
| `coverage` | Coverage instrumentation (Debug)               |
| `fuzz`     | libFuzzer targets with ASan/UBSan (Clang only) |

The `fuzz` preset builds a libFuzzer harness that drives untrusted input through
the full parse → compile → evaluate pipeline:

```bash
cmake --preset fuzz                 # configure (needs clang)
cmake --build --preset fuzz         # build
./build-fuzz/fuzz/cxpr_fuzz_parse build-fuzz/fuzz/corpus   # run
```

## Installation

### As a subdirectory

```cmake
add_subdirectory(external/cxpr)
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
static cxpr_value within_limit(const cxpr_value* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_bool(args[0].d < args[1].d);
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
    cxpr_context_set(ctx, "angle_deg", 30.0);

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

Runtime values are typed as `CXPR_VALUE_NUMBER`, `CXPR_VALUE_BOOL`,
`CXPR_VALUE_STRING`, `CXPR_VALUE_NULL`, `CXPR_VALUE_TIMESTAMP`,
`CXPR_VALUE_DURATION`, `CXPR_VALUE_ARRAY`, or `CXPR_VALUE_STRUCT`. Use
`cxpr_num`, `cxpr_bool`, `cxpr_string`, `cxpr_null`, `cxpr_timestamp`,
`cxpr_duration`, `cxpr_array`, and `cxpr_struct` when returning typed values
from callbacks.

Equality supports matching scalar operands across number, bool, string, null,
timestamp, and duration values, so ordinary boolean logic can include checks
such as `region == "EU"`. Cross-type equality still fails with a type mismatch.
Arrays are transport values for callbacks and structs; array literals and deep
array equality are not part of the expression language yet.

Contexts hold normal variables, `$params`, and named struct values:

```c
cxpr_context_set(ctx, "close", 101.5);
cxpr_context_set_bool(ctx, "market_open", true);
cxpr_context_set_string(ctx, "region", "EU");
cxpr_context_set_param(ctx, "threshold", 0.8);

const char* fields[] = {"bid", "ask"};
cxpr_value values[] = {cxpr_num(101.4), cxpr_num(101.6)};
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

## Context Overlays

A context overlay is a lightweight child context created with `cxpr_context_overlay_new`. It
chains to a parent context: every lookup — variables, `$params`, structs, and cached structs —
checks the overlay first, and on a miss falls through to the parent. Writes always stay local
to the overlay, so the parent context is never mutated.

This makes overlays the right tool when you need to evaluate an expression with a few
bindings changed without copying the entire context. Typical use cases:

- **Expression-defined functions** — `cxpr` internally creates an overlay to bind function
  parameters (`sq(x) => x * x`) so they shadow, but do not overwrite, the caller's variables.
- **Scenario evaluation** — test different `$param` values against the same base context.
- **Basket iteration** — evaluate one symbol at a time, overlaying per-symbol data while the
  shared market context stays in the parent.
- **Bar-by-bar evaluation** — in a loop over time-series bars, create an overlay per bar with
  bar-specific fields (e.g. open, high, low, close, volume) while shared parameters and
  configuration stay in the parent.
- **Source remapping** — map a struct prefix like `src.x` into a function parameter like `v.x`.

Context overlays are an evaluation primitive, not expression syntax. They do not select a
timeframe or scope by themselves. Hosts that expose scoped data should represent that in the
expression language through scoped source or indicator calls, for example:

```text
close("1d")
ema(close, 14, "1d")
close(timeframe="1d")
ema(close, 14, timeframe="1d")
```

The host may then use context overlays internally while materializing those scoped series.
For example, a trading host may keep the primary timeframe's bar fields in a base context
and evaluate daily indicator bars in child contexts so daily `close`, `volume`, etc. do not
overwrite the primary values.

**Shared context — works when everything uses the same timeframe:**

```c
// ctx holds 1h bar data set by the framework (close=hourly close, etc.).
for (size_t i = 0; i < bar_count_1h; i++) {
    cxpr_context_set(ctx, "close",     bars_1h[i].close);
    cxpr_context_set(ctx, "volume",    bars_1h[i].volume);
    cxpr_context_set(ctx, "bar_index", (double)i);

    cxpr_eval_ast(indicator_ast, ctx, reg, &out, &err);
    series[i] = out.d;
}
// Simple and fast. But if a second indicator now needs to materialize against
// daily bars, writing bars_1d[j].close into the same ctx overwrites the hourly
// close. After that loop finishes, the hourly close in ctx is gone — any later
// indicator that expects hourly data silently reads the last daily value.
```

**Overlay — daily bars shadow the hourly base without destroying it:**

```c
// base holds 1h bar data and shared state (role bindings, params, series refs).
// The parsed expression chose the daily timeframe; overlays only isolate bindings.
for (size_t j = 0; j < bar_count_1d; j++) {
    cxpr_context* bar_ctx = cxpr_context_overlay_new(base);
    cxpr_context_set(bar_ctx, "close",     bars_1d[j].close);
    cxpr_context_set(bar_ctx, "volume",    bars_1d[j].volume);
    cxpr_context_set(bar_ctx, "bar_index", (double)j);

    // Reads role bindings and params from base.
    // Daily close/volume/bar_index stay in bar_ctx.
    cxpr_eval_ast(daily_indicator_ast, bar_ctx, reg, &out, &err);
    daily_series[j] = out.d;

    cxpr_context_free(bar_ctx); // recycled — daily bar state discarded
}
// base still holds the hourly close. The next indicator that materializes
// hourly data — or another indicator on a weekly timeframe — sees the
// original base values intact.
```

### When to use overlays vs. alternatives

The overlay adds ~15 ns per parent fallback lookup (~31 ns for alloc+free with caching).
This cost matters when the base context is not yours to mutate — a framework-owned context
shared across scoped sources, indicators, or evaluation passes. When you own the context
exclusively, direct writes are simpler and faster.

| Approach | Allocation | Lookup | Isolation | Best for |
| -------- | ---------- | ------ | --------- | -------- |
| Shared context | None | Direct | None — all writes visible | Single owner, caller controls all names |
| Overlay | ~31 ns (cached) | +15 ns on fallback | Full — parent read-only | Borrowed base, multiple indicators or evaluators |
| Clone | Full copy | Direct | Full — independent copy | One-off snapshot needing slot binding |

See the [Benchmark](#benchmark) section for detailed overlay timing data.

## Slot Binding

Slot binding gives direct mutable access to a context value, bypassing name lookups in the
hot loop. Pre-bind slots once after the context is populated, then update through the slot
handle:

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

Register C functions before parsing expressions that call them. Using the `deg2rad`, `clamp`,
and `within_limit` functions from the [Quick Start](#quick-start) example:

```c
// Register those functions under the names used in the expression.
cxpr_registry_add_unary(reg, "deg2rad", deg2rad);
cxpr_registry_add_ternary(reg, "clamp", clamp);
cxpr_registry_add_value(reg, "within_limit", within_limit, 2, 2, NULL, NULL);

// Parse a pipe-style expression. This reads left-to-right:
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

- `cxpr_registry_add_numeric`, `cxpr_registry_add_unary`, `cxpr_registry_add_binary`,
  `cxpr_registry_add_ternary`, and `cxpr_registry_add_nullary` register scalar numeric callbacks
  eligible for the double fast path. `cxpr_registry_add` remains as a compatibility alias.
- `cxpr_registry_add_value` registers callbacks that accept `cxpr_value` arguments and return a
  typed `cxpr_value`.
- `cxpr_registry_add_typed` adds argument type validation and declares a typed return value.
- `cxpr_registry_add_ast` receives the original call AST and can evaluate arguments itself.
- `cxpr_registry_add_ast_handler` layers an AST-level dispatch on top of an existing entry
  without replacing its scalar or struct-producer callbacks (see below).
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
    out[0] = cxpr_num(mid + band); // upper
    out[1] = cxpr_num(mid);        // middle
    out[2] = cxpr_num(mid - band); // lower
}

const char* bb_fields[] = {"upper", "middle", "lower"};
cxpr_registry_add_struct(reg, "bb", bb_producer, 3, 3, bb_fields, 3, NULL, NULL);

// Now expressions like bb(close, 20, 2.0).upper evaluate through the producer.
```

### AST handler dispatch

`cxpr_registry_add_ast_handler` adds an AST-level handler to a function that already has
a scalar or struct-producer callback. The handler sees every call to that function and can
inspect the raw AST arguments — string literals, identifier sources, lookback nodes — before
deciding how to handle the call. The existing scalar and struct paths stay intact, so callers
that pass only numeric arguments still compile to the fast IR path.

This is separate from context overlays. An AST handler decides how a call such as
`close(timeframe="1d")` or `ema(close, 14, "1d")` should be routed. A context overlay is only
one possible host-side tool for evaluating the materialized bars after that routing decision
has been made.

This is useful when a host-backed function has a fast numeric path for the common case but
needs special dispatch when the caller passes a non-numeric argument such as a scope qualifier,
an identifier source, or a lookback offset. Typical examples:

| Expression | Dispatch path |
| ---------- | ------------- |
| `ema(close, 14)` | Scalar callback — all arguments are numeric |
| `ema(close, 14, timeframe="1d")` | AST handler — named string argument triggers scoped resolution |
| `bb(close, 20, 2.0).upper` | Struct producer — field access on numeric arguments |
| `bb(close(timeframe="1d"), 20, 2.0).upper` | AST handler — source argument carries a scope |
| `close` | Variable lookup — no call involved |
| `close(timeframe="1d")` | AST handler — direct source with scope qualifier |

Register the handler after the base callback:

```c
// 1. Register the fast scalar path for ema(source_value, period).
cxpr_registry_add_binary(reg, "ema", ema_scalar);

// 2. Layer an AST handler that handles calls with scope arguments.
//    The handler inspects the AST for string literals or identifier sources.
//    Calls with only numeric arguments still compile to the scalar IR path.
cxpr_registry_add_ast_handler(reg, "ema", ema_scoped_dispatch, 1, 3, NULL, NULL);
```

`cxpr_register_defaults` installs standard math helpers such as `sqrt`, `abs`, `min`, and
`max`. `cxpr_register_basket_builtins` installs basket aggregate helpers for host
applications that evaluate multi-symbol expressions. Use `cxpr_basket_is_builtin`,
`cxpr_basket_is_aggregate_function`,
`cxpr_ast_uses_basket_aggregates`, and `cxpr_expression_uses_basket_aggregates` when a host
needs to detect those aggregate forms before execution.

## Providers and Host-Backed Sources

Provider metadata describes functions and direct sources that live in the host application.
`cxpr` uses this metadata to register parse-time signatures, preserve named arguments,
describe record fields, and decode scoped series arguments.

Important provider pieces:

- `cxpr_provider_fn_spec` describes a host-backed function, its numeric arity, optional source
  input shape, named parameters, output fields, primary field, flags, and scope metadata.
- `cxpr_provider_source_spec` describes a direct source such as `close`, `temperature`, or
  `requests`.
- `cxpr_provider_scope_spec` describes optional named scope arguments. Trading providers usually
  use `timeframe`; generic providers may use names such as `warehouse`, `region`, or another
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
or evaluate value arguments while excluding named scope arguments. Use
`cxpr_resolve_expression_scope` when a host needs to find the first provider-declared scoped
call inside a larger expression.

## Source Plans and Scoped Sources

Source planning is the bridge between parsed expressions and host-owned data sources. It is
useful when expressions such as `close`, `ema(close, 14)`,
`ema(close(timeframe="1d"), 14)[2]`, or an arbitrary nested source expression must be
materialized by the host and evaluated bar-by-bar.

New host integrations should use `cxpr_plan_bind_sources`. It owns the planning workflow:

1. Walk the expression AST.
2. Parse provider source-plan subtrees internally.
3. Evaluate numeric bound arguments against `ctx` and `reg`.
4. Call the host once per materializable source-plan leaf.
5. Register provider-declared scoped source functions with the resolver from
   `cxpr_plan_config`, when a mutable registry is provided.

The host only supplies one plan-time binder and one eval-time resolver:

```c
static int my_bind_source(
    const cxpr_source_plan_node* node,
    const double* args,
    size_t arg_count,
    uint64_t* out_handle,
    void* userdata) {
    my_data_registry* data = (my_data_registry*)userdata;

    // node->name        = "close", "atr", etc.
    // node->field_name  = selected record field, if any
    // node->scope_value = parsed scope such as "1d", if any
    // node->node_id     = stable hash of canonical node content
    // args/arg_count    = this node's pre-evaluated numeric arguments
    return my_data_registry_find_or_create(
        data,
        node->name,
        node->field_name,
        node->scope_value,
        args,
        arg_count,
        out_handle);
}

static int my_resolve_source(
    uint64_t handle,
    const char* source_name,
    double* out_value,
    void* userdata) {
    my_data_registry* data = (my_data_registry*)userdata;
    return my_data_registry_current_value(data, handle, source_name, out_value);
}

cxpr_plan_config config = {
    .bind = my_bind_source,
    .resolve = my_resolve_source,
    .userdata = my_data_registry,
};
cxpr_source_plan_bindings bindings = {0};
if (cxpr_plan_bind_sources(
        &provider,
        expr_ast,
        ctx,
        reg,
        &config,
        &bindings,
        &err)) {
    // bindings.handles contains host handles in source-plan traversal order.
    // cxpr also registered scoped source functions from provider metadata
    // using config.resolve.
    cxpr_free_source_plan_bindings(&bindings);
}
```

Simple hosts can skip the callback and bind from a static table:

```c
const cxpr_source_handle_entry table[] = {
    {"close", NULL, 1},  // default/primary close
    {"close", "1d", 2}, // daily close
};

cxpr_source_plan_bindings bindings = {0};
cxpr_plan_bind_sources_from_table(
    &provider,
    expr_ast,
    ctx,
    reg,
    table,
    CXPR_ARRAY_COUNT(table),
    &bindings,
    &err);
cxpr_free_source_plan_bindings(&bindings);
```

Low-level source-plan parsing helpers still exist for compatibility and advanced migration
work, but they are not the preferred public integration path. Prefer the plan-driver API above
so hosts do not own AST traversal, source-plan lifecycle, argument evaluation, or scoped-source
registration.

At evaluation time the registered scoped source functions delegate value lookup to the
resolver from `cxpr_plan_config`. This lower-level example shows what that resolver state can
look like. The host has already planned its data sources with helpers such as
`cxpr_plan_bind_sources`, canonicalized each scope value, validated that data exists, and
mapped each source to a stable numeric handle. The resolver receives only that handle during
evaluation.
The same pattern can expose full OHLCV sources (`open`, `high`, `low`, `close`, `volume`);
the example registers only `close` to keep the resolver small.
Here, `handle` is the host-defined source id passed to the resolver. In this example,
`0` means "use the default/primary source", while `1` is the example id assigned to the
source selected by a scope such as `"1d"`.

```c
// 1. Define the host-owned series store passed through resolver userdata.
typedef struct {
    const double* primary_close; // array of primary-timeframe close values
    size_t primary_count;
    const double* daily_close; // array of daily close values
    size_t daily_count;
    size_t current_index;
} my_series_store_t;

// 2. Resolve one source value for the current host index.
static int my_source_resolver(
    uint64_t handle, // host-planned source id: 0 = default, 1 = example id for "1d"
    const char* source_name,
    double* out_value,
    void* userdata) {
    my_series_store_t* store = (my_series_store_t*)userdata;
    const double* values = NULL;
    size_t count = 0;

    if (!store || !source_name || !out_value || strcmp(source_name, "close") != 0) {
        return 0;
    }
    if (handle == 0) {
        values = store->primary_close;
        count = store->primary_count;
    } else if (handle == 1) {
        values = store->daily_close;
        count = store->daily_count;
    }
    if (!values || store->current_index >= count) return 0;
    *out_value = values[store->current_index];
    return 1;
}

// 3. Populate the store from host-owned arrays.
double closes_1h[] = {100.0, 101.0, 102.5, 101.8};
double closes_1d[] = {98.0, 103.0, 107.5};
size_t current_bar_index = 1;

my_series_store_t my_series_store = {
    .primary_close = closes_1h,
    .primary_count = CXPR_ARRAY_COUNT(closes_1h),
    .daily_close = closes_1d,
    .daily_count = CXPR_ARRAY_COUNT(closes_1d),
    .current_index = current_bar_index,
};

// 4. Wire the binder and resolver into the plan config passed to cxpr_plan_bind_sources.
cxpr_plan_config config = {
    .bind = my_bind_source,
    .resolve = my_source_resolver,
    .userdata = &my_series_store,
};
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

`err.message` always points to storage owned by `cxpr` — either a static string literal or a
thread-local scratch buffer reused for the next failing call **on the same thread**. It is
never heap-allocated and must not be freed. Treat it as valid only until the next `cxpr` call
on that thread: copy it (or format it with `cxpr_error_format`) if you need to retain it.

`cxpr_error_format` renders a complete, human-readable line including the code, source
position, and message into a caller-owned buffer:

```c
cxpr_error err = {0};
if (!cxpr_evaluator_compile(evaluator, &err)) {
    char buf[256];
    cxpr_error_format(&err, buf, sizeof(buf));
    fprintf(stderr, "%s\n", buf);   // e.g. "Syntax error at 1:7: Expected ')'"
}
```

## Concurrency

`cxpr` has no global mutable state and acquires no locks. The threading contract is
*per-thread isolation*:

- **Immutable-after-build handles are shareable.** A `cxpr_registry`, parsed `cxpr_ast`, and
  compiled `cxpr_program` are not modified during evaluation (the eval entry points take them
  as `const`). Once fully built and no longer being mutated, the same instances may be read
  concurrently from many threads.
- **Mutable handles are not shared.** A `cxpr_context` is updated during evaluation (it caches
  intermediate results), and a `cxpr_evaluator` holds per-batch state. Give each thread its
  own context and evaluator. Building one set and cloning per thread (`cxpr_context_clone`) is
  fine.
- **Internal per-thread state is already isolated.** The empty-overlay reuse cache and the
  error-message scratch buffers are thread-local, so concurrent evaluation on separate
  contexts never races on them.

This makes the common optimizer pattern safe: build a registry and compile programs once on
the main thread, then fan out across worker threads where each thread owns its context and
evaluates against the shared, read-only registry/programs.

A worker thread that runs many evaluations and then exits can call `cxpr_thread_cleanup()`
just before exiting to release its thread-local overlay cache immediately. This is optional —
it never affects correctness, only how promptly that memory is reclaimed.

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
- `cxpr_ast_contains_reference` and `cxpr_ast_contains_variable` test whether a subtree uses a
  specific runtime reference or `$param`.
- `cxpr_ast_call_arg_contexts_for_reference` and `cxpr_ast_call_arg_contexts_for_variable`
  trace which function or producer calls receive that reference/param anywhere inside their
  argument subtrees. Hosts can use this to infer chart/source context without hand-parsing
  expression strings.
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

Trace a reference or parameter into function-call arguments:

```c
cxpr_ast* ast = cxpr_parse(
    parser,
    "supertrend(period=10, mult=ema(atr_pct, $atr_baseline)).value",
    &err);

const char* contexts[8];
size_t n = cxpr_ast_call_arg_contexts_for_variable(
    ast, "atr_baseline", contexts, 8);
// contexts contains "supertrend" and "ema".

n = cxpr_ast_call_arg_contexts_for_reference(ast, "atr_pct", contexts, 8);
// contexts again contains "supertrend" and "ema".
```

When a host maps function names to chart panes or source domains, this gives a stable rule:
inherit a function context only when the traced reference/param resolves to one unambiguous
host context. If the same alias or parameter flows into multiple consumers, for example both
`supertrend(...)` and `macd(...)`, the host should treat that as ambiguous and keep its default
placement unless it creates separate context-specific outputs.

## Examples

Longer integration examples are in [examples/README.md](examples/README.md).

## Benchmark

Build and run the benchmark like this:

```bash
cmake -S . -B build -DCXPR_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release && \
cmake --build build --target cxpr_bench_ir && \
./build/benchmarks/cxpr_bench_ir
```

When `cxpr` is embedded as a CMake subdirectory, the benchmark binary location depends on the
parent project's build tree layout.

Use `-DCMAKE_BUILD_TYPE=Release` when benchmarking for meaningful timings.

Example output from `./build/benchmarks/cxpr_bench_ir`:

```text
cxpr AST vs IR benchmark

Scalar
case                     iters   AST ns/eval    IR ns/eval   speedup
simple_arith            500000         23.85         23.81      1.00x
nested_expr             400000         42.72         36.13      1.18x
function_call           250000         42.32         38.96      1.09x
defined_fn              200000         36.68         30.77      1.19x
native_fn               200000         33.26         28.52      1.17x
defined_chain           120000         46.99         43.14      1.09x
native_chain            120000         35.91         33.83      1.06x
mixed_chain             120000         41.06         38.98      1.05x
deep_defined             80000         46.17         42.77      1.08x
deep_native              80000         92.54         34.20      2.71x
context_churn           200000         97.71         98.00      1.00x
ast_handler_num         200000        198.00        199.99      0.99x
ast_handler_string      200000        212.47        199.79      1.06x

Typed Struct
case                     iters   AST ns/eval    IR ns/eval   speedup
producer_field          150000        152.34        136.40      1.12x
producer_struct         150000        225.70         64.22      3.51x

IR-only
case                     iters   AST ns/eval    IR ns/eval   speedup
context_slot            200000             -         40.12         -

Context Update Paths
case                     iters     set ns/op     alt ns/op   speedup
base_array              500000        138.35        129.98      1.06x
mutate_array            500000         65.04         61.44      1.06x
mutate_prehashed        500000         65.04         68.57      0.95x
mutate_slot             500000         65.04          9.14      7.11x

Param Update Paths
case                     iters     set ns/op     alt ns/op   speedup
base_param_array        500000         97.45         87.72      1.11x
mutate_param_array      500000         98.07         88.80      1.10x
mutate_param_hash       500000         98.07         84.06      1.17x

Overlay Paths
case                           iters           ns/op        output
parent_get                   1000000            9.38     11.500000
overlay_fallback_get         1000000           24.29     11.500000
overlay_override_get         1000000            9.78     21.500000
overlay_alloc_free_get        200000           31.42     11.500000
defined_prefix                200000           78.93     42.000000
sink=523307643.882822
```

These numbers are machine-dependent, but they show the expected shape: IR evaluation
is usually faster than AST evaluation, slot/prehashed update paths help the hottest loops,
and overlay allocation/release is cheap when the overlay has no local state to destroy.
