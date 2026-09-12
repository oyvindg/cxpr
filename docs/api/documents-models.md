# Documents and models

CXPR separates document syntax, semantic models, execution, and generated code. Use
the narrowest layer that matches the host's job.

## Document API

Include `<cxpr/doc.h>`. A `cxpr_doc` is an owned parsed `.cxpr` document.

- `cxpr_doc_parse_manifest` and `cxpr_doc_load_manifest` accept host blocks but
  reject executable model statements.
- `cxpr_doc_parse_model` and `cxpr_doc_load_model` enable model syntax in addition
  to host blocks.
- `cxpr_doc_parse` and `cxpr_doc_load` are low-level variants taking a
  `cxpr_doc_extension` bitmask.

The input text is borrowed only for the call. The returned document owns its AST,
host blocks, strings, and optional semantic model. Release it with
`cxpr_doc_free`. Values returned by `cxpr_doc_host_block_at`,
`cxpr_doc_host_block`, and `cxpr_doc_model` are borrowed and become invalid when
the document is freed.

Host block syntax is owned by CXPR; block meaning is owned by the host. Register
accepted block kinds in a `cxpr_host_block_registry`, validate with
`cxpr_doc_validate_host_blocks`, then free the registry with
`cxpr_host_block_registry_free`. Registered specs are copied by value, while
their strings and `userdata` remain borrowed and must outlive validation.

### Read-only document AST

Include `<cxpr/doc/ast.h>` when tooling needs source order and syntax shape before
semantic lowering. `cxpr_doc_ast_parse` copies the source and optional source
name; free the owned tree with `cxpr_doc_ast_free`. `cxpr_doc_ast_lower` borrows
the AST and returns an independent owned `cxpr_doc`. `cxpr_doc_ast_view` borrows
the syntax tree owned by a document. Root/node, child, string, expression, and
span accessors all return borrowed data. `cxpr_doc_ast_visit` is pre-order and
honors `CONTINUE`, `SKIP_CHILDREN`, and `STOP` exactly as declared by
`cxpr_visit_control`.

## Semantic model API

Include `<cxpr/model/model.h>`. `cxpr_model_parse` is the convenient owning entry
point when the document wrapper and host-block AST are not needed. Release the
result with `cxpr_model_free`.

A normal host pipeline is:

```c
cxpr_error err = {0};
cxpr_model *model = cxpr_model_parse(source, &err);
if (!model || !cxpr_model_validate(model, &err)) {
    /* report err before releasing model */
}
cxpr_model_compiled *program = cxpr_model_compile(model, registry, &err);
/* model and registry must follow the contracts below */
cxpr_model_compiled_free(program);
cxpr_model_free(model);
```

`cxpr_model_validate` performs domain-neutral symbol and reference checks.
`cxpr_model_validate_with_external_refs` additionally permits explicitly named
host-owned roots. It does not discover trading indicators, physics fields, data
feeds, or other domain concepts.

For scoped sources, call `cxpr_model_plan_bind_sources`. CXPR returns structured
requirements; the provider/host decides how to obtain, align, cache, or resample
the requested data. Free source-plan outputs using the ownership functions in
`<cxpr/provider.h>`.

## Imports

Include `<cxpr/model/imports.h>` for a host-controlled import graph. The loader
callback owns path policy and I/O. On success it transfers its heap-allocated
canonical ID and source string to the bundle. A successful callback may return
both as `NULL` to mark a `use` as host-owned rather than a model import.

`cxpr_model_import_bundle_build` owns all resolved child models and compiled
programs until `cxpr_model_import_bundle_free`. The array returned by
`cxpr_model_import_bundle_root_imports` is borrowed from the bundle; keep the
bundle alive through root compilation.

## Compiled models and sessions

A `cxpr_model_compiled` is immutable and may be shared. The registry passed to
compilation is borrowed; keep it alive wherever the compiled program or its
sessions may call registry functions. Release the program with
`cxpr_model_compiled_free` only after its sessions are gone.

The reference execution path is:

1. Create a mutable `cxpr_model_session` with `cxpr_model_session_new`.
2. Obtain its borrowed context with `cxpr_model_session_context`.
3. Write host inputs and parameter overrides to that context.
4. Call `cxpr_model_session_tick` or `cxpr_model_session_tick_fast`.
5. Read named outputs through the session accessors.
6. Release the session with `cxpr_model_session_free`.

One session represents one mutable run. Do not share it concurrently. State
updates are atomic across a tick: expressions observe state from the start of
the tick, updates are staged, and commits become current on the next tick.

## Generated-support boundary

The session API is the reference/tooling runtime and a useful parity oracle. For
production hot loops, use `cxpr_model_compiled_generate_c*` where the model is
supported. Generated source is newly allocated and must be freed with `free`.
Unsupported shapes fail with `cxpr_error`; they are not silently approximated.

Generated tick ABI, input order, parameter order, selected output order, state
initialization, and optional resample-view suffix are defined by the comments and
accessors in `<cxpr/model/model.h>`. Hosts compile and execute the emitted C/CUDA;
CXPR does not own the compiler, device, grid, scheduler, filesystem, or runtime
loader. Always query parameter, output, and resample requirement order instead of
reconstructing it from source text.

## Introspection API map

All strings and AST pointers returned by ordinary model accessors are borrowed
from the parsed model. The public accessor families cover:

- model name and span: `cxpr_model_name*`;
- imports and aliases: `cxpr_model_use*`;
- model-defined functions: `cxpr_model_function*`;
- inputs, constants/call parameters, bindings, and outputs:
  `cxpr_model_input*`, `cxpr_model_constant*`, `cxpr_model_binding*`, and
  `cxpr_model_output*`;
- attached metadata and typed metadata fields: `cxpr_model_metadata*`;
- flat and nested host blocks: `cxpr_model_host_block*` and `cxpr_host_block*`.

Three accessor families allocate results for the caller:
`cxpr_model_function_declaration_source` returns an owned source string,
`cxpr_model_metadata_field_number_list` returns an owned `double` array, and
`cxpr_host_block_field_string_by_key` / `_string_list_by_key` return owned
string data. Release these results with `free`; for a string list, free each
string and then the array. This differs from the similarly named raw-value
accessors, which remain borrowed.

Compiled-model introspection is grouped by binding, parameter, state default,
input, output, imported child, history requirement, selected backend/fusion/
trace state, and fast-path slot/input/export/output/commit layout. Count first,
then access indices below that count. Returned names and diagnostic reasons are
borrowed from the compiled program. The generated-C layout accessors
(`cxpr_model_compiled_c_param_*`, call parameters, and resample requirements)
are the authoritative ABI order.

Companion public headers have deliberately separate roles:

- `<cxpr/model/compiled.h>` is the convenience include for compiled models;
- `<cxpr/model/imports.h>` owns import-graph construction;
- `<cxpr/model/plugin.h>` defines model-plugin descriptors and execution;
- `<cxpr/model/runtime.h>` supplies header-only helpers referenced by emitted C.

Plugin loading and lower-level code generation are documented separately in
`plugins-codegen.md` and `generated-c.md`.
