# CXPR core C API

This page describes the public foundation exposed by the installed headers in
`include/cxpr`. For expression parsing and evaluation, see
[Expressions](expressions.md). For reusable programs, sessions, generated
artifacts, and bulk execution, see [Execution](execution.md).

## Including CXPR

`<cxpr/cxpr.h>` is the umbrella header. Applications may instead include the
small header that owns an API, such as `<cxpr/types.h>`, `<cxpr/context.h>`, or
`<cxpr/registry.h>`. CXPR exposes a C11 API and guards its declarations for C++
callers.

## Values and errors

`<cxpr/types.h>` defines `cxpr_value`, `cxpr_value_type`, and `cxpr_error`.
Values may be numbers, booleans, strings, structs, arrays, nulls, timestamps, or
durations. The inline constructors `cxpr_num`, `cxpr_bool`, `cxpr_string`,
`cxpr_null`, `cxpr_timestamp`, `cxpr_duration`, `cxpr_struct`, and `cxpr_array`
do not allocate. In particular, `cxpr_string` borrows its string pointer.

Use `cxpr_value_clone` when an independent value is required and pair it with
`cxpr_value_free`. `cxpr_struct_value_new` and `cxpr_array_value_new` deep-copy
their inputs; release them with `cxpr_struct_value_free` and
`cxpr_array_value_free`.

An error has a code plus optional byte position, line, and column. A
`cxpr_error.message` is owned by CXPR and may refer to thread-local scratch
storage. Never free it, and do not retain it across later CXPR calls on the same
thread. `cxpr_error_format` copies a complete diagnostic into caller-owned
storage. `cxpr_error_string` returns a static description of a code.

## Contexts

`<cxpr/context.h>` defines the mutable name/value environment used during
evaluation:

- `cxpr_context_new`, `cxpr_context_clone`, and `cxpr_context_overlay_new`
  create owned contexts; `cxpr_context_free` releases them. An overlay borrows
  its parent, so the parent must outlive it.
- `cxpr_context_set`, `_set_bool`, `_set_string`, `_set_value`, and their
  `$param` counterparts set typed values. Bulk setters include
  `cxpr_context_set_array` and `cxpr_context_set_param_array`.
- `cxpr_context_get`, `_get_bool`, `_get_string`, `_get_typed`, and the
  corresponding parameter getters return values and optionally set `found`.
- `cxpr_context_set_struct`, `cxpr_context_get_struct`,
  `cxpr_context_get_field`, and `cxpr_context_set_fields` expose structured
  values. Consult the header for the exact copy/borrow contract of each setter.
- `cxpr_context_clear` clears ordinary and parameter bindings.

For hot numeric paths, bind a `cxpr_context_slot` with
`cxpr_context_slot_bind` or `cxpr_context_slots_bind`, validate it with
`cxpr_context_slot_valid`, and then use the slot get/set functions. Slots are
low-level cached handles and must be rebound after a context mutation that
invalidates them.

`cxpr_context_set_history_offset` and `cxpr_context_history_offset` are
low-level host integration APIs used while resolving lookbacks. Most users
should let the engine manage them.

## Registries

`<cxpr/registry.h>` defines the function vocabulary used by parsers,
type-checking, evaluators, and engines. `cxpr_registry_new` returns an owned
registry and `cxpr_registry_free` releases it. `cxpr_register_math`,
`cxpr_register_timeseries`, or `cxpr_register_defaults` install built-ins.

Registration families serve different host contracts:

- `cxpr_registry_add_numeric` / `cxpr_registry_add` register numeric callbacks.
- `cxpr_registry_add_value` and `cxpr_registry_add_typed` register typed value
  callbacks.
- `cxpr_registry_add_ast` and `cxpr_registry_add_ast_handler` expose the call
  AST to a host. These are advanced integration APIs.
- `cxpr_registry_add_timeseries` and `cxpr_registry_add_struct` support
  stateful/time-series and structured producers.
- `cxpr_registry_define_fn` adds an expression-defined function.

