# Embedding cxpr in a host

cxpr supplies parsing, validation, model execution, and artifact generation.
The embedding host supplies files or source text, domain data, registered
functions, source materialization, scheduling, allocation, persistence, and
CPU/GPU compilation and dispatch.

## Choose the execution path

| Need | Recommended path |
| --- | --- |
| One expression or editor query | Parse an expression and use the direct or compiled expression API. |
| Dynamic models, diagnostics, watches, or parity tests | Compile a model and create one `cxpr_model_session` or `cxpr_engine_session` per independent execution. |
| Fixed production model and hot loop | Generate C at build time, compile it with the host, validate its descriptor, and call its tick function. |
| Many independent cells/entities | Use the generated descriptor with `<cxpr/bulk.h>`, or wrap the generated evaluator in a host-owned worker/kernel. |
| CUDA | Emit CUDA-compatible math, then let the host compile, allocate, transfer, launch, and synchronize. |

The reference session and generated artifact should implement the same model
semantics. Keeping a parity test between them is the safest deployment pattern.

## Shared library and .NET P/Invoke

Configure with `-DCXPR_BUILD_SHARED=ON` to produce `libcxpr.so`, `cxpr.dll`, or
`libcxpr.dylib`. Install normally with `cmake --install`; the native library is
placed in the configured library directory on Unix and runtime directory on
Windows, while the exported `cxpr::cxpr` CMake target remains unchanged.

For .NET deployment, copy the native artifact into the application's matching
RID runtime folder, for example `runtimes/linux-x64/native/libcxpr.so`,
`runtimes/win-x64/native/cxpr.dll`, or
`runtimes/osx-arm64/native/libcxpr.dylib`. Declare `[DllImport("cxpr")]`; the
runtime supplies the platform-specific prefix and suffix.

The recommended flat expression path is
`cxpr_expr_parser_new`/`cxpr_expr_ast_parse`, `cxpr_expr_compile`,
`cxpr_context_new`/`cxpr_context_set`,
`cxpr_expr_compiled_eval_number` or `_bool`, followed by the matching free
functions. Opaque handles marshal as `IntPtr`, C `double` as `double`, `size_t`
as `nuint`, and C `bool` returns as one-byte `[MarshalAs(UnmanagedType.I1)]`.
UTF-8 input strings are borrowed for the duration of each call.

`cxpr_error` is blittable apart from its borrowed `message` pointer; copy or
format that message before another cxpr call. `cxpr_value` contains a tagged C
union and is best avoided at the managed boundary in favor of the typed number
and bool evaluators. The bulk column/view structs contain only pointers and
`size_t` fields and are blittable, but their backing arrays must remain pinned
for the complete call. Descriptor structs include function pointers and are not
recommended for direct managed marshalling; wrap descriptor/bulk execution in
a small flat native shim when needed. The 3.1 shared target intentionally uses
default symbol visibility; an explicit export-macro policy is future work.

## Minimal dynamic model host

For a model already available as a NUL-terminated string:

```c
#include <cxpr/cxpr.h>

cxpr_error err = {0};
cxpr_registry *registry = cxpr_registry_new();
cxpr_model *model = NULL;
cxpr_model_compiled *program = NULL;
cxpr_model_session *session = NULL;

if (!registry) goto fail;
cxpr_register_defaults(registry);

model = cxpr_model_parse(model_source, &err);
if (!model) goto fail;
if (!cxpr_model_validate(model, &err)) goto fail;

program = cxpr_model_compile(model, registry, &err);
if (!program) goto fail;
session = cxpr_model_session_new(program, registry, &err);
if (!session) goto fail;

cxpr_context *ctx = cxpr_model_session_context(session); /* borrowed */
cxpr_context_set(ctx, "temperature", 72.5);
cxpr_context_set_param(ctx, "limit", 80.0);
if (!cxpr_model_session_tick(program, session, registry, &err)) goto fail;

bool alarm = false;
if (!cxpr_model_session_get_bool(session, "alarm", &alarm)) goto fail;

/* registry must outlive program/session use */
cxpr_model_session_free(session);
cxpr_model_compiled_free(program);
cxpr_model_free(model);
cxpr_registry_free(registry);
```

Check the return value of every setter/evaluation call in production code. On
failure, copy `cxpr_error` to durable host storage with `cxpr_error_format`;
the message pointer itself is borrowed thread-local storage.

`cxpr_doc_load_model` or `cxpr_doc_parse_model` is preferable when the host
also needs document AST or host blocks. `cxpr_model_parse` is the shorter owning
entry point when only the semantic model is needed.

## Register host functions

Create a registry and register only the vocabulary the model may call. Builtin
sets are installed with `cxpr_register_math`, `cxpr_register_timeseries`, or
`cxpr_register_defaults`. Host callbacks use the appropriate
`cxpr_registry_add_*` family for numeric, typed, AST-aware, time-series, or
record-producing functions.

