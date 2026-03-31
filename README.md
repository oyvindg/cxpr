# cxpr

[![CI](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml/badge.svg)](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml)

Generic expression parser, evaluator, and formula engine written in C11. No external dependencies.

## Features

- Full expression grammar — arithmetic, comparison, logical, ternary, function calls
- Runtime variables (`context`) and compile-time parameters (`$param`)
- Custom function registry with arity validation and optional userdata
- 30+ built-in math functions (`sin`, `cos`, `sqrt`, `pow`, `min`, `max`, `log`, ...)
- `FormulaEngine` — multi-formula dependency resolution with topological sort and cycle detection
- AST inspection API — extract variable, parameter, and function references
- Structured error handling — error codes, messages, and source position
- C11 ABI — suitable for FFI (Python, Rust, Go, ...) and embedding

## Installation

### As a git submodule

```bash
git submodule add https://github.com/oyvindg/cxpr.git extern/cxpr
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

Start with the AST path. It is the simplest way to parse and evaluate an expression, and it matches the core mental model of the library.

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

    double result = cxpr_ast_eval(ast, ctx, reg, &err);  // → 1.0

    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
}
```

When the same parsed expression will be evaluated repeatedly with changing context values, compile it once and use the IR path:

```c
cxpr_program* prog = cxpr_compile(ast, reg, &err);
double fast_result = cxpr_ir_eval(prog, ctx, reg, &err);
cxpr_program_free(prog);
```

## AST vs IR

`cxpr` has two execution paths for the same expression language:

- `cxpr_ast_eval(ast, ...)` evaluates the AST directly.
- `cxpr_compile(ast, ...)` builds a reusable compiled program, and `cxpr_ir_eval(...)` runs that program repeatedly.

The AST is still the primary representation. It is what parsing produces, and it remains the source of truth for reference extraction, dependency analysis, validation, and error reporting. If you need to inspect or understand an expression structurally, you are working with the AST.

The IR is a compiled runtime plan built from that AST. Its job is narrower: make repeated evaluation cheaper. It does not define a second language, and it does not replace the AST in the library design. In the current implementation, a compiled program may mix native IR instructions with fallback steps that still delegate to the AST evaluator when that is the simplest way to preserve semantics.

In practice, the intended flow is:

1. Parse text into an AST.
2. Use the AST for validation, introspection, or dependency work.
3. Compile the AST to IR when the same expression will run many times.
4. Reuse the compiled program across many context updates.

```c
cxpr_ast* ast = cxpr_parse(parser, "sqrt(x*x + y*y) > $limit", &err);
cxpr_program* prog = cxpr_compile(ast, reg, &err);

double ast_value = cxpr_ast_eval(ast, ctx, reg, &err);
double ir_value  = cxpr_ir_eval(prog, ctx, reg, &err);

cxpr_program_free(prog);
cxpr_ast_free(ast);
```

Use the compiled program path when the same parsed expression will be evaluated many times with different context values.

The local benchmark validates AST and compiled-program results on every iteration before reporting timings. On one recent local run, the compiled path improved simple arithmetic to `1.42x`, native function calls to `4.32x`, expression-defined functions to `6.04x`, nested defined chains to `5.09x`, and deeper defined expressions to `3.82x`.

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

### Trading — signal composition

```c
// Intermediate values resolved in dependency order by FormulaEngine
cxpr_formula_add(engine, "trend",   "close > ema_fast and ema_fast > ema_slow", &err);
cxpr_formula_add(engine, "pullback","close < ema_fast * 0.995", &err);
cxpr_formula_add(engine, "entry",   "trend and pullback and rsi > 50", &err);

cxpr_formula_eval_all(engine, ctx, &err);
double entry = cxpr_formula_get(engine, "entry", NULL);
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
distance_front < $stop_distance ? 0.0 : max_speed * (battery > 20)
slip_ratio > $max_slip or abs(heading_error) > $max_heading_error
```

Register scalar helpers that collapse vec3/quat components into scalar outputs:

```c
static double fn_distance3(const double* args, size_t argc, void* ud) {
    double dx = args[0]-args[3], dy = args[1]-args[4], dz = args[2]-args[5];
    return sqrt(dx*dx + dy*dy + dz*dz);
}
cxpr_registry_add(reg, "distance3", fn_distance3, 6, 6, NULL, NULL);
```