Callback userdata is borrowed unless a `cxpr_userdata_free_fn` is supplied by
the registration call; when supplied, the registry owns cleanup through that
callback. A registry passed to
`cxpr_evaluator_new` or an engine config is borrowed and must outlive that
consumer.

Lookback resolvers, index capabilities, direct registry calls, struct codegen,
and `cxpr_registry_defined_fn_to_c_function` are public but low-level. They are
intended for host and compiler integrations, not ordinary expression clients.

## Providers, sources, and scopes

`<cxpr/provider.h>`, `<cxpr/source.h>`, `<cxpr/runtime.h>`, and
`<cxpr/scope.h>` form the host-neutral provider/source-plan layer. They parse
runtime calls, derive source requirements, bind host series, and resolve scoped
values. Plan objects and binding objects created by these APIs own storage and
must be released with `cxpr_free_source_plan_ast` and
`cxpr_free_source_plan_bindings`.

These headers are advanced host APIs. CXPR does not prescribe market data,
physics grids, devices, storage, scheduling, or boundary conditions; hosts map
such concepts to named scalar sources and generated-model inputs.

## Alias expansion

`<cxpr/alias.h>` defines `cxpr_alias` and `cxpr_expand_aliases`. Expansion is
performed on parsed ASTs rather than by textual substitution, detects cycles,
and returns an allocated expression string through `out_expression`. The caller
owns that string and releases it with `free()`.

## Historical numeric sources

`<cxpr/history.h>` provides host-neutral history adapters:

| API | Contract |
| --- | --- |
| `cxpr_register_history_numeric_sources` | Copies source descriptors while borrowing their immutable buffers and cursor. |
| `cxpr_register_history_contiguous_numbers` | Convenience registration for one contiguous `double` column. |
| `cxpr_register_history_numeric_provider` | Registers stable names whose borrowed view may be rebound dynamically by the host. |

Bounds behavior is explicit through `cxpr_history_bounds_policy`: error,
clamp-to-first, or legacy-NaN. A numeric source stride of zero means
`sizeof(double)`. Registered backing storage and borrowed cursors must outlive
their registry use. A provider userdata destructor, when supplied, transfers
userdata cleanup to the registry.

## Basket and window metadata

`<cxpr/basket.h>` exposes recognition and registration of basket aggregates:
`cxpr_basket_is_builtin`, `cxpr_basket_is_aggregate_function`,
`cxpr_register_basket_builtins`, `cxpr_expr_ast_uses_basket_aggregates`, and
`cxpr_expression_uses_basket_aggregates`. Detection does not fetch or construct
a domain basket; the host supplies the values and execution scope.

`<cxpr/window.h>` exposes the static `cxpr_window_ir` registry used by analysis
and code generation. Inspect it with `cxpr_window_ir_find`,
`cxpr_window_ir_at`, and `cxpr_window_ir_count`. Returned metadata is static and
borrowed. Window syntax is lowered to history operations; it does not allocate
a runtime array or window object.

## Thread-local cleanup

`<cxpr/thread.h>` contains `cxpr_thread_cleanup`. It releases the calling
thread's internal evaluation cache and is useful just before a short-lived
worker exits. The call is optional, idempotent, and affects memory retention,
not correctness. It must be invoked by the thread whose cache is being
released.

## Threading and ownership summary

- Treat mutable contexts, evaluators, registries, and sessions as confined to
  one thread unless a header explicitly states otherwise.
- An immutable engine program may be shared; each worker should own a separate
  session. See [Execution](execution.md#threading).
- Borrowed registries, parent contexts, descriptors, callbacks, and backing
  buffers must outlive their consumers.
- Free every object returned as newly allocated with its matching CXPR free
  function. Never free borrowed error strings or names returned by inspection
  APIs.

## Deprecated and compatibility APIs

Deprecation is declared in the installed headers, not inferred here. Notable
compatibility names include `CXPR_NODE_LOOKBACK`, an alias of
`CXPR_NODE_INDEX` in `<cxpr/expr/ast.h>`. Prefer current index terminology.
The direct AST-style evaluator and AST APIs remain supported public APIs, but
the engine/session and generated-artifact APIs are the recommended execution
surface for reusable or high-throughput workloads.
