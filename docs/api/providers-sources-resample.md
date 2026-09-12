# Providers, source plans, and resampling

Providers describe host-backed functions and scalar series without importing a
domain runtime into cxpr. `<cxpr/provider.h>` supplies metadata for names,
arguments, record fields, source-input functions, scopes, value types, and
materialization policy. Provider specs, their strings, and inventory arrays are
provider-owned and must outlive registration and inspection.

Register inventories with `cxpr_register_provider_signatures()`. Runtime values
remain a host responsibility through `cxpr_host_config` callbacks or explicitly
registered handlers. A provider scope is generic metadata: `timeframe`,
`resolution`, `region`, or another partition key. It is not intrinsically a
trading concept.

For materialized series, prefer `cxpr_plan_bind_sources()` from
`<cxpr/source.h>`. cxpr traverses the AST, parses source-plan nodes, evaluates
numeric binding arguments, and identifies requirements. The host callback maps
each leaf to a stable opaque handle. The returned
`cxpr_source_plan_bindings` owns its normalized strings and arrays and must be
released with `cxpr_free_source_plan_bindings()`. Low-level
`cxpr_source_plan_ast` metadata is released with
`cxpr_free_source_plan_ast()`; its `expression_ast` and bound AST pointers remain
borrowed from the original expression AST.

`resample(source, every=...)` is explicit syntax for a pre-materialized temporal
view. `<cxpr/resample.h>` parses and normalizes fixed durations. cxpr does not
fetch, aggregate, interpolate, cache, or invent missing series. The provider
declares whether a source supports resampling and its materialization policy;
the host binds actual data and alignment. Missing requirements should fail at
the binding boundary, not silently fall back to the primary series.

Before creating generated C/CUDA views, call
`cxpr_validate_generated_resample_bindings()`. Version 1 of that transport is
scalar numeric only; unsupported records or vectors must be rejected rather
than reinterpreted as doubles. Reference evaluation can use
`cxpr_resolve_bound_series_value()` with a host resolver and explicit evaluation
time, cursor, and lookback.

See `examples/scoped_sources.c`, `tests/provider.test.c`,
`tests/source_plan_bind.test.c`, and `tests/generated_resample_parity.test.c`.

## API inventory

| Header | Public symbols |
| --- | --- |
| `<cxpr/provider.h>` | `cxpr_provider_is_valid`, `cxpr_provider_fn_specs`, `cxpr_provider_fn_spec_find`, `cxpr_provider_source_specs`, `cxpr_provider_source_spec_find`, `cxpr_provider_expr_param_spec_for` |
| `<cxpr/provider.h>` registration | `cxpr_register_provider_fn_spec`, `cxpr_provider_host_visible_arg_range`, `cxpr_register_provider_signatures` |
| `<cxpr/source.h>` planning | `cxpr_parse_provider_source_plan_ast`, `cxpr_eval_source_plan_bound_args`, `cxpr_plan_bind_sources`, `cxpr_plan_bind_sources_from_table` |
| `<cxpr/source.h>` lifetime | `cxpr_free_source_plan_ast`, `cxpr_free_source_plan_bindings` |
| `<cxpr/source.h>` generated/reference binding | `cxpr_validate_generated_resample_bindings`, `cxpr_resolve_bound_series_value`, `cxpr_eval_bound_resample`, `cxpr_eval_bound_resample_lookback` |
| `<cxpr/resample.h>` syntax | `cxpr_parse_fixed_duration`, `cxpr_resample_call_parse`, `cxpr_resample_validate_ast` |
