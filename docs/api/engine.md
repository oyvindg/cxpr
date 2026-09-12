# Stateful engine API

Include `<cxpr/engine.h>`. The engine is an optional domain-neutral layer over
the evaluator. It defines expressions, sources, ticks, lookback, and transition
events; the host defines what a tick means and performs every side effect.

## Program and session lifecycle

`cxpr_engine_program` is immutable after construction. Build it with
`cxpr_engine_program_new`, share it read-only if desired, and release it with
`cxpr_engine_program_free` after all sessions are gone.

`cxpr_engine_session` owns one mutable run: context values, cursor, source and
expression history, edge state, and event storage. Create one session per
concurrent worker with `cxpr_engine_session_new`, or use
`cxpr_engine_session_create` to create a session that owns its program. Release
sessions with `cxpr_engine_session_free`.

`cxpr_engine_session_context` returns the session-owned, borrowed context. It is
valid until session destruction and must not be freed separately. Parameters
may be set as numbers with `cxpr_engine_set_param` or as typed values with
`cxpr_engine_set_param_value`; roles are replaced with `cxpr_engine_set_role`.

Configuration arrays and their strings are borrowed only during program
construction; the program copies the declarations it needs. An injected
registry is borrowed and must outlive the program. With a `NULL` registry, the
engine creates and owns a default registry. Host callback `userdata` and source
data remain host-owned and must outlive every session using them.

## Sources

Choose one source form per declared name:

- Pull sources resolve the current value through `cxpr_engine_pull_fn`. The
  engine memoizes a referenced source within a tick and maintains its required
  lookback ring.
- View sources resolve an absolute index through `cxpr_engine_view_fn`. An
  optional mapper converts the primary cursor to the source's index space.
- Column sources borrow a strided array of `double` fields and provide the
  lowest-overhead random-access path.

Declarations belong to the program. Bindings are session-local defaults that
can be replaced with `cxpr_engine_bind_userdata` or
`cxpr_engine_bind_column`. Out-of-range or unavailable data is represented by
the documented unresolved/`NAN` behavior, not by host side effects.

## Ticks, lookback, and events

`cxpr_engine_tick` advances the cursor, hydrates only referenced sources,
evaluates the expression set once, updates history, and returns fired watches.
`cxpr_engine_tick_at` explicitly selects the next absolute index. The fallback
variants temporarily expose a parent context for host-provided values.

The event array returned through `out_events` is borrowed session storage. Read
or copy it before the next tick, reset, or session destruction. Each event's
`expr_name` is borrowed from the program; its `cxpr_value` is not transferred to
the caller.

Use `cxpr_engine_session_reset` to restart cursor/history/edge state while
retaining parameters, roles, and source bindings. Named results and instruction
statistics are borrowed/read-only observations of the session evaluator.

`cxpr_engine_tick_index` reports `-1` before the first tick. Result readers are
`cxpr_engine_get`, `cxpr_engine_get_double`, and `cxpr_engine_get_bool`; always
inspect their `found` output instead of interpreting a default return value as a
successful lookup. Instruction-count accessors expose one expression, its
dependency closure, or the complete expression batch.

`cxpr_engine_snapshot_flow` and `_fallback` allocate an owned
`cxpr_eval_snapshot_flow` for diagnostics. Initialize the destination to zero
and release it with `cxpr_eval_snapshot_flow_free`; it is not session-owned.

The engine owns lookback for declared sources and tracked expressions. When an
injected registry already has a lookback resolver, unrelated targets are
delegated to it and the prior resolver is restored when the final program using
that registry is released. An `cxpr_engine_inline_lookback_fn` may opt host AST
targets into offset-aware evaluation. Such callbacks can read the active offset
with `cxpr_engine_context_lookback_offset`; they must not retain the context or
AST pointers.

## Concurrency

An immutable program may be shared, but a session is single-threaded. Use one
session per worker. Registry mutation and host callback state require whatever
synchronization the host's registry/provider contract specifies. Events are
returned only after evaluation, so actions should be dispatched by the host
after the tick rather than from source callbacks.

## Engine versus generated models

The engine is appropriate for embedding, dynamic rule sets, diagnostics, and
reference execution. It is not the generated-model ABI and is not implicitly
used by generated C. For dense CPU/CUDA bulk execution, compile a semantic model
and use `cxpr_model_compiled_generate_c*`; the host owns memory layout,
parallelism, compilation, and launch. Keep engine/session parity tests as an
oracle when introducing a generated backend.