```text
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

### Other fits

```text
# Industrial monitoring
pressure > $max_pressure or temperature_rate > $max_temp_rate

# Game logic
health < 20 and cooldown == 0 ? 1 : 0

# Pricing / risk
exposure > $limit and sqrt(var_1d) > $risk_budget
```

## Formula Engine

`FormulaEngine` allows formulas to reference each other. It performs a topological sort at compile time so evaluation always runs in correct dependency order. The engine still uses ASTs for dependency analysis and formula structure, but it now compiles each formula to a reusable internal program during `cxpr_formula_compile(...)` and uses that compiled path during evaluation.

```c
cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);

cxpr_formula_add(engine, "spread", "ask - bid",               &err);
cxpr_formula_add(engine, "mid",    "(ask + bid) / 2",         &err);
cxpr_formula_add(engine, "signal", "spread / mid > $threshold", &err);

if (!cxpr_formula_compile(engine, &err)) {
    fprintf(stderr, "compile error: %s\n", err.message);  // circular dep, etc.
}

cxpr_formula_eval_all(engine, ctx, &err);
double signal = cxpr_formula_get(engine, "signal", NULL);

cxpr_formula_engine_free(engine);
```

## Building

**Requirements:** CMake 3.20+, C11 compiler (GCC or Clang)

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --verbose
```

Single-config generators default to `Release` when no build type is specified.

To build and run the local benchmark:

```bash
cmake -S . -B build -DCXPR_BUILD_BENCHMARKS=ON
cmake --build build
./build/benchmarks/cxpr_bench_ir
```

The benchmark checks AST vs IR equivalence per iteration and aborts on the first mismatch before reporting timing results.

## Tests

134 test cases across 8 suites using `assert()` — no external test framework.

| Suite      | Tests | Description                                              |
| ---------- | ----: | -------------------------------------------------------- |
| lexer      |    26 | Tokenization, keywords, operators, position tracking     |
| parser     |    25 | AST construction, reference extraction, error recovery   |
| eval       |    21 | Evaluation, variables, parameters, functions, errors     |
| math       |    16 | Built-in math, custom functions, deeply nested exprs     |
| precedence |    14 | Operator precedence, associativity                       |
| formula    |     8 | Dependency resolution, topological sort, cycle detection |
| errors     |    17 | Error codes, messages, source position tracking          |
| simulation |     7 | 200 OHLC bars with SMA/EMA/RSI/Bollinger/ATR strategies  |

## Project Structure

```text
cxpr/
├── include/
│   └── cxpr/
│       └── cxpr.h        # Public C11 API
├── src/
│   ├── internal.h        # Internal data structures and IR types
│   ├── lexer.c           # Tokenizer
│   ├── parser.c          # Recursive descent parser -> AST
│   ├── ast.c             # AST construction, inspection, reference extraction
│   ├── eval.c            # Recursive tree-walk evaluator
│   ├── ir.c              # Internal compiled IR / program evaluator
│   ├── context.c         # Variable/parameter storage
│   ├── registry.c        # Function registry, built-ins, defined functions
│   └── formula.c         # FormulaEngine with dependency ordering + program caching
├── benchmarks/
│   ├── CMakeLists.txt
│   └── ir_bench.c        # AST vs IR benchmark with per-iteration validation
├── tests/
│   ├── CMakeLists.txt
│   ├── define.test.c     # cxpr_registry_define_fn and nested defined functions
│   ├── errors.test.c     # Error codes, parse/eval failures
│   ├── eval.test.c       # Tree-walk evaluator coverage
│   ├── formula.test.c    # FormulaEngine behavior
│   ├── formula_ir.test.c # Compiled FormulaEngine path
│   ├── ir.test.c         # Internal IR compile/eval parity
│   ├── lexer.test.c
│   ├── math.test.c
│   ├── parser.test.c
│   ├── precedence.test.c
│   ├── program.test.c    # Public compiled-program API tests
│   ├── simulation.test.c
│   └── struct_fn.test.c  # Struct-aware function expansion
└── CMakeLists.txt
```

The public API is fully documented in `include/cxpr/cxpr.h`.

## License

MIT
