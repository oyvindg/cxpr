# cxpr

[![CI](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml/badge.svg)](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Generic expression parser, evaluator, and formula engine written in C11. No external dependencies.

## Features

- Full expression grammar — arithmetic, comparison, logical, ternary, function calls
- Runtime variables (`context`) and compile-time parameters (`$param`)
- Custom function registry with arity validation and optional userdata
- Built-in math and utility functions (`sin`, `cos`, `sqrt`, `pow`, `min`, `max`, `log`, ...)
- Formula engine support via `cxpr_formula_engine` with dependency resolution and cycle detection
- AST inspection API — extract variable, parameter, and function references
- Structured error handling — error codes, messages, and source position
- C11-oriented API surface that is practical to embed from C and other native-language bindings

## Installation

### As a git submodule

```bash
git submodule add https://github.com/oyvindg/cxpr.git libs/cxpr
git submodule update --init
```

```cmake
add_subdirectory(extern/cxpr)
target_link_libraries(my_target PRIVATE cxpr::cxpr)
```

Tests are only built when `cxpr` is the top-level project, so they won't interfere with your build.

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

### Via find_package (system install)

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

Start with the AST path. It is the most direct way to parse and evaluate an expression, and it matches the core mental model of the library.

```c
#include <cxpr/cxpr.h>
#include <stdio.h>

int main(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_context_set(ctx, "rsi", 25.0);
    cxpr_context_set(ctx, "volume", 1500000.0);
    cxpr_context_set_param(ctx, "min_volume", 1000000.0);

    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, "rsi < 30 and volume > $min_volume", &err);
    if (!ast) { fprintf(stderr, "parse error: %s\n", err.message); return 1; }

    cxpr_field_value result = cxpr_ast_eval(ast, ctx, reg, &err);  // -> bool(true)

    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
}
```

When the same parsed expression will be evaluated repeatedly with changing context values, compile it once and use the IR path:

```c
cxpr_program* prog = cxpr_compile(ast, reg, &err);
cxpr_field_value fast_result = cxpr_ir_eval(prog, ctx, reg, &err);
cxpr_program_free(prog);
```

## AST vs IR

`cxpr` has two execution paths for the same expression language:

- `cxpr_ast_eval(ast, ...)` evaluates the AST directly.
- `cxpr_compile(ast, ...)` builds a reusable compiled program, and `cxpr_ir_eval(...)` runs that program repeatedly.

The AST is the primary representation produced by parsing, and it is the main structure used for reference extraction, dependency analysis, validation, and error reporting. If you need to inspect or understand an expression structurally, you are working with the AST.

The IR is a compiled runtime plan built from that AST. Its job is narrower: make repeated evaluation cheaper. It does not define a second language, and it does not replace the AST in the library design. In the current implementation, a compiled program may mix native IR instructions with fallback steps that still delegate to the AST evaluator when that is the simplest way to preserve semantics.

In typical use, the flow is:

1. Parse text into an AST.
2. Use the AST for validation, introspection, or dependency work.
3. Compile the AST to IR when the same expression will run many times.
4. Reuse the compiled program across many context updates.

```c
cxpr_ast* ast = cxpr_parse(parser, "sqrt(x*x + y*y) > $limit", &err);
cxpr_program* prog = cxpr_compile(ast, reg, &err);

cxpr_field_value ast_value = cxpr_ast_eval(ast, ctx, reg, &err);
cxpr_field_value ir_value  = cxpr_ir_eval(prog, ctx, reg, &err);

cxpr_program_free(prog);
cxpr_ast_free(ast);
```

Use the compiled program path when the same parsed expression will be evaluated many times with different context values.

The local benchmark validates AST and compiled-program results on every iteration before reporting timings. Performance gains depend heavily on expression shape, registry setup, compiler settings, and workload. On one recent local run, the compiled path improved simple arithmetic to `1.42x`, native function calls to `4.32x`, expression-defined functions to `6.04x`, nested defined chains to `5.09x`, and deeper defined expressions to `3.82x`.

## Expression Syntax

```
# Arithmetic and power
(a + b) * c / d
sqrt(x^2 + y^2)

# Comparison and logical
rsi < 30 and volume > $min_volume
not emergency_stop and lidar_confidence > 0.8

# Ternary
signal > $threshold ? 1.0 : (signal < -$threshold ? -1.0 : 0.0)

# Field access
sqrt(body.vx^2 + body.vy^2)
distance3(goal.x, goal.y, goal.z, pose.x, pose.y, pose.z)
```

### Operators (low → high precedence)

| Precedence | Operator                       | Associativity |
| :--------: | ------------------------------ | :-----------: |
| 1          | `?:` ternary                   | right         |
| 2          | `or` `\|\|`                    | left          |
| 3          | `and` `&&`                     | left          |
| 4          | `not` `!` (unary)              | —             |
| 5          | `==` `!=`                      | left          |
| 6          | `<` `<=` `>` `>=`              | left          |
| 7          | `+` `-`                        | left          |
| 8          | `*` `/` `%`                    | left          |
| 9          | `-` `+` (unary)                | —             |
| 10         | `^` `**` power                 | right         |
| 11         | `()` grouping / function calls | —             |

## Custom Functions

For fixed-arity scalar functions, use the convenience wrappers:

```c
static double deg2rad(double d)                            { return d * M_PI / 180.0; }
static double clamp(double v, double lo, double hi)        { return v < lo ? lo : v > hi ? hi : v; }
static double rand_uniform(void)                           { return (double)rand() / RAND_MAX; }

cxpr_registry_add_unary(reg,   "deg2rad",      deg2rad);
cxpr_registry_add_ternary(reg, "clamp",        clamp);
cxpr_registry_add_nullary(reg, "rand_uniform", rand_uniform);
```

For variable arity or functions that need external state, use the general form:

```c
// userdata points to your application state — cast inside the function
static double fn_lookup(const double* args, size_t argc, void* userdata) {
    MyTable* table = (MyTable*)userdata;
    return mytable_lookup(table, (int)args[0]);
}

cxpr_registry_add(reg, "lookup", fn_lookup,
                  /*min_args=*/1, /*max_args=*/1,
                  my_table, /*free_fn=*/NULL);
```

Functions return `double`. Booleans are `1.0` / `0.0` by convention.

### Expression-Defined Functions

You can also register functions from expressions instead of C callbacks:

```c
cxpr_registry_define_fn(reg, "sq(x) => x * x");
cxpr_registry_define_fn(reg, "hyp2(a, b) => sqrt(sq(a) + sq(b))");
cxpr_registry_define_fn(reg, "clamp_score(x) => x > 1 ? 1 : (x < -1 ? -1 : x)");
```

Defined functions use the same expression language as normal formulas, can call built-ins, and can call other registered functions, including other defined functions.

The definition syntax is:

```text
name(param1, param2, ...) => expression_body
```

Examples:

```c
cxpr_registry_define_fn(reg, "sum(a, b) => a + b");
cxpr_registry_define_fn(reg, "lerp(a, b, t) => a + (b - a) * t");
cxpr_registry_define_fn(reg, "energy(m, v) => 0.5 * m * v * v");
```

Parameters can also behave like struct arguments when the body accesses fields from them. In that case the caller passes an identifier prefix and the fields are read from the context.

```c
cxpr_registry_define_fn(reg, "dot2(u, v) => u.x * v.x + u.y * v.y");
cxpr_registry_define_fn(reg, "len2(p) => sqrt(p.x * p.x + p.y * p.y)");
```

```text
dot2(goal, velocity)
len2(body)
```

This lets you write compact domain helpers without needing a C callback for every small formula. It is especially useful for:

- reusable scalar helpers like `sq`, `hyp2`, or `clamp_score`
- configurable host-defined DSL building blocks
- struct-aware helpers such as `dot2(u, v)` or `len2(body)`

If a function name is redefined, the latest definition replaces the previous one.

## Domain Examples

These examples are illustrative. They show how expressions and formula composition can map onto different domains, but they are not presented as validated real-world models, control laws, safety logic, or trading advice.

### Trading — signal composition

```c
// Intermediate values resolved in dependency order by the formula engine
const cxpr_formula_def defs[] = {
    { "trend",    "close > ema_fast and ema_fast > ema_slow" },
    { "pullback", "close < ema_fast * 0.995" },
    { "entry",    "trend and pullback and rsi > 50" }
};

cxpr_formulas_add(engine, defs, 3, &err);
cxpr_formula_compile(engine, &err);
cxpr_formula_eval_all(engine, ctx, &err);
cxpr_field_value entry = cxpr_formula_get(engine, "entry", NULL);
```

```text
# Direct expressions
rsi < 30 and close < lower_band and volume > $min_volume
cross_below(ema_fast, ema_slow, prev_ema_fast, prev_ema_slow) or atr / close > $max_vol_ratio
```

### Robotics — configurable guard conditions

```c
cxpr_context_set(ctx, "distance_front", 0.42);
cxpr_context_set(ctx, "battery",        76.0);
cxpr_context_set(ctx, "slip_ratio",     0.03);
cxpr_context_set_param(ctx, "stop_distance", 0.25);
cxpr_context_set_param(ctx, "max_slip",      0.10);
```

```text
distance_front < $stop_distance ? 0.0 : (battery > 20 ? max_speed : 0.0)
slip_ratio > $max_slip or abs(heading_error) > $max_heading_error
```

Register scalar helpers that collapse vec3/quat components into scalar outputs:

```c
static double fn_distance2(const double* args, size_t argc, void* ud) {
    double dx = args[0]-args[2], dy = args[1]-args[3];
    return sqrt(dx*dx + dy*dy);
}
static double fn_distance3(const double* args, size_t argc, void* ud) {
    double dx = args[0]-args[3], dy = args[1]-args[4], dz = args[2]-args[5];
    return sqrt(dx*dx + dy*dy + dz*dz);
}
const char* xy[]  = {"x", "y"};

cxpr_registry_add_fn(reg, "distance2", fn_distance2, xy,  2, 2, NULL, NULL);
cxpr_registry_add(reg, "distance3", fn_distance3, 6, 6, NULL, NULL);
```

```text
distance2(goal, pose) < $capture_radius
distance3(goal.x, goal.y, goal.z, pose.x, pose.y, pose.z) < $capture_radius
```

### Physics / simulation — analytical formulas

```c
const char* fields[] = {"x", "y", "vx", "vy"};
double vals[]        = {1.2, -0.5, 0.8, 1.1};
cxpr_context_set_fields(ctx, "body", fields, vals, 4);
```

```text
0.5 * mass * velocity^2
exp(-damping * t) * cos(omega * t)
sqrt(body.vx^2 + body.vy^2)
abs(acceleration) > $max_acceleration or temperature >= $meltdown_limit
```

### Gaming — combat, AI, and encounter scripting

```c
cxpr_context_set(ctx, "health",              18.0);
cxpr_context_set(ctx, "stamina",             42.0);
cxpr_context_set(ctx, "target_distance",      3.5);
cxpr_context_set(ctx, "incoming_dps",        11.0);
cxpr_context_set(ctx, "cooldown_dash",        0.0);
cxpr_context_set_param(ctx, "panic_health",  25.0);
cxpr_context_set_param(ctx, "melee_range",    2.2);
```

```text
health < $panic_health and cooldown_dash == 0
target_distance < $melee_range and stamina > 20 and not is_stunned
incoming_dps * time_to_cover > health ? retreat_weight : engage_weight
loot_rarity >= $legendary_floor and player_level >= area_level - 2
```

### Industrial monitoring — alarms and operating envelopes

```c
cxpr_context_set(ctx, "pressure",         118.0);
cxpr_context_set(ctx, "temperature",       82.0);
cxpr_context_set(ctx, "temperature_rate",   1.7);
cxpr_context_set(ctx, "vibration_rms",      0.42);
cxpr_context_set_param(ctx, "max_pressure", 120.0);
cxpr_context_set_param(ctx, "max_temp_rate", 1.2);
```

```text
pressure > $max_pressure or temperature_rate > $max_temp_rate
vibration_rms > $bearing_limit and rpm > $inspection_rpm
temperature > $warn_temp ? warn_level : 0
```

### Drone navigation — waypoints, altitude windows, and return-home rules

```c
const char* xyz[] = {"x", "y", "z"};
double pose_xyz[]     = {128.0, 96.0, 42.0};
double waypoint_xyz[] = {160.0, 120.0, 45.0};
double home_xyz[]     = {100.0, 80.0, 20.0};

cxpr_context_set_fields(ctx, "pose",     xyz, pose_xyz,     3);
cxpr_context_set_fields(ctx, "waypoint", xyz, waypoint_xyz, 3);
cxpr_context_set_fields(ctx, "home",     xyz, home_xyz,     3);
cxpr_context_set(ctx, "battery",        31.0);
cxpr_context_set(ctx, "ground_speed",    8.5);
cxpr_context_set(ctx, "wind_speed",      6.0);
cxpr_context_set(ctx, "heading_error",   4.0);
cxpr_context_set_param(ctx, "capture_radius",  6.0);
cxpr_context_set_param(ctx, "min_altitude",   20.0);
cxpr_context_set_param(ctx, "max_altitude",  120.0);
cxpr_context_set_param(ctx, "rth_battery",    18.0);
```

```text
distance3(waypoint.x, waypoint.y, waypoint.z, pose.x, pose.y, pose.z) < $capture_radius
pose.z >= $min_altitude and pose.z <= $max_altitude
battery < $rth_battery or wind_speed > $max_wind_speed
abs(heading_error) > $max_heading_error ? reduced_speed : cruise_speed
ground_speed > 0 ? distance3(home.x, home.y, home.z, pose.x, pose.y, pose.z) / ground_speed : 1e9
```

### Other fits

```text
# Pricing / risk
exposure > $limit and sqrt(var_1d) > $risk_budget

# Feature flags / rollout
country == $target_country and app_version >= $min_version and not is_internal

# Fraud / abuse heuristics
failed_logins > $max_failures and device_age_hours < $new_device_window
```

## Formula Engine

`cxpr_formula_engine` allows formulas to reference each other. It performs a topological sort at compile time so formulas can be evaluated in dependency order. The engine still uses ASTs for dependency analysis and formula structure, but it also compiles each formula to an internal program during `cxpr_formula_compile(...)` and uses that compiled path during evaluation when available. Use `cxpr_formula_add(...)` for incremental registration or `cxpr_formulas_add(...)` when you already have an array of definitions and want atomic rollback on parse failure.

```c
cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
const char* quote_fields[] = {"mid", "spread"};

cxpr_registry_add_struct(reg, "quote2", quote2_producer,
                         2, 2, quote_fields, 2, NULL, NULL);

const cxpr_formula_def defs[] = {
    { "quote", "quote2(bid, ask)" },
    { "wide",  "quote.spread > $threshold" },
    { "entry", "wide and quote.mid > $min_mid" },
    { "score", "quote.mid + quote.spread * 10" }
};

if (!cxpr_formulas_add(engine, defs, 4, &err)) {
    fprintf(stderr, "add error: %s\n", err.message);
}

if (!cxpr_formula_compile(engine, &err)) {
    fprintf(stderr, "compile error: %s\n", err.message);  // circular dep, etc.
}

cxpr_formula_eval_all(engine, ctx, &err);
cxpr_field_value quote = cxpr_formula_get(engine, "quote", NULL);  // struct
cxpr_field_value entry = cxpr_formula_get(engine, "entry", NULL);  // bool
double score = cxpr_formula_get_double(engine, "score", NULL);     // double

cxpr_formula_engine_free(engine);
```

## Building

**Requirements:** CMake 3.20+, C11 compiler (GCC or Clang)

```bash
cmake --preset default
cmake --build --preset default
cmake --build build --target test
```

Single-config generators default to `Release` when no build type is specified.
Use `cmake --build <build-dir> --target test` as the authoritative test command for this repo.
It goes through the generator-native test target and avoids environment-specific `ctest`
launcher issues.

For a strict local build with warnings-as-errors:

```bash
cmake --preset strict
cmake --build --preset strict
cmake --build build-strict --target test
```

For sanitizer runs:

```bash
cmake --preset asan
cmake --build --preset asan
cmake --build build-asan --target test
```

```bash
cmake --preset ubsan
cmake --build --preset ubsan
cmake --build build-ubsan --target test
```

To build and run the local benchmark:

```bash
cmake -S . -B build -DCXPR_BUILD_BENCHMARKS=ON
cmake --build build
./build/benchmarks/cxpr_bench_ir
```

The benchmark checks AST vs IR equivalence per iteration and aborts on the first mismatch before reporting timing results.

## Tests

The recommended way to run tests in this repository is:

```bash
cmake --build build --target test
```

For strict and sanitizer builds, use the matching build directory:

```bash
cmake --build build-strict --target test
cmake --build build-asan --target test
cmake --build build-ubsan --target test
```

If `ctest` works in your environment, it should report the same test results, but it is not
the canonical command documented or used by CI.

The repository currently builds 20 standalone C test executables using `assert()` and no external test framework.

| Area            | Coverage                                                                 |
| --------------- | ------------------------------------------------------------------------ |
| lexer / parser  | Tokenization, precedence, AST construction, reference extraction         |
| eval            | Runtime evaluation, variables, parameters, field access, errors          |
| formulas        | Dependency resolution, compile/eval flow, cycle detection                |
| IR              | Lowering, execution, ownership/reset, cache invalidation, parity checks  |
| structs         | Context-backed structs, struct-aware functions, producer access          |
| examples        | Executable README snippets kept in sync with the documented API          |

## Project Structure

```text
cxpr/
├── include/
│   └── cxpr/
│       ├── ast.h             # Public AST inspection helpers
│       └── cxpr.h            # Public C11 API
├── src/
│   ├── internal.h            # Shared internal types and declarations
│   ├── hashmap.c             # Internal string-keyed storage
│   ├── lexer.c               # Tokenizer
│   ├── parser.c              # Recursive descent parser -> AST
│   ├── ast.c                 # AST construction and reference extraction
│   ├── eval.c                # Tree-walk evaluator
│   ├── context.c             # Variables, params, typed field values
│   ├── registry.c            # Function registry, built-ins, defined functions
│   ├── struct.c              # Struct field expansion and producers
│   ├── formula.c             # Formula engine and dependency ordering
│   └── ir/
│       ├── compile.c         # AST -> internal program lowering
│       ├── exec.c            # Internal program execution
│       └── internal.h        # IR-local internals
├── benchmarks/
│   ├── CMakeLists.txt
│   └── ir_bench.c            # AST vs IR benchmark with validation
├── tests/
│   ├── CMakeLists.txt
│   ├── chain_access.test.c   # Nested field access and chaining
│   ├── define.test.c         # Defined functions and replacement
│   ├── errors.test.c         # Error codes, parse/eval failures
│   ├── eval.test.c           # Evaluator coverage and runtime behavior
│   ├── field_value.test.c    # Typed field value helpers
│   ├── formula.test.c        # Formula engine behavior
│   ├── formula_ir.test.c     # Formula-engine compiled path
│   ├── ir.test.c             # IR compile/execution coverage
│   ├── ir_ownership.test.c   # IR ownership and cleanup
│   ├── lexer.test.c          # Tokenization and lexing edge cases
│   ├── math.test.c           # Built-in math and custom functions
│   ├── parser.test.c         # Parse tree construction
│   ├── physics_simulation.test.c
│   ├── precedence.test.c     # Operator precedence and associativity
│   ├── program.test.c        # Public compiled-program API tests
│   ├── readme.test.c         # README examples kept executable
│   ├── simulation.test.c     # End-to-end indicator/signal simulation
│   ├── struct_ctx.test.c     # Struct values stored in context
│   ├── struct_fn.test.c      # Struct-aware function expansion
│   └── struct_producer.test.c# Struct-producing functions and access
├── cmake/
│   └── cxprConfig.cmake.in
└── CMakeLists.txt
```

The public API is documented in `include/cxpr/cxpr.h`.