Registry callback userdata is normally borrowed. When a registration accepts
a `cxpr_userdata_free_fn`, supplying it transfers userdata cleanup to the
registry. Treat registry mutation as a host synchronization point, and keep the
registry alive for every parser/compiler/session that borrows it.

For a declared provider inventory and materialized sources, use
`cxpr_register_provider_signatures` and `cxpr_model_plan_bind_sources` or
`cxpr_plan_bind_sources`. The returned requirement/binding plan tells the host
what to load. It does not load data itself. Missing source or resample bindings
must fail closed; never substitute the primary series for a requested scope.

See [Providers, sources, and resampling](providers-sources-resample.md) and the
code-backed example `examples/scoped_sources.c`.

## Imports and host blocks

The core parser records `use` declarations but does not choose a filesystem or
package policy. Implement `cxpr_model_import_load_fn` to map
`(importer_id, use_path)` to heap-allocated canonical ID and source strings.
`cxpr_model_import_bundle_build` takes ownership of those strings, resolves the
transitive graph, compiles child models, and rejects cycles and duplicate
namespaces. Obtain the root's borrowed import array with
`cxpr_model_import_bundle_root_imports`, then compile with
`cxpr_model_compile_with_imports` or `cxpr_model_compile_full`. Keep the bundle
alive until the compiled root is no longer using its child programs.

In source, imported calls remain namespaced:

```cxpr
use math_helpers
use geometry2d as g

doubled = math_helpers.twice(value)
position = g.vec2(x, y)
```

The first uses the import leaf as namespace; the second assigns `g` explicitly.

Application configuration belongs in host blocks. Register accepted block
kinds with `cxpr_host_block_registry_register`, then call
`cxpr_doc_validate_host_blocks` or `cxpr_model_validate_host_blocks`. This lets
the host define policy without adding domain concepts to the language runtime.

## Generated C deployment

Generate the artifact during the host build, not inside the per-tick loop:

```sh
cxpr_model_codegen \
  --model model.cxpr \
  --output model.gen.c \
  --function application_model_tick
```

Compile `model.gen.c` into the host, a static library, or a loadable module.
Before use, validate `application_model_tick_descriptor` with
`cxpr_generated_model_descriptor_abi_valid`. Allocate one zeroed state block
per independent model instance, seed the parameter array from descriptor
defaults or host configuration, and fill inputs in descriptor order:

```c
const cxpr_generated_model_descriptor *d =
    &application_model_tick_descriptor;
if (!cxpr_generated_model_descriptor_abi_valid(d)) return 0;

void *state = calloc(1u, d->state_size());
double inputs[CXPR_GENERATED_MODEL_MAX_INPUTS] = {0};
double params[CXPR_GENERATED_MODEL_MAX_PARAMS] = {0};
double outputs[CXPR_GENERATED_MODEL_MAX_OUTPUTS] = {0};

for (size_t i = 0; i < d->param_count; ++i) {
    if (d->param_has_default[i]) params[i] = d->param_defaults[i];
}
d->tick(state, inputs, params, outputs);
```

Names, arrays, and callbacks in the descriptor are artifact-owned. Keep the
artifact loaded while they are referenced. Generated state is mutable and must
not be shared between independent simulations. See [Generated C](generated-c.md)
and `examples/compiled_strategy/host.c` for complete reference/generated parity.

Models with `resample` use the separate additive generated-function ABI and
ordered `cxpr_resample_view` arguments; the generic descriptor tick and bulk
runner do not add those arguments automatically. Build a host wrapper after
validating each view and its alignment.

## Bulk, grids, and CUDA

`cxpr_bulk_view` is a structure-of-arrays adapter over a generated scalar
descriptor. The host owns extents, neighbor gathering, halo/boundary rules,
timesteps, worker pools, state placement, and buffer swaps. Validate with
`cxpr_bulk_validate`, initialize with `cxpr_bulk_reset`, then call
`cxpr_bulk_run` or disjoint `cxpr_bulk_run_range` operations.

For CUDA, keep the same scalar input/output contract but compile the emitted
device evaluator with `nvcc` and call it from a host-defined kernel. cxpr does
not expose streams, blocks, grids, device allocation, or transfer APIs. See
[Bulk and grid](bulk.md) and `examples/bulk_grid/`.

## Build integration and lifecycle checklist

1. Link `cxpr::cxpr` through `add_subdirectory` or `find_package`.
2. Parse model source and validate any host blocks.
3. Register the exact function/provider vocabulary and resolve imports.
4. Bind every required source/resample view explicitly.
5. Choose a reference session or generate a native artifact at build time.
6. Give each independent execution its own session/state and mutable context.
7. Validate versioned descriptors/views before use.
8. Format errors immediately and release every owned object with its matching
   cxpr cleanup function.
9. Add deterministic parity tests before moving a model to C/CUDA.
