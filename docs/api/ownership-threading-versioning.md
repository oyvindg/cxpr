# Ownership, threading, and compatibility

This page summarizes cross-cutting rules. Function-specific exceptions are
documented beside each API and in the declaring header.

## Ownership vocabulary

Constructors, parsers, compilers, snapshot creators, and functions documented
as returning allocated text transfer ownership to the caller. Release them
with the matching `*_free`, `*_destroy`, or documented allocator counterpart.
Do not substitute `free()` when a matching cxpr release function exists.

Accessors normally return borrowed pointers. A borrowed pointer remains valid
only while its owning AST, document, model, compiled program, session,
snapshot, provider inventory, or generated artifact remains alive and
unchanged. Array counts and string pointers returned by inspection APIs belong
to that same lifetime.

Contexts and sessions own mutable runtime state. Registries, provider specs,
host callback tables, source handles, and generated descriptor metadata are
commonly borrowed and must outlive every consumer. Plugin event data is valid
only during the plugin invocation; artifact sinks must copy bytes they retain.

## Immutability and concurrency

Parsed ASTs, validated models, compiled expression/model programs, provider
inventories, and generated descriptors may be shared only where their public
contract is read-only. Mutable contexts, model sessions, engine sessions,
history buffers, snapshots under construction, and generated state blocks must
not be shared concurrently without host synchronization.

`cxpr_bulk_run_range()` may run concurrently for disjoint element ranges when
all writable output/state regions are disjoint and shared inputs/parameters
are read-only. The library does not create a worker pool.

Registry mutation and resolver installation are host synchronization points.
The engine temporarily coordinates its lookback resolver for a borrowed
registry; follow the lifecycle rules in [Engine](engine.md) and do not mutate
that registry concurrently.

## Version and ABI checks

Compile-time library version macros do not validate a loaded generated
artifact. Use the validator associated with each versioned transport:

- `cxpr_generated_model_descriptor_abi_valid()` for descriptor ABI 4;
- `cxpr_resample_view_validate()` for resample view ABI 1;
- `cxpr_debug_map_validate()` for debug-map ABI 1;
- the version constants and validation functions documented by public IR and
  codegen interfaces before consuming versioned views.

Additive fields or APIs do not make an older artifact magically compatible;
the host must validate the exact contract it consumes. Structs passed through
public APIs should be zero-initialized before setting known fields so additive
options retain their defaults.

## Deprecation and compatibility

Deprecated AST lookback names in `<cxpr/expr/ast.h>` are compatibility aliases
for general indexing. New code should use the `index` constructors/accessors
named in their header annotations. Compatibility facades such as
`<cxpr/expression.h>`, `<cxpr/evaluator.h>`, and `<cxpr/runtime.h>` are public,
but focused parser, compiled-expression, model-session, or engine APIs provide
clearer ownership and error contracts for new integrations.

The language and runtime do not infer domain policy. In particular, hosts must
provide source data required by `resample`, reject missing bindings, and own
all CPU/GPU scheduling. See [Providers and resampling](providers-sources-resample.md),
[Generated C](generated-c.md), and [Bulk and grid](bulk.md).

