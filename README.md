# cxpr

[![CI](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml/badge.svg)](https://github.com/oyvindg/cxpr/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

`cxpr` is a standalone C11 library for expressions, executable models, and
build-time model artifacts. It has no external runtime dependencies.

The library has three related surfaces:

1. The expression API parses and evaluates one expression at a time.
2. The `.cxpr` model API describes named inputs, parameters, functions, state,
   derived values, and outputs that execute together.
3. The plugin API turns parsed or compiled models into artifacts such as C
   source, CUDA source, metadata manifests, and model graphs.

The core is domain-independent. A host supplies data, registered functions,
source resolution, scheduling, and policy. The same language can therefore
describe a control rule, a scientific calculation, a game entity, an SLO
condition, or a trading signal without adding those domains to `cxpr`.

```cxpr
# robotics
stable = imu_ok and abs(roll) <= $max_roll

# operations
within(latency_ms, 0, $budget_ms) and error_rate < $max_error_rate

# trading
close > ema(close, 20) and volume > $min_volume
```

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
| `cxpr_document_ast` | Source-oriented tree for a complete `.cxpr` document |
| `cxpr_document` | Parsed document containing host blocks and optionally a model |
| `cxpr_model` | Validated semantic representation of model declarations |
| `cxpr_program` | Compiled typed IR for one expression |
| `cxpr_model_program` | Immutable compiled plan for a complete model |
| `cxpr_context` | Runtime variables, parameters, structs, slots, and overlays |
| `cxpr_registry` | Built-in, expression-defined, and host-defined functions |
| `cxpr_model_session` | Mutable state for one execution of a model program |
| `cxpr_engine_program` | Immutable plan for the optional rule-engine layer |
| `cxpr_engine_session` | Mutable cursor, history, source, and watch state |

## Build

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Available presets:

| Preset | Purpose |
| --- | --- |
| `default` | Release build |
| `strict` | Strict warnings with `-Werror` or `/WX` |
| `asan` | AddressSanitizer debug build |
| `ubsan` | UndefinedBehaviorSanitizer debug build |
| `coverage` | Coverage instrumentation |
| `fuzz` | libFuzzer with ASan/UBSan; requires Clang |

Useful CMake options:

| Option | Default when top-level | Purpose |
| --- | --- | --- |
| `CXPR_BUILD_TESTS` | `ON` | Build tests and tested examples |
| `CXPR_BUILD_BENCHMARKS` | `ON` | Build benchmarks |
| `CXPR_BUILD_TOOLS` | `ON` | Build model codegen and document tooling |
| `CXPR_BUILD_FUZZERS` | `OFF` | Build the parser fuzzer |

Embed the library with CMake:

```cmake
add_subdirectory(external/cxpr)
target_link_libraries(my_target PRIVATE cxpr::cxpr)
```

Installed packages export the same target:

```cmake
find_package(cxpr CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE cxpr::cxpr)
```

The umbrella header is:

```c
#include <cxpr/cxpr.h>
```

Focused headers such as `<cxpr/parser.h>`, `<cxpr/model/model.h>`, or
`<cxpr/plugins/cuda.h>` may be included directly.

## Expression language

Expressions are the common calculation language used by the low-level API and
inside `.cxpr` models.

### Values and references

```cxpr
42
3.14159
true
false
"sensor-a"
null
temperature
$warning_limit
pose.position.x
[1, 2, $fallback]
```

Runtime names such as `temperature` are read from a `cxpr_context`. Names
prefixed with `$` are parameters. Field access reads struct-like values.
Bracketed lists are first-class arrays.

`null` is distinct from numeric zero. Use `isnull(x)`, `notnull(x)`, and
`coalesce(x, fallback)` when missing values are expected.

### Operators

From lowest to highest precedence:

| Precedence | Operators |
| --- | --- |
| conditional | `condition ? yes : no` |
| logical OR | `or` |
| logical AND | `and` |
| comparison | `==`, `!=`, `<`, `<=`, `>`, `>=`, `in` |
| additive | `+`, `-` |
| multiplicative | `*`, `/`, `%` |
| power | `^`, `**` |
| unary | `not`, unary `-` |
| postfix | function call, field access, lookback |

Boolean positions require boolean values. Numeric truthiness is not supported:

```cxpr
temperature > 80 and fan_ready   # valid when fan_ready is bool
not retry_count                  # invalid: retry_count is numeric
retry_count == 0                 # valid
```

`and` and `or` short-circuit. Power is right-associative. Parentheses should be
used when an expression is intended to communicate policy rather than merely
arithmetic precedence.

### Functions, named arguments, and pipes

```cxpr
sqrt(x*x + y*y)
within(source=latency_ms, min=0, max=$budget_ms)
close | ema(20)
```

The pipe form passes its left-hand value as the first argument. The registry
determines which functions exist and whether positional or named arguments are
valid.

The default registry includes common arithmetic, trigonometric, rounding,
predicate, membership, and time-series helpers. Representative functions
include:

```text
abs sqrt pow exp log log10 floor ceil round sign
sin cos tan asin acos atan atan2 radians degrees
min max clamp within contains
isnull notnull coalesce
sum mean stddev count any all
rising falling repeat
```

Some functions require host data, lookback resolution, a role/basket binding,
or explicit opt-in registration. Function availability is therefore a
property of the registry used for compilation and evaluation, not just of the
grammar.

### Sets, arrays, structs, and lookback

`in` means set membership:

```cxpr
status in ["ready", "degraded"]
contains(status, $allowed_states)
```

Continuous range checks use `within`:

```cxpr
within(temperature, $minimum, $maximum)
within(source=x, min=0, max=1)
```

Struct-like values can be returned by C callbacks or expression-defined
functions:

```cxpr
pose.position.x
bands.lower
```

Lookback syntax asks the installed resolver or model/engine history layer for a
prior value:

```cxpr
temperature[1]
bands.lower[2]
price($instrument)[1]
```

A bare expression evaluator does not invent history. The host must install a
lookback resolver, use the reusable column lookback helper, or use a model or
engine session that owns the required history.

### Custom functions

Functions are registered in a `cxpr_registry`. The API supports scalar
callbacks, typed-value callbacks, AST handlers, time-series functions,
struct-producing functions, basket functions, and expression-defined
functions.

```c
#include <cxpr/cxpr.h>
#include <math.h>

static double deadband(double x, double width) {
    return fabs(x) <= width ? 0.0 : x;
}

int main(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_registry_add_binary(reg, "deadband", deadband);
    /* Parse, compile, and evaluate "deadband(error, $width)". */
    cxpr_registry_free(reg);
}
```

Registries and compiled programs are intended to become immutable before they
are shared. Use a separate context or session for each concurrent execution.

## Low-level expression API

Use the expression API when the host owns the execution loop and needs one
formula rather than a complete model.

```c
#include <cxpr/cxpr.h>
#include <stdio.h>

int main(void) {
    cxpr_error err = {0};
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(
        parser,
        "sqrt(vx^2 + vy^2) > $max_speed",
        &err);

    cxpr_context_set(ctx, "vx", 3.0);
    cxpr_context_set(ctx, "vy", 4.0);
    cxpr_context_set_param(ctx, "max_speed", 4.5);

    cxpr_program* program = cxpr_compile(ast, reg, &err);
    bool exceeded = false;
    if (program) {
        exceeded = cxpr_eval_program_bool(program, ctx, reg, &err);
    }

    printf("speed exceeded: %s\n", exceeded ? "yes" : "no");

    cxpr_program_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);
    return err.code == CXPR_OK ? 0 : 1;
}
```

The tree evaluator is useful for diagnostics and fallback paths. Compile to a
`cxpr_program` when an expression is evaluated repeatedly.

## The `.cxpr` document and model language

A `.cxpr` file is parsed in one of two modes:

- A **manifest document** contains host-defined blocks such as `project`,
  `tooling`, or `profile`. Model statements are rejected.
- A **model document** enables the model extension and may contain both host
  blocks and executable model statements.

Use `cxpr_parse_manifest` / `cxpr_load_manifest_file` for manifests and
`cxpr_parse_model_document` / `cxpr_load_model_document_file` for model
documents. `cxpr_parse_model_source` is the direct owning entry point when only
the semantic model is needed.

### Complete model example

This stateful controller is deliberately domain-neutral at the runtime
boundary: the host writes two inputs and reads three outputs.

```cxpr
model thermal_controller {
    category = "control"
}

in {
    temperature,
    sensor_ok
}

$target = 70
$deadband = 2
$max_integral = 100

fn clamp_value(x, lo, hi) = max(lo, min(hi, x))

state {
    integral = 0
    previous_alarm = false
}

error = temperature - $target
next_integral = clamp_value(integral + error, 0 - $max_integral, $max_integral)
heating = sensor_ok and error < 0 - $deadband
alarm = not sensor_ok or temperature > $target + 20

integral := next_integral
previous_alarm := alarm

out {
    heating,
    alarm,
    integral
}
```

The model declares a stable external shape:

- `in` names values supplied by the host for each tick;
- `$name = expression` declares a parameter with a default expression;
- `fn` declares reusable model-local functions;
- `state` declares values retained by one session;
- ordinary `name = expression` bindings are dependency-ordered calculations;
- `state_name := expression` stages a state update;
- `out` declares the public results and their order.

Model programs are immutable and shareable. A model session owns mutable state
for one device, simulation, entity, strategy instance, or other independent
execution.

### Model declaration forms

Inputs, parameters, state, and outputs accept compact or block forms:

```cxpr
in source
in { x, y, enabled }

$period = 20
$threshold = 0.8 { description = "Decision threshold" }

state accumulator = 0
state {
    accumulator = 0
    initialized = false
}

out score
out { score, accepted }
```

State can also be declared and updated locally:

```cxpr
bars := bars + 1 initial 0
```

This is equivalent to declaring `bars` in `state` and later staging
`bars := bars + 1`. The local form is preferred for new models; the block form
remains supported for compatibility and for grouping larger state schemas.

State updates are atomic with respect to a tick: calculations read the current
state, update expressions are staged, and commits become the next tick's state.
This prevents statement order from accidentally changing state semantics.

### Functions and records

Expression-bodied functions return one value:

```cxpr
fn distance(p) = sqrt(p.x*p.x + p.y*p.y + p.z*p.z)
```

Block-bodied functions may define locals and return a record:

```cxpr
fn normalize2(v, epsilon) {
    length = sqrt(v.x*v.x + v.y*v.y)
    invalid = length <= epsilon
    return {
        x = invalid ? 0 : v.x / length,
        y = invalid ? 0 : v.y / length
    }
}
```

Record shorthand uses local names as field names:

```cxpr
fn point3(x, y, z) {
    return { x, y, z }
}
```

### Imports

`use` imports reusable `.cxpr` functions or child models:

```cxpr
use filters
use indicators/ema as ema_lib
```

Library callers can resolve imports explicitly with
`cxpr_compile_model_with_imports*` or the import bundle APIs. The
`cxpr_model_codegen` tool resolves adjacent `.cxpr` imports and also probes
ancestor `libs/dyn/cxpr/` directories for project integrations.

Imports are resolved before model execution. Runtime expression parsing is not
required for generated-model consumers.

### Metadata and host blocks

Blocks attached to models, parameters, bindings, or outputs preserve
host-defined metadata:

```cxpr
model latency_guard {
    domain = "operations"
}

$budget_ms = 200 {
    label = "Latency budget"
    unit = "ms"
}

out healthy {
    label = "Within SLO"
    plot { color = "green" }
}
```

The document parser owns syntax and source spans; the host owns the schema and
meaning. Register allowed block kinds with `cxpr_host_block_registry` and
validate them with `cxpr_document_validate_host_blocks` or
`cxpr_model_validate_host_blocks`.

This separation lets editors and build tools understand document structure
without placing domain-specific chart, deployment, or UI semantics in the
core library.

### Model execution

The reference/tooling path is:

```c
cxpr_error err = {0};
cxpr_model* model = cxpr_parse_model_source(source, &err);
cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
cxpr_model_session* session = cxpr_model_session_new(program, NULL, &err);
cxpr_context* ctx = cxpr_model_session_context(session);

cxpr_context_set(ctx, "temperature", 73.0);
cxpr_context_set_bool(ctx, "sensor_ok", true);
cxpr_model_session_tick(program, session, NULL, &err);

bool alarm = false;
cxpr_model_session_output_bool(session, "alarm", &alarm);

cxpr_model_session_free(session);
cxpr_model_program_free(program);
cxpr_model_free(model);
```

`cxpr_model_session_tick` is intended for diagnostics, editor tooling, parity
tests, and explicit fallback paths. `cxpr_model_session_tick_fast` avoids some
context materialization for supported scalar models. For production,
backtesting, or optimizer hot loops, generate and compile C when the model is
compatible with the generated backend.

Compile options select `AUTO`, `IR`, or `C`, allow or disable fusion, and may
request trace-friendly evaluation. `AUTO` chooses the supported path and
retains evaluator fallback for model shapes that cannot use the scalar fast
path.

## AST, analysis, type checking, and IR

There are two public syntax-tree views:

- The expression AST represents operators, calls, values, fields, and lookback
  within one expression. It can be parsed, deep-cloned, or constructed
  programmatically through the public `cxpr_expr_ast_new_*` functions.
- The document AST preserves the complete `.cxpr` file structure, statement
  order, nested blocks, expression subtrees, text, and source spans.

Expression-AST accessors return borrowed children and values, but the public
constructors create owned trees:

```c
cxpr_expr_ast* condition = cxpr_expr_ast_binary_new(
    CXPR_TOK_GT,
    cxpr_expr_ast_identifier_new("temperature"),
    cxpr_expr_ast_param_new("limit"));

/* Compile, analyze, evaluate, or emit C from condition. */
cxpr_expr_ast_free(condition);
```

Constructors that accept child nodes take ownership as documented in
`<cxpr/ast/expression.h>`. The document AST has no corresponding public
mutation/builder API; it is produced by parsing and inspected through const
accessors and the visitor API.

The document AST can be visited without lowering:

```c
cxpr_document_ast* ast = cxpr_parse_document_ast(
    source, "controller.cxpr", CXPR_DOCUMENT_EXTENSION_MODEL, &err);
cxpr_document_ast_visit(ast, visitor, userdata);
cxpr_document_ast_free(ast);
```

Lowering converts source-oriented syntax into a semantic `cxpr_model`.
Validation then checks declarations, references, outputs, parameters, imports,
host blocks, and dependency cycles as applicable.

Expression analysis reports referenced variables, parameters, functions,
producer fields, result shape, short-circuit behavior, and maximum literal
lookback depth. The shared type checker enforces the same boolean/numeric rules
for tree evaluation, typed IR, model compilation, engine watches, baskets, and
C code generation.

`cxpr_compile` lowers a supported expression AST into typed stack IR. The
executor includes optimized paths for common scalar operations and falls back
to registered AST-level behavior where required.

The public `<cxpr/ir.h>` API exposes a versioned, read-only instruction view:

```c
for (size_t i = 0; i < cxpr_ir_view_count(program); ++i) {
    cxpr_ir_view_instr instruction;
    if (cxpr_ir_view_instr_at(program, i, &instruction)) {
        printf("%s\n", cxpr_ir_view_opcode_name(instruction.op));
    }
}
```

This view is diagnostic:

- returned pointers are borrowed from the program;
- opcode tags are not a serialized bytecode format;
- internal storage and executor details remain private;
- persist `.cxpr` source or generated artifacts, not instruction sequences.

## C code generation

There are two C-generation levels.

For individual expressions, `<cxpr/codegen.h>` emits a C-like expression or a
function from an expression set:

```c
char* code = cxpr_expr_ast_to_c(ast, NULL, &err);
/* Example: ((2 * (G * M)) / pow(c, 2)) */
free(code);
```

`cxpr_c_target` maps function names and provides optional leaf and call hooks.
It can adapt the traversal to C-like targets while keeping memory layout and
host source access outside `cxpr`.

For complete models, `cxpr_model_program_to_c_tick_function*` emits a stateful
tick function with this logical ABI:

```c
void model_tick(
    model_tick_state* state,
    const double* inputs,
    const double* params,
    double* outputs);
```

Generated variants can:

- emit all or selected outputs;
- bake parameter values into the generated source;
- specialize windows whose periods become compile-time constants;
- emit a stable `cxpr_generated_model_descriptor` containing names, counts,
  defaults, state size, and tick callbacks.

Generated descriptor ABI version 3 supports at most 64 inputs, 64 outputs, and
64 public parameters. Consumers must validate descriptors with
`cxpr_generated_model_descriptor_abi_valid`.

### Command-line model codegen

Build-time model generation:

```bash
./build/cxpr_model_codegen \
  --model controller.cxpr \
  --output controller.gen.c \
  --function controller_tick \
  --qualifiers "static inline"
```

Optional outputs and specialization:

```bash
./build/cxpr_model_codegen \
  --model controller.cxpr \
  --output controller.gen.c \
  --meta-output controller.meta.json \
  --graph-output controller.graph.json \
  --outputs heating,alarm \
  --specialize-defaults
```

`--specialize-defaults` bakes model defaults into generated expressions.
`--outputs` narrows the generated output array while preserving required state
updates and history.

## CUDA and other accelerator targets

The CUDA plugin emits CUDA source for a compiled model:

```c
#include <cxpr/plugins/cuda.h>

cxpr_cuda_plugin_options options = {
    .function_name = "controller_device_tick",
    .qualifiers = "static __device__ inline",
};

char* source = cxpr_cuda_plugin_source_from_program(program, &options, &err);
/* Compile or compose source with the host's CUDA toolchain. */
cxpr_cuda_plugin_source_free(source);
```

This is source generation, not a GPU runtime. The host remains responsible for:

- choosing device memory layout;
- composing a kernel or calling device function;
- compiling with NVCC, NVRTC, or its own build pipeline;
- transferring inputs, parameters, state, and outputs;
- launching work and reporting device errors.

Only model shapes supported by the code generator can be emitted. Host-backed
callbacks, dynamic sources, or other runtime-only behavior require lowering
hooks, generated inputs, or a CPU fallback.

The plugin contract is accelerator-neutral. A HIP, OpenCL, Metal, SYCL, WGSL,
or other backend can implement `cxpr_plugin_backend` and emit its own artifact.
The existing `cxpr_c_target` hooks also support adapting individual expression
emission to C-like targets. Those backends are extension points; only the C and
CUDA source plugins are included today.

## Plugins

A plugin consumes a borrowed model event:

```c
typedef struct cxpr_plugin_model_event {
    const char* model_path;
    const cxpr_model* model;
    const cxpr_model_program* program;
} cxpr_plugin_model_event;
```

It emits one or more artifacts through three host callbacks:

```text
begin_artifact(metadata)
  -> write_artifact(bytes) one or more times
  -> end_artifact()
```

The host decides whether those bytes go to a file, memory buffer, cache,
generated-source directory, package, or network service. A plugin identifies
each artifact with a stable name, kind, and optional path hint.

Included plugins:

| Plugin | Input | Artifact |
| --- | --- | --- |
| C | compiled model | C source |
| CUDA | compiled model | CUDA source |
| Graph | parsed model | renderer-neutral dependency graph JSON (`cxpr.graph.v1`) |
| Meta | parsed model | declarations and metadata JSON (`cxpr.meta.manifest.v1`) |

Run a backend generically:

```c
cxpr_plugin_model_event event = {
    .model_path = "controller.cxpr",
    .model = model,
    .program = program,
};

cxpr_plugin_run_model_backend(
    &event,
    cxpr_c_plugin_backend(),
    &options,
    &host_callbacks,
    &err);
```

Backends receive an opaque options pointer through the generic contract. Their
public headers define the actual option struct. This keeps the core plugin API
independent of code generators, GPUs, renderers, and application domains.

## Rule-engine layer

`<cxpr/engine.h>` is an optional higher-level layer for hosts that want `cxpr`
to own repeated execution mechanics.

```text
cxpr_engine_config
  -> cxpr_engine_program       immutable and shareable
  -> cxpr_engine_session       mutable state for one run
  -> cxpr_engine_tick()        hydrate, evaluate, detect, emit
```

The config declares named expressions, sources, default params, role bindings,
and watches. The engine provides:

- dependency-ordered expression evaluation;
- lazy pull, callback-view, and direct-column source hydration;
- lookback for engine sources and tracked expression results;
- per-session parameter overrides and role membership;
- rising, falling, level, and changed events.

The host still interprets events. The engine has no built-in trading,
monitoring, robotics, or IO action semantics.

Use the lower-level expression API when the host already owns scheduling. Use a
`.cxpr` model when a calculation has a declared input/output/state contract.
Use the engine when expressions, sources, cursor advancement, and transition
events should be managed as one runtime unit.

## Tooling

`cxpr_document_tooling` reads a model document from standard input and writes
JSON containing parse status, outline symbols, folding ranges, and semantic
tokens:

```bash
./build/cxpr_document_tooling \
  --source-name controller.cxpr < controller.cxpr
```

This tool is intended for editor integration. The underlying document AST API
is public for hosts that need custom navigation, diagnostics, formatting, or
language-server behavior.

The graph plugin produces renderer-neutral JSON. The repository includes a
Cytoscape example that demonstrates one possible visualization without making
Cytoscape part of the library contract.

## Cross-domain examples

The following examples use the same language and runtime boundary.

### Scientific calculation

```cxpr
model orbital_escape

in { gravitational_constant, mass, radius }

escape_velocity =
    sqrt(2 * gravitational_constant * mass / radius)

out escape_velocity
```

### Service-level objective

```cxpr
model slo_guard

in { latency_ms, error_rate, sample_valid }

$latency_budget_ms = 200
$max_error_rate = 0.01

healthy =
    sample_valid and
    within(latency_ms, 0, $latency_budget_ms) and
    error_rate <= $max_error_rate

out healthy
```

### Stateful game entity

```cxpr
model health_component

in { damage, healing, reset }
$maximum = 100

health := (
    reset
        ? $maximum
        : clamp(health - damage + healing, 0, $maximum)
) initial $maximum

alive = health > 0
out { health, alive }
```

### Robotics safety gate

```cxpr
model attitude_guard

in { roll, pitch, imu_ok }
$max_roll = 0.45
$max_pitch = 0.45

stable =
    imu_ok and
    abs(roll) <= $max_roll and
    abs(pitch) <= $max_pitch

out stable
```

### Trading signal

```cxpr
model threshold_cross

in { close, trend }
$entry_margin = 0.01

entry = close > trend * (1 + $entry_margin)
exit = close < trend

out { entry, exit }
```

These examples deliberately keep data acquisition and actions outside the
model. A scientific host supplies constants, an operations host supplies
telemetry, a game supplies entity-local inputs, a robot supplies sensor
values, and a trading host supplies market series. Each host decides how to
consume the outputs.

Larger tested examples are available in:

- [`examples/`](examples) for integration-oriented C and Markdown examples;
- [`tests/fixtures/`](tests/fixtures) for `.cxpr` models covering robotics,
  games, scientific calculations, imports, records, state, and trading.

## Providers and host-backed sources

Provider metadata describes host functions and source inventories without
embedding the host's storage policy. It can declare:

- function signatures and named arguments;
- result fields and primary fields;
- scope arguments such as timeframe or device;
- direct and series-backed sources.

Source plans turn parsed calls into canonical source requirements. The host
binds those requirements to arrays, views, replay data, resamplers, sensors, or
other storage. Runtime-call helpers then expose the resolved values to
evaluation.

This is the correct boundary for expressions such as:

```cxpr
temperature(device="motor-2")
close(timeframe="1d")
ema(source=close, period=20, timeframe="1h")
```

`cxpr` parses and plans the call. The host determines what the scope means and
where its values come from.

## Errors, ownership, and concurrency

Functions that can fail accept a `cxpr_error*`. Errors include a code, message,
and source position when available. Passing `NULL` is allowed where documented,
but retaining the error object is recommended for tooling and build logs.

Ownership follows explicit constructors and destructors:

- parser, AST, context, registry, program, model, document, and session objects
  are freed with their corresponding `*_free` function;
- strings returned by code generators are owned and use the documented free
  function or `free`;
- AST/document/model accessor pointers are borrowed from their owner;
- engine events and plugin callback arguments are borrowed for the documented
  call or session lifetime.

Compiled programs and immutable registries may be shared between threads after
construction. Contexts, model sessions, and engine sessions contain mutable
execution state and should be owned by one worker at a time. Call
`cxpr_thread_cleanup` before a worker thread exits when it has used thread-local
cxpr caches.

## Performance guidance

- Parse and compile once; evaluate many times.
- Reuse contexts or sessions instead of rebuilding them per tick.
- Use context slots and prehashed updates when the host has a stable schema.
- Prefer `cxpr_model_session_tick_fast` for supported in-process scalar models.
- Generate C for production hot loops that need predictable low overhead.
- Specialize stable parameters and selected outputs only when the resulting
  artifact remains representative of the intended workload.
- Benchmark Release builds; debug, sanitizer, and tracing builds are not
  performance references.

Build the benchmarks:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCXPR_BUILD_BENCHMARKS=ON
cmake --build build --target cxpr_bench_ir cxpr_bench_model
./build/benchmarks/cxpr_bench_ir
./build/benchmarks/cxpr_bench_model
```

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
