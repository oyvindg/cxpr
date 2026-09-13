# CXPR execution API

CXPR separates host-neutral math from host-owned data, topology, scheduling,
and devices. This page covers engine sessions, generated artifacts, and bulk
execution. See [Core](core.md) and [Expressions](expressions.md) for supporting
APIs.

## Programs and sessions

`<cxpr/engine.h>` separates immutable compilation state from mutable execution:

- `cxpr_engine_program_new` builds an immutable program from
  `cxpr_engine_config`; `cxpr_engine_program_free` releases it.
- `cxpr_engine_session_new` opens an isolated mutable session over a borrowed
  program; `cxpr_engine_session_free` releases it.
- `cxpr_engine_session_create` is a convenience that creates a session owning
  its internal program.

The config declares expressions, params, pull/view/column sources, roles,
watches, and optional inline-lookback policy. An injected registry is borrowed
and must outlive the program.

For each session, bind data with `cxpr_engine_bind_column` and
`cxpr_engine_bind_userdata`; update parameters with `cxpr_engine_set_param` or
`cxpr_engine_set_param_value`; update basket roles with `cxpr_engine_set_role`.
Advance with `cxpr_engine_tick` or indexed `cxpr_engine_tick_at`, then retrieve
named results with `cxpr_engine_get`, `_get_double`, or `_get_bool`. Call
`cxpr_engine_session_reset` before an independent reuse.

The `_fallback` tick and snapshot functions accept an external fallback
context. They are host-integration APIs; declared sources and params provide a
more stable production schema. Check every boolean result and inspect the
supplied `cxpr_error`.

Column binding storage is borrowed and must remain valid while it can be read.
Callback userdata follows the source definition's ownership callback contract.

## Threading

The header explicitly permits an immutable `cxpr_engine_program` to be shared
across threads. Each worker must own a separate `cxpr_engine_session`, including
its params, roles, bindings, history, and results. A session is not a concurrent
execution object. Mutable injected registries or callback userdata require host
synchronization or per-worker instances.

## Generated artifacts

`<cxpr/generated.h>` defines `cxpr_generated_model_descriptor`, the ABI for a
generated scalar C model. Validate a descriptor with
`cxpr_generated_model_descriptor_abi_valid` before invoking its state, reset,
or tick callbacks. Descriptor names and schema arrays are borrowed static
artifact data.

`<cxpr/codegen.h>`, `<cxpr/model/compiled.h>`, and `<cxpr/plugins/c.h>` expose
build/tooling APIs for generated C. `<cxpr/plugins/cuda.h>` is an optional CUDA
backend plugin. Backend selection does not add CUDA, grids, or physics concepts
to the `.cxpr` language.

Model parsing, imports, compilation, and model sessions are declared by
`<cxpr/model/model.h>`, `<cxpr/model/imports.h>`, `<cxpr/model/compiled.h>`, and
`<cxpr/model/runtime.h>`. These are lower-level compiler/runtime APIs. Match
every allocated model, compiled object, and session with the free function
documented in its header; loader and registry objects follow their callback and
borrow rules.

## Bulk execution

`<cxpr/bulk.h>` maps one generated scalar model evaluation to each logical
element. CXPR does not own a grid. A host materializes neighbors, coordinates,
tensor components, boundary values, and properties as named scalar columns.

`cxpr_bulk_view` is structure-of-arrays storage. Inputs are read-only strided
`double` columns (stride zero broadcasts element zero); outputs are writable
strided columns; params are shared; and `states` contains one model state block
per element separated by `state_stride` bytes.

```c
cxpr_bulk_view view = {
    .inputs = inputs,
    .input_count = descriptor->input_count,
    .params = params,
    .param_count = descriptor->param_count,
    .outputs = outputs,
    .output_count = descriptor->output_count,
    .states = states,
    .state_stride = state_stride,
    .element_count = element_count,
};

cxpr_bulk_status status = cxpr_bulk_validate(descriptor, &view);
if (status == CXPR_BULK_OK) status = cxpr_bulk_reset(descriptor, &view);
if (status == CXPR_BULK_OK) status = cxpr_bulk_run(descriptor, &view);
```

`cxpr_bulk_run_range` and `cxpr_bulk_reset_range` operate on a subrange;
`cxpr_bulk_status_message` describes failures. The host must allocate every
strided access, satisfy generated-C state alignment, and prevent input/param
storage from overlapping outputs or state that can execute concurrently.
Disjoint ranges may run in parallel; overlapping ranges are a data race.

## Execution policy

`<cxpr/execution.h>` defines host-selected intent:
`CXPR_EXECUTION_GENERATED_C`, `CXPR_EXECUTION_AST_ANALYSIS`, and
`CXPR_EXECUTION_IR_REFERENCE`. The inline helpers validate a policy, report
whether runtime compilation is allowed, and check for generated-only execution.
These types constrain/report a path; they do not compile or execute by
themselves.

AST analysis and reference IR are useful for tooling and parity tests.
Generated C, or an optional host CUDA backend, is the intended route for heavy
repeated computation where runtime compilation is prohibited.
