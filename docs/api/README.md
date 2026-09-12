# cxpr public API

This directory documents the installed C headers and the `.cxpr` language as
implemented in the current source tree. Public declarations in
`include/cxpr/`, their implementations, and executable tests are the source of
truth. Historical plans and release notes are not API specifications.

## Choose a surface

| Surface | Use it for | Primary headers |
| --- | --- | --- |
| [Core](core.md) | Values, errors, contexts, registries, scopes, history, windows, baskets, aliases, and versioning | `types.h`, `context.h`, `registry.h`, `scope.h`, `history.h`, `window.h`, `basket.h`, `alias.h`, `thread.h`, `version.h` |
| [Expression syntax and AST](expressions.md) | Lexing, parsing, AST construction and inspection, analysis, and type checking | `token.h`, `parser.h`, `expr/ast.h`, `analysis.h`, `typecheck.h` |
| [Expression execution](execution.md) | Tree evaluation, compiled expressions, typed IR, evaluators, snapshots, and compatibility facades | `eval.h`, `expr/compiled.h`, `ir.h`, `execution.h`, `evaluator.h`, `expression.h`, `snapshot.h` |
| [`.cxpr` language](language.md) | Authoring portable expression and model source files | lexer, expression parser, document parser, model validation |
| [Embedding guide](embedding.md) | Integrating `.cxpr` into a host from parsing through C/CUDA deployment | `cxpr.h`, `doc.h`, `model/model.h`, `generated.h`, `bulk.h` |
| [Documents and models](documents-models.md) | Parsing documents, model introspection, imports, compilation, and model sessions | `doc.h`, `doc/ast.h`, `model/model.h`, `model/imports.h`, `model/compiled.h`, `model/runtime.h` |
| [Engine](engine.md) | Stateful ticks, source hydration, roles, watches, transitions, and engine snapshots | `engine.h`, `source.h`, `snapshot.h` |
| [Generated C](generated-c.md) | Stable generated-model ABI and native deployment | `generated.h`, `codegen.h` |
| [Bulk and grid](bulk.md) | Structure-of-arrays validation, reset, serial/range execution, and host-owned grids | `bulk.h`, `generated.h` |
| [Providers and resampling](providers-sources-resample.md) | Provider inventories, source plans, fixed-duration parsing, binding, and materialized views | `provider.h`, `source.h`, `resample.h` |
| [Plugins and code generation](plugins-codegen.md) | C, CUDA, graph, metadata, and debug-map artifacts | `model/plugin.h`, `plugins/c.h`, `plugins/cuda.h`, `plugins/graph.h`, `plugins/meta.h`, `plugins/debug_map.h` |
| [Diagnostics and tooling](diagnostics-tooling.md) | Source locations, errors, debug maps, codegen callbacks, and command-line tooling | `source_location.h`, `debug_map.h`, `codegen.h` |
| [Ownership, threading, and compatibility](ownership-threading-versioning.md) | Cross-cutting lifetime, concurrency, and ABI/API rules | all public headers |

`<cxpr/cxpr.h>` includes the normal umbrella surface. The installed headers
`<cxpr/debug_map.h>`, `<cxpr/source_location.h>`,
`<cxpr/plugins/debug_map.h>`, and `<cxpr/model/runtime.h>` are focused or
generated-support interfaces and should be included explicitly when needed.

## Stability classes

- **Public:** installed declarations without a deprecation annotation.
- **Generated ABI:** versioned layouts and callbacks consumed by generated
  artifacts. Validate their version before use.
- **Low-level:** public inspection or construction APIs that expose AST/IR
  details. They remain callable but are not the shortest integration path.
- **Compatibility:** legacy facades and declarations explicitly marked
  deprecated in headers. New code should use the replacement named there.
- **Host boundary:** data acquisition, scheduling, persistence, C/CUDA
  compilation, allocation, device transfer, and kernel launch are deliberately
  outside cxpr.

## Version anchors

| Contract | Current value | Header |
| --- | ---: | --- |
| Library version | 3.0.0 | `<cxpr/version.h>` |
| Generated model descriptor ABI | 4 | `<cxpr/generated.h>` |
| Generated resample view ABI | 1 | `<cxpr/generated.h>` |
| Public IR view API | 1 | `<cxpr/ir.h>` |
| C target API | 1 | `<cxpr/codegen.h>` |
| Debug-map ABI | 1 | `<cxpr/debug_map.h>` |
