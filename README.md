# cxpr

[![CI](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml/badge.svg)](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

`cxpr` is a standalone C11 expression engine for applications that need safe,
embeddable rules without a scripting runtime. Given an expression string, it
parses it into an AST, analyzes references and function requirements, evaluates
it against a context of variables and parameters, and optionally compiles it to
typed IR for repeated execution without re-parsing. It is domain-agnostic — the
same engine drives rules across very different fields:

```text
sqrt(vx^2 + vy^2) > $max_speed              # physics / robotics
within(latency_ms, 0, $budget_ms)           # systems / SLOs
close > ema(close, 20) and volume > $min_volume # trading
```

It supports numbers, booleans, struct-like values, custom C callbacks, and
expression-defined functions. A named-expression evaluator manages sets of
interdependent expressions with automatic topological ordering and cycle
detection. Host integrations provide domain data and execution policy: provider
metadata, scoped sources, runtime-resolved series, and source plans for
step-by-step materialization (e.g. per simulation tick or per market bar) live
outside the engine. See [examples/](examples) for runnable physics, robotics,
and trading programs.

No external dependencies. C11 required.

## Contents

- [What The Library Provides](#what-the-library-provides)
- [Core Concepts](#core-concepts)
- [Engine Layer](#engine-layer)
- [Building and Testing](#building-and-testing)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Expression Language](#expression-language)
- [Sets and Intervals](#sets-and-intervals)
- [Built-in Function Reference](#built-in-function-reference)
- [Values, Structs, and Contexts](#values-structs-and-contexts)
- [Timestamps and Durations](#timestamps-and-durations)
- [Null Handling](#null-handling)
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
- [Code Generation](#code-generation)
- [Examples](#examples)
- [Benchmark](#benchmark)

## What The Library Provides

- A standalone expression engine: parser, analyzer, evaluator, and typed IR compiler
- A parser that turns expression strings into an AST
- Evaluation of numbers, booleans, and struct-like values
- AST and typed IR execution paths for expressions you run many times
- A context API for variables, `$params`, named structs, cached structs, overlays, slots, and prehashed updates
- A registry for scalar, typed, AST-level, time-series, struct-producing, built-in, basket, and expression-defined functions
- Expression evaluation with dependency ordering and cycle detection
- AST analysis for references, parameters, functions, producer fields, result shape, and short-circuit behavior
- Provider metadata for host-backed functions, named arguments, record fields, scoped series, and direct sources
- Runtime-call and source-plan helpers for host integrations that materialize series data outside `cxpr`
- An opt-in rule-engine layer for stateful tick/session execution, source hydration,
  lookback history, and transition events
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
- `cxpr_engine_program`: immutable compiled rule engine program, safe to share across sessions
- `cxpr_engine_session`: mutable per-run engine state for ticks, params, lookback, and events

## Engine Layer

Most of the API is intentionally low level: parse an expression, build a
registry, fill a context, evaluate. That is the right surface when a host already
has its own scheduler, state model, and event loop.

`cxpr/engine.h` adds a higher-level rule-engine layer for hosts that want cxpr to
own the repeated execution mechanics. The host declares expressions, sources,
default `$params`, roles, and watches once in a `cxpr_engine_config`. The engine
then builds a compiled `cxpr_engine_program` and runs isolated
`cxpr_engine_session`s:

```text
cxpr_engine_config
  -> cxpr_engine_program       # immutable, compiled once, shareable
  -> cxpr_engine_session       # mutable run state: params, cursor, lookback
  -> cxpr_engine_tick()        # hydrate referenced sources, evaluate, emit events
```

The engine API is split into:

- `cxpr_engine_config`: declarative description of rules, sources, params, roles,
  and watches
- `cxpr_engine_program`: immutable compiled engine program, built once and shared
- `cxpr_engine_session`: mutable per-run state, one per live run or worker

Think of `program` as the compiled rule plan and `session` as one execution of
that plan. The program owns read-only structure: expression dependency order,
watch declarations, source definitions, default params, and lookback buffer
layout. A session borrows that program and owns changing state: the current
cursor, hydrated source values, lookback history, previous values for edge
detection, per-run `$param` overrides, role membership, and pending events.

Build one program when the rule/source shape is fixed, then create one session
per independent run. Optimizers typically share one program across many worker
sessions with different params or data bindings. A live loop or one-off backtest
may use only one session, but the same lifetime rule applies: a borrowed program
must outlive every session created from it.

The engine owns:

- dependency-ordered expression evaluation
- lazy source hydration for pull, callback-view, and direct column sources
- lookback history for `expr[n]`
- per-session `$param` overrides and role bindings
- transition detection via `CXPR_EDGE_RISING`, `CXPR_EDGE_FALLING`,
  `CXPR_EDGE_LEVEL`, and `CXPR_EDGE_CHANGED`

The host still owns domain policy. Source callbacks only provide values; fired
events are returned after the tick, and the host decides what those events mean.
The engine has no trading, robotics, monitoring, order, or IO behavior built in.

Minimal shape:

```c
#include <cxpr/engine.h>

static const double close[] = { 10.0, 11.0, 9.0, 12.0, 8.0 };

int main(void) {
    const cxpr_expression_def exprs[] = {
        { "buy", "close > $threshold" },
    };
    const cxpr_context_entry params[] = {
        { "threshold", 10.0 },
    };
    const cxpr_engine_column_source_def cols[] = {
        { "close", &close[0], sizeof(double), 5 },
    };
    const cxpr_engine_watch_def watches[] = {
        { "buy", CXPR_EDGE_RISING },
    };

    cxpr_engine_config cfg = {
        .expressions = exprs, .expression_count = 1,
        .params = params, .param_count = 1,
        .column_sources = cols, .column_source_count = 1,
        .watches = watches, .watch_count = 1,
    };

    cxpr_error err = {0};
    cxpr_engine_program* program = cxpr_engine_program_new(&cfg, &err);
    cxpr_engine_session* session = cxpr_engine_session_new(program);

    for (size_t i = 0; i < 5; ++i) {
        const cxpr_engine_event* events = NULL;
        size_t event_count = 0;
        cxpr_engine_tick(session, &events, &event_count, &err);
        /* Host handles events[0..event_count). */
    }

    cxpr_engine_session_free(session);
    cxpr_engine_program_free(program);
    return 0;
}
```

Use the lower-level evaluator/context APIs when you need full control over every
evaluation step. Use the engine layer when your application naturally looks like
"compile rules once, advance a cursor, and react to fired conditions".

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
./build-fuzz/tests/fuzz/cxpr_fuzz_parse build-fuzz/tests/fuzz/corpus   # run
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
// ema_alpha is the smoothing factor of an N-period EMA: a genuinely
// domain-specific helper with no standard-library equivalent. (Common math
// such as radians/hypot/log1p is already built in — see Custom Functions.)
static double ema_alpha(double period) {
    return 2.0 / (period + 1.0);
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
    // period is a normal context variable referenced as period in expressions.
    cxpr_context_set(ctx, "period", 9.0);

    // limit is a parameter referenced as $limit in expressions.
    cxpr_context_set_param(ctx, "limit", 0.4);

    // 4. Register those functions under the names used in the expression.
    cxpr_registry_add_unary(reg, "ema_alpha", ema_alpha);
    cxpr_registry_add_ternary(reg, "clamp", clamp);
    cxpr_registry_add_value(reg, "within_limit", within_limit, 2, 2, NULL, NULL);

    cxpr_error err = {0};
    // 5. Parse an expression that turns period into a smoothing factor, clamps
    //    it to [0, 1], and checks it stays below $limit.
    cxpr_ast* ast = cxpr_parse(parser,
        "within_limit(clamp(ema_alpha(period), 0.0, 1.0), $limit)",
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
close > ema(close, 20) and volume > $min_volume
signal > $threshold ? 1.0 : 0.0
body.position.x + body.velocity.x
```

Supported language features:

- Arithmetic: `+`, `-`, `*`, `/`, `%`, `^`, `**` (numbers; `+`/`-`/`*`/`/` also
  drive the [timestamp/duration algebra](#timestamps-and-durations) and
  [struct arithmetic](#values-structs-and-contexts))
- Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=` (numbers, and ordering of two
  timestamps or two durations)
- Set membership: `x in [a, b, c]`, `x not in [a, b, c]`,
  `contains(x, values)` (matches when `x` equals any listed value)
- Logic: `and`, `or`, `not`, `&&`, `||`, `!`
- Ternary: `condition ? a : b`
- Function calls: `sqrt(x)`, `clamp(v, lo, hi)`, `contains(v, values)`,
  `within(v, min, max)`
- Named arguments for calls that preserve argument names in the AST/provider path:
  `close(timeframe="1d")`, `macd(fast=12, slow=26, signal=9).hist`
- Forward pipe: `x |> f |> g(1)` (desugars to `g(f(x), 1)`, RHS must be callable)
- Params with `$` prefix: `$threshold`
- Field access for named structs and produced structs: `quote.mid`, `body.velocity.x`
- String literals: `"1d"`, used as named-argument values such as `close(timeframe="1d")`
- Postfix lookback syntax: `close[1]`, `macd(12, 26, 9).signal[2]`

### Operator Precedence

From lowest to highest binding. Operators in the same row share precedence.

| Level | Operators | Associativity | Notes |
| --- | --- | --- | --- |
| 1 (loosest) | `\|>` | left | Forward pipe; RHS must be callable |
| 2 | `?:` | right | Ternary conditional |
| 3 | `or`, `\|\|` | left | Short-circuits |
| 4 | `and`, `&&` | left | Short-circuits |
| 5 | `not`, `!` | right (unary) | Logical negation |
| 6 | `==`, `!=` | left | Matching scalar types only |
| 7 | `<`, `<=`, `>`, `>=`, `in`, `not in` | left | Ordering; set membership |
| 8 | `+`, `-` | left | Additive |
| 9 | `*`, `/`, `%` | left | Multiplicative |
| 10 | `-`, `+` | right (unary) | Unary sign |
| 11 | `^`, `**` | right | Exponentiation |
| 12 (tightest) | `f(...)`, `.field`, `[lookback]` | left (postfix) | Calls, field access, lookback |

Primary expressions — number/string literals, `true`/`false`, identifiers,
`$params`, and parenthesised groups — bind tighter than every operator.

### Sets and Intervals

`in` is set membership syntax. `contains(...)` and `within(...)` are builtin
membership predicates.

`x in [a, b, c]` is **set membership**: true when `x` equals any listed value. It
desugars to `contains(x, [a, b, c])` and therefore works for any scalar type
equality supports (numbers, bools, strings, null, timestamps, durations). At
least one element is required for `in`.

```text
regime in ["uptrend", "breakout"]
side not in [buy, sell]
x in [$a, $b]
contains(source=region, values=$allowed_regions)
```

`contains(source, values)` is the function form for runtime arrays. `values`
must evaluate to an array; use `cxpr_context_set_value(...)` or
`cxpr_context_set_param_value(...)` to bind array variables/params from C.

`within(x, lo, hi)` is **interval membership**: an inclusive, continuous range
by default. It also supports named arguments and optional exclusive bounds.

```text
within(temperature, 18, 24)                         # 18 <= temperature and temperature <= 24
within(source=temperature, min=18, max=24)          # named args, order-independent
within(temperature, 18, 24, false, true)            # exclusive min, inclusive max
within(temperature, 18, 24, include_max=false)      # named optional bound
not within(latency_ms, 0, $budget_ms)
```

> Migration note (2.0.0): earlier releases used `x in [lo, hi]` for intervals.
> That spelling is now set membership; use `within(x, lo, hi)` for ranges.

## Built-in Function Reference

`cxpr_register_defaults(reg)` installs everything in this section. Arity is the
accepted argument count; `n..m` means variadic. All numeric functions are
eligible for the compiled double fast path unless noted.

### Constants (nullary)

| Function | Returns | Description |
| --- | --- | --- |
| `pi()` | number | 3.14159265358979323846 |
| `e()` | number | 2.71828182845904523536 |
| `nan()` | number | IEEE quiet NaN |
| `inf()` | number | Positive infinity |

### Unary math

| Function | Description |
| --- | --- |
| `abs(x)` | Absolute value |
| `sign(x)` | −1, 0, or +1 by sign of `x` |
| `floor(x)`, `ceil(x)`, `round(x)`, `trunc(x)` | Rounding modes |
| `sqrt(x)`, `cbrt(x)` | Square / cube root |
| `exp(x)`, `exp2(x)`, `expm1(x)` | `e^x`, `2^x`, `e^x − 1` |
| `log(x)`, `log2(x)`, `log10(x)`, `log1p(x)` | Natural/base-2/base-10 log, `log(1+x)` |
| `sin(x)`, `cos(x)`, `tan(x)` | Trigonometric (radians) |
| `asin(x)`, `acos(x)`, `atan(x)` | Inverse trigonometric |
| `sinh(x)`, `cosh(x)`, `tanh(x)` | Hyperbolic |
| `radians(x)` | Degrees → radians |
| `degrees(x)` | Radians → degrees |

### Binary math

| Function | Description |
| --- | --- |
| `pow(b, e)` | `b^e` (same as `b ^ e`) |
| `hypot(x, y)` | `sqrt(x² + y²)` without overflow |
| `mod(x, y)` | Floating-point remainder (same as `x % y`) |
| `copysign(m, s)` | Magnitude of `m` with sign of `s` |
| `atan2(y, x)` | Two-argument arctangent |
| `add(a, b)`, `sub(a, b)`, `mul(a, b)`, `div(a, b)` | Function forms of `+ - * /` |

### Ternary / variadic math

| Function | Arity | Description |
| --- | --- | --- |
| `min(...)`, `max(...)` | 1..8 | Smallest / largest argument |
| `clamp(x, lo, hi)` | 3 | Constrain `x` to `[lo, hi]` (swaps if `lo > hi`) |
| `lerp(a, b, t)` | 3 | Linear interpolation `a + (b−a)·t` |
| `smoothstep(x, e0, e1)` | 3 | Hermite smoothstep in `[e0, e1]` |
| `sigmoid(x, center, steepness)` | 3 | Logistic `1/(1+e^(−steepness·(x−center)))` |
| `if(cond, a, b)` | 3 | `a` when `cond ≠ 0`, else `b` (also `cond ? a : b`) |

### Predicates and null handling

| Function | Returns | Description |
| --- | --- | --- |
| `isnan(x)` | bool | True when `x` is NaN |
| `isfinite(x)` | bool | True when `x` is finite |
| `is_null(x)` | bool | True when `x` is `null` |
| `coalesce(a, b, …)` | any | First non-null argument (1..8 args), else `null` |

### Time-series functions

These read an argument expression at historical offsets (offset 0 = current bar,
offset `n` = `n` bars ago) and require lookback-capable evaluation. `value` is a
numeric expression, `condition` a boolean one, and `samples`/`bars` a positive
integer count. The `(value, samples)` functions accept named `value`/`samples`
arguments (e.g. `rising(value=close, samples=3)`); `overlaps`/`signal_overlaps`
accept named `left`, `right`, `bars`. `cross_above`/`cross_below` are positional.

| Function | Arity | Returns | Description |
| --- | --- | --- | --- |
| `rising(value, samples)` | 2 | bool | `value` strictly increased on every bar across the window |
| `falling(value, samples)` | 2 | bool | `value` strictly decreased on every bar across the window |
| `net_up(value, samples)` | 2 | bool | `value` now is higher than `samples` bars ago |
| `net_down(value, samples)` | 2 | bool | `value` now is lower than `samples` bars ago |
| `delta(value, samples)` | 2 | number | `value − value[samples]` |
| `roc(value, samples)` | 2 | number | Rate of change `(value − value[samples]) / value[samples]` |
| `highest(value, samples)` | 2 | number | Maximum of `value` over the window |
| `lowest(value, samples)` | 2 | number | Minimum of `value` over the window |
| `cross_above(left, right)` | 2 | bool | `left` crossed from `≤ right` to `> right` this bar |
| `cross_below(left, right)` | 2 | bool | `left` crossed from `≥ right` to `< right` this bar |
| `repeat(condition, samples)` | 2 | bool | `condition` held true on every one of the last `samples` bars |
| `overlaps(left, right[, bars])` | 2..3 | bool | Both conditions occur within `bars` bars of each other (default same bar) |
| `signal_overlaps(left, right[, bars])` | 2..3 | bool | Alias of `overlaps` |

### Opt-in: basket aggregates

`cxpr_register_basket_builtins(reg)` (separate from `cxpr_register_defaults`)
adds multi-symbol aggregates for hosts that evaluate basket expressions:
`avg(expr)`, `count(expr)`, `any(expr)`, `all(expr)`, and the basket forms of
`min(expr)`/`max(expr)`. See [the basket section](#what-the-library-provides)
and `cxpr_basket_is_builtin` for detection helpers.

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
Arrays (`CXPR_VALUE_ARRAY`) are transport values for callbacks, structs, and
membership predicates. Bracketed lists are first-class array literals, so
`x in [$a, $b]` desugars to `contains(x, [$a, $b])`, while
`contains(x, $allowed)` can use an array bound in the context. Array literals
may also be the top-level expression and may contain nested arrays or any other
`cxpr_value` produced by their element expressions. `cxpr` does not attach host
meaning to a top-level array; callers that expect a scalar should validate the
returned type. Array values returned from `cxpr_eval_program` are owned by the
caller and should be released with `cxpr_value_free`. Deep array equality is not
part of the expression language.

Timestamps and durations also carry a closed arithmetic and ordering algebra
(see [Timestamps and Durations](#timestamps-and-durations)), and `null` values
can be defaulted with the built-in `coalesce`/`is_null` helpers
(see [Null Handling](#null-handling)). Both work identically in the tree-walk
and compiled paths. Timestamp and duration values reach expressions through
struct fields and typed callbacks — not bare numeric context variables, whose
fast-path stays double-only.

Struct arithmetic applies `+`, `-`, `*`, and `/` field by field:

```text
quote * 2
2 * quote
quote + spread
{ bid = 101.4, ask = 101.6 } - { bid = 0.1, ask = 0.1 }
```

Struct/struct operations require matching named fields. The result preserves the
left operand's field order. Struct/scalar operations apply the scalar to every
field and preserve operand order, so `10 - vector` differs from `vector - 10`.
Fields may be numbers or other compatible typed values such as durations. Static
record literals with mismatched fields fail during compile/typecheck; dynamic
context or producer structs are validated at runtime.

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

## Timestamps and Durations

`CXPR_VALUE_TIMESTAMP` (Unix nanoseconds) and `CXPR_VALUE_DURATION` (nanoseconds)
participate in a closed, type-checked algebra. Values flow in from struct fields
and typed callbacks — for example an `event.time` field or a host `now()`
function — and the engine reasons about them directly instead of forcing every
host to unpack raw nanoseconds:

```text
event.time - request.start       # timestamp - timestamp  -> duration
event.time + $retry_after        # timestamp + duration    -> timestamp
$timeout * 2                     # duration  * number      -> duration
elapsed / sample_interval        # duration  / duration    -> number (ratio)
event.time >= window_start       # ordering of two timestamps -> bool
```

The full set of valid operations:

| Expression | Result |
| --- | --- |
| `timestamp - timestamp` | `duration` |
| `timestamp ± duration`, `duration + timestamp` | `timestamp` |
| `duration ± duration` | `duration` |
| `duration * number`, `number * duration` | `duration` |
| `duration / number` | `duration` |
| `duration / duration` | `number` |
| `<`, `<=`, `>`, `>=` on two timestamps or two durations | `bool` |
| `==`, `!=` on matching timestamp/duration operands | `bool` |

Any other combination (for example `timestamp + timestamp`, `timestamp * number`,
or comparing a timestamp with a duration) is a `CXPR_ERR_TYPE_MISMATCH`. The rules
are identical in the tree-walk evaluator and the compiled IR. Timestamp and
duration values must originate from struct fields or typed callbacks; the
double-only numeric fast path is unaffected, so plain numeric variables keep
their performance.

## Null Handling

`CXPR_VALUE_NULL` represents missing data — a common case for optional fields or
host-backed series that have no value at a given step. Two built-ins make `null`
usable instead of fatal:

```text
coalesce(reading, prev_reading)  # first non-null argument (1-8 args)
coalesce(config.timeout, 30)     # fall back to a default when unset
is_null(sensor.value)            # true when the value is null
coalesce(close[1], close)        # series: last bar missing -> use current
```

`coalesce(a, b, …)` returns its first non-null argument (or `null` if all are
null). `is_null(x)` returns a boolean and can guard any downstream use. Both run
in the typed evaluation path in either engine. There is no dedicated `??`
operator — `coalesce` covers the same need without adding operator syntax.

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
- **Per-element iteration** — evaluate one item at a time (a simulation particle, a basket
  symbol), overlaying per-item data while the shared context stays in the parent.
- **Per-step evaluation** — in a loop over discrete steps (simulation ticks, animation frames,
  time-series bars), create an overlay per step with step-specific fields while shared
  constants, parameters, and configuration stay in the parent.
- **Source remapping** — map a struct prefix like `src.x` into a function parameter like `v.x`.

Context overlays are an evaluation primitive, not expression syntax. They do not select a
rate or scope by themselves. Hosts that expose scoped data (different sampling rates,
timeframes, or coordinate frames) should represent that in the expression language through
scoped source or indicator calls — for example a finance host selecting a timeframe:

```text
close("1d")
ema(close, 14, "1d")
close(timeframe="1d")
ema(close, 14, timeframe="1d")
```

The host may then use context overlays internally while materializing those scoped series,
keeping the primary stream's fields in a base context and evaluating the secondary stream in
child contexts so its values do not overwrite the primary ones.

The example below is a simulation: the base context holds shared constants and the current
step's state, and a finer-grained sub-step integration runs in overlays.

**Shared context — works when everything advances at the same rate:**

```c
// ctx holds the current step state set by the integrator (position, velocity).
for (size_t i = 0; i < step_count; i++) {
    cxpr_context_set(ctx, "position",   state[i].position);
    cxpr_context_set(ctx, "velocity",   state[i].velocity);
    cxpr_context_set(ctx, "step_index", (double)i);

    cxpr_eval_ast(energy_ast, ctx, reg, &out, &err);
    series[i] = out.d;
}
// Simple and fast. But if a second pass now refines each step with sub-steps,
// writing sub_state[j].position into the same ctx overwrites the step position.
// After that loop finishes, the step position in ctx is gone — any later
// computation that expects step-level state silently reads the last sub-step value.
```

**Overlay — sub-steps shadow the step state without destroying it:**

```c
// base holds shared constants/params and the current step state.
for (size_t j = 0; j < substep_count; j++) {
    cxpr_context* sub_ctx = cxpr_context_overlay_new(base);
    cxpr_context_set(sub_ctx, "position",   sub_state[j].position);
    cxpr_context_set(sub_ctx, "velocity",   sub_state[j].velocity);
    cxpr_context_set(sub_ctx, "step_index", (double)j);

    // Reads shared constants and params from base.
    // Sub-step position/velocity/step_index stay in sub_ctx.
    cxpr_eval_ast(refined_ast, sub_ctx, reg, &out, &err);
    refined_series[j] = out.d;

    cxpr_context_free(sub_ctx); // recycled — sub-step state discarded
}
// base still holds the step-level state. The next pass that needs step-level
// values — or another computation on a coarser cadence — sees the base intact.
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

const char* slot_names[] = {"close", "volume"};
cxpr_context_slot slots[2];
cxpr_context_slots_bind(ctx, slot_names, slots, CXPR_ARRAY_COUNT(slots));

// In the hot loop, update through slots instead of by name.
for (size_t i = 0; i < bar_count; i++) {
    cxpr_context_slots_set(slots, (double[]){bars[i].close, bars[i].volume},
                           CXPR_ARRAY_COUNT(slots));

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

Register C functions before parsing expressions that call them. Register only what is
genuinely host- or domain-specific — common math (`hypot`, `radians`, `degrees`, `mod`,
`copysign`, `log1p`, `expm1`, `isnan`, `isfinite`) and null handling (`coalesce`, `is_null`)
are already built in via `cxpr_register_defaults`. Using the `ema_alpha`, `clamp`, and
`within_limit` functions from the [Quick Start](#quick-start) example:

```c
// Register those functions under the names used in the expression.
cxpr_registry_add_unary(reg, "ema_alpha", ema_alpha);
cxpr_registry_add_ternary(reg, "clamp", clamp);
cxpr_registry_add_value(reg, "within_limit", within_limit, 2, 2, NULL, NULL);

// Parse a pipe-style expression. This reads left-to-right:
// period -> ema_alpha(...) -> clamp(..., 0.0, 1.0) -> within_limit(..., $limit)
cxpr_ast* ast = cxpr_parse(parser,
    "period |> ema_alpha |> clamp(0.0, 1.0) |> within_limit($limit)",
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

`cxpr_register_defaults` installs standard math helpers. Besides the usual
`sqrt`, `abs`, `min`, `max`, `clamp`, and trig/exponential families, it includes
`hypot`, `radians`, `degrees`, `mod`, `copysign`, `log1p`, `expm1`, the boolean
predicates `isnan`/`isfinite`, and the null helpers `coalesce`/`is_null`. Prefer
these over re-registering equivalents as custom functions.
`cxpr_register_basket_builtins` installs basket aggregate helpers for host
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

A worker thread that runs many evaluations and then exits can call
`cxpr_thread_cleanup()` from `cxpr/thread.h` just before exiting to release its
thread-local overlay cache immediately. This is optional — it never affects
correctness, only how promptly that memory is reclaimed.

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
if (cxpr_analyze_expr("close > ema_fast and volume > $min_volume", reg, &info, &err)) {
    printf("result type:  %s\n", info.result_type == CXPR_EXPR_BOOL ? "bool" : "number");
    printf("uses params:  %s\n", info.uses_parameters ? "yes" : "no");
    printf("can short-circuit: %s\n", info.can_short_circuit ? "yes" : "no");
    printf("references:   %zu\n", info.reference_count);   // close, ema_fast, volume
    printf("parameters:   %zu\n", info.parameter_count);   // min_volume
}

// Collect the actual names used in a parsed AST.
cxpr_ast* ast = cxpr_parse(parser, "close > ema_fast and volume > $limit", &err);
const char* refs[8];
size_t n = cxpr_ast_references(ast, refs, 8);      // close, ema_fast, volume
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

## Code Generation

`cxpr/codegen.h` transpiles ASTs into C source — the codegen counterpart to the
runtime evaluator. Use it to emit native source you then compile (a hot loop, or
a GPU kernel via a runtime compiler) instead of interpreting.

```c
cxpr_ast* ast = cxpr_parse(parser, "2 * G * M / c^2", &err);
char* code = cxpr_ast_to_c(ast, NULL, &err);   // -> "((2 * (G * M)) / pow(c, 2))"
free(code);
```

`cxpr_exprset_to_c` transpiles a set of interdependent named expressions into a
block of C declarations, topologically ordered so each definition precedes its
uses (cycles are rejected):

```c
cxpr_c_named_expr defs[] = {
    { "dr_dl", ast_dr },   // "p_r * f"
    { "f",     ast_f  },   // "1 - r_s / r"
    { "r_s",   ast_rs },   // "2 * G * M / c^2"
};
char* block = cxpr_exprset_to_c(defs, 3, "double", NULL, &err);
// double r_s = ...;  double f = ...;  double dr_dl = ...;  (in dependency order)
```

`cxpr_exprset_to_c_function` wraps that block into a complete function: a result
struct (one field per expression name) plus a function that takes one parameter
per input, computes the locals in dependency order, and returns the struct. The
caller supplies every name and type, so it stays target-agnostic — it emits
portable C with no CUDA coupling; `__host__ __device__` (if wanted) is just a
`qualifiers` argument:

```c
const char* inputs[] = { "r", "p_r", "G", "M", "c", "L" };
char* fn = cxpr_exprset_to_c_function("CX_HD static inline", "State", "double",
                                      "eval", inputs, 6, defs, 3, NULL, &err);
// typedef struct State { double r_s; double f; double dr_dl; } State;
// CX_HD static inline State eval(double r, double p_r, ...) { ...; return _cx_out; }
```

Mapping: `^`/`**` → `pow()`, `%` → `fmod()`, `and`/`or`/`not` → `&&`/`||`/`!`,
variadic `min`/`max` → nested `fmin`/`fmax`. Target-specific function names (CUDA,
WGSL, …) are supplied through `cxpr_c_target.map_function`. Native lookback
codegen is available when the target sets `api_version = CXPR_C_TARGET_API_VERSION`
and supplies `emit_leaf_at_offset`; cxpr then propagates `expr[n]` offsets through
the AST and the host emits each concrete leaf at that offset. This keeps buffer
layout and warmup policy host-side while sharing one traversal rule. `rising`,
`falling`, and `repeat` codegen require literal bars/samples so those offsets can
be expanded at compile time.

Field/chain/producer nodes without a target hook and unmapped functions are
rejected with an error — the emitter is conservative by design. `in` desugars to
`contains(...)`, so codegen rejects it unless a target/runtime supplies membership
support; `within(...)` is likewise a runtime builtin, not a C operator.

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

The `.cxpr C` column reports the compiled scalar result for a `.cxpr` model.
For struct expressions this means generated C may scalarize a final field
projection such as `(vector * weights).z` instead of materializing a runtime
struct. The comparison is therefore result-oriented: AST/IR measure the typed
runtime path, while `.cxpr C` measures the compiled path to the requested output.

To inspect the generated benchmark C, run:

```bash
./build/benchmarks/cxpr_bench_ir --print-c
./build/benchmarks/cxpr_bench_ir --print-c struct_struct_mul
```

For subdirectory builds the generated inline files are written under the parent
build tree, for example `build/libs/cxpr/benchmarks/ir_struct_struct_mul.inline.inc`.
The debug output prints the benchmark expression, the `.inc` path, and the
generated C source for each `.cxpr C` case. Generated model C includes `// .cxpr:`
comments above emitted expressions to make scalarized output easier to inspect.

Example output from `./build/benchmarks/cxpr_bench_ir`:

```text
cxpr AST vs IR benchmark

Scalar
case                           iters   AST ns/eval    IR ns/eval  .cxpr C ns/eval    AST/IR      IR/C
simple_arith                  500000         39.43         28.81            3.13      1.37x      9.22x
nested_expr                   400000         37.71         39.05            3.43      0.97x     11.37x
function_call                 250000         43.63         41.84            2.98      1.04x     14.04x
defined_fn                    200000         30.66         26.67            4.52      1.15x      5.90x
native_fn                     200000         56.48         55.30               -      1.02x         -
defined_chain                 120000         50.77         46.10            8.95      1.10x      5.15x
native_chain                  120000         65.05         65.31               -      1.00x         -
mixed_chain                   120000         66.46         63.59               -      1.05x         -
deep_defined                   80000         49.47         42.33            4.72      1.17x      8.96x
deep_native                    80000        104.47         53.28               -      1.96x         -
complex_signal                 80000         68.78         67.52            6.32      1.02x     10.69x
mixed_expr                    120000         43.26         41.11            3.79      1.05x     10.85x
mixed_pipe                    120000         43.39         39.99            3.91      1.08x     10.22x
context_churn                 200000        143.56        139.83            5.77      1.03x     24.23x
ast_handler_num               200000        287.66        498.10               -      0.58x         -
ast_handler_string            200000        290.61        525.24               -      0.55x         -

Typed Struct
case                           iters   AST ns/eval    IR ns/eval  .cxpr C ns/eval    AST/IR      IR/C
producer_field                150000        187.42        113.55               -      1.65x         -
producer_struct               150000        551.86         68.12               -      8.10x         -
struct_scalar_mul             120000        422.32        418.55            3.00      1.01x    139.67x
scalar_struct_mul             120000        411.23        427.87            2.96      0.96x    144.35x
struct_struct_mul             100000        599.60        608.23            3.01      0.99x    202.06x
struct_struct_add             100000        597.62        644.27            3.09      0.93x    208.62x
struct_scalar_mul_all         100000       1274.36       1313.17            3.01      0.97x    436.76x
scalar_struct_mul_all         100000       1317.62       1374.22            3.15      0.96x    435.81x
struct_struct_mul_all          80000       1905.84       1910.81            3.04      1.00x    627.87x
struct_struct_add_all          80000       1957.29       1884.30            3.08      1.04x    612.49x

Lookback
case                           iters   AST ns/eval    IR ns/eval  .cxpr C ns/eval    AST/IR      IR/C
lookback_leaf                 250000        115.55        110.19            3.05      1.05x     36.14x
lookback_mixed                200000        160.50        156.40            3.00      1.03x     52.10x
lookback_nested               200000        127.19        132.22               -      0.96x         -

IR-only
case                           iters   AST ns/eval    IR ns/eval  .cxpr C ns/eval    AST/IR      IR/C
context_slot                  200000             -         81.11               -         -         -

Context Update Paths
case                           iters     set ns/op     alt ns/op   speedup
base_array                    500000        219.71        209.73      1.05x
mutate_array                  500000        132.49        117.39      1.13x
mutate_prehashed              500000        132.49        110.23      1.20x
mutate_slot                   500000        132.49         10.10     13.12x

Param Update Paths
case                           iters     set ns/op     alt ns/op   speedup
base_param_array              500000        154.81        152.06      1.02x
mutate_param_array            500000        168.48        162.28      1.04x
mutate_param_hash             500000        168.48        153.69      1.10x

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
is usually faster than AST evaluation, and generated `.cxpr` C is called directly for
rows that have a codegen fixture. `-` means no generated C case is linked for that row.
Slot/prehashed update paths help the hottest loops, and overlay allocation/release is
cheap when the overlay has no local state to destroy.
