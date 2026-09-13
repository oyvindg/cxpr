# Diagnostics and tooling

CXPR diagnostics are structured around `cxpr_error`, source spans, immutable
debug metadata, and explicit tooling APIs. Hosts should preserve these values
instead of replacing them with backend-specific strings too early.

## Errors and source locations

Most parse, validation, compilation, and evaluation calls accept an optional
`cxpr_error*`. Initialize it to zero, check the function result first, then read
its code, message, and source position. Error message storage is library-owned;
copy it if it must outlive the relevant operation or object.

`<cxpr/source_location.h>` defines zero-based byte offsets and columns, one-based
lines, and half-open spans. This convention also applies to document/model AST
accessors and generated debug-map spans.

Diagnostics belong to the layer that detects the problem:

- document parsing reports syntax and extension-domain violations;
- model validation reports symbols, references, host-block schemas, imports,
  and dependency cycles;
- backend selection/code generation reports unsupported model or IR shapes;
- execution reports runtime type, arity, index, and resolution failures.

Do not treat an unsupported generated backend as successful code generation.
Either report it or deliberately use the reference runtime as a host-selected
fallback.

## Generated debug maps

Include `<cxpr/debug_map.h>`. A `cxpr_debug_map` is versioned, read-only metadata
describing generated nodes, dependencies, outputs, stable IDs, source spans,
canonical expressions, result types, and optional trace slots.

Call `cxpr_debug_map_validate` before consuming metadata from a generated unit
or plugin. Check `abi_version == CXPR_DEBUG_MAP_ABI_VERSION`; do not infer a newer
layout. All arrays and strings referenced by a map are borrowed from the
generated artifact and remain valid only while that artifact is loaded.
`CXPR_DEBUG_TRACE_SLOT_NONE` means that a node has no runtime trace slot.

Debug maps explain generated code; they do not execute it and do not guarantee
that tracing was enabled. Compilation options such as `enable_trace` and the
compiled-model trace accessors describe whether trace-friendly evaluation was
requested.

## Evaluation snapshots

Include `<cxpr/snapshot.h>`. `cxpr_eval_snapshot_build` creates an owned AST
evaluation snapshot; `cxpr_eval_snapshot_build_flow` creates an owned graph of
named expressions, dependency edges, and per-expression AST snapshots. Zero the
destination before building and release it with `cxpr_eval_snapshot_free` or
`cxpr_eval_snapshot_flow_free`, including after partially completed work.

`cxpr_snapshot_state_name` returns a borrowed static name. The JSON writers emit
to a caller-owned `FILE*` and never close it. The `_ex` variants borrow
`cxpr_snapshot_json_hooks` for the call. Hook callbacks must write exactly one
complete JSON object value for their `host` field and must not retain snapshot
pointers.

## AST and expression code generation

`<cxpr/codegen.h>` exposes target-neutral C-like emission. Returned source
strings are heap allocated and must be released with `free`.

A `cxpr_c_target` is borrowed for the emission call. Set
`api_version = CXPR_C_TARGET_API_VERSION` before using offset-aware hooks. Hooks
own target policy: function mapping, leaf layout, source access, and special call
lowering. A hook that handles a node must return owned source or set a useful
error. Returning `handled = false` delegates to CXPR's default lowering.

Code generation is intentionally capability-checked. Dynamic calls, aggregate
results, unsupported AST nodes, and target-specific lookback cannot be guessed.
Use checked-result APIs when index failure must remain distinguishable from a
numeric value.

## Document tooling

`cxpr_document_tooling` is the repository CLI built from the public document and
model APIs. `cxpr_model_codegen` exercises the generated-model boundary. Their
exact command-line contracts are reported by each executable's `--help`; library
consumers should depend on the C APIs rather than parsing human-readable CLI
output.

The library-facing syntax tooling API is `<cxpr/doc/ast.h>`: parse an owned AST,
inspect copied source/name/extensions and borrowed nodes, traverse it, or lower
it into an independent document. Expression AST diagnostics and source spans
remain valid only for the owning document AST's lifetime.

## Header/function coverage

This page covers the diagnostics/tooling entry points in:

- `<cxpr/source_location.h>`: shared positions and half-open spans;
- `<cxpr/debug_map.h>`: generated metadata ABI and validation;
- `<cxpr/snapshot.h>`: snapshot build/free/state-name/JSON APIs;
- `<cxpr/doc/ast.h>`: document AST parse/free/lower/view/access/visit APIs;
- `<cxpr/codegen.h>`: AST, compiled-IR, checked-result, lookback-offset,
  expression-set, and complete-function emitters.

Recommended validation layers for host tooling are:

1. Parse with the correct manifest/model entry point.
2. Validate host blocks with the host registry.
3. Validate model symbols and external roots.
4. Resolve imports and provider/source requirements.
5. Compile the semantic model.
6. Validate generated debug metadata and run reference-versus-generated parity
   tests for every enabled backend.

The host owns files, import search paths, compiler processes, shared-library or
GPU loading, caches, scheduling, and presentation of diagnostics. CXPR remains
agnostic and exposes enough structured information for those policies without
embedding them in `.cxpr` syntax.
