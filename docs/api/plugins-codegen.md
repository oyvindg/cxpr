# Plugins and code generation

The plugin API turns a parsed/compiled model into artifacts while keeping cxpr
host-agnostic. `<cxpr/model/plugin.h>` defines an immutable, borrowed model event
and a host sink with begin/write/end callbacks. Event pointers are valid only
for the plugin call. Artifact content is plugin-owned until written; storage,
filenames, atomic replacement, packaging, and build integration are host policy.

Run a backend through `cxpr_model_plugin_run()`. Backend-specific options are
opaque to core cxpr. Built-in public backends include:

- `<cxpr/plugins/c.h>` for portable C source and optional generated descriptors.
- `<cxpr/plugins/cuda.h>` for CUDA device source.
- `<cxpr/plugins/meta.h>` for metadata manifests.
- `<cxpr/plugins/graph.h>` for model graphs.
- `<cxpr/plugins/debug_map.h>` for versioned generated-C debug metadata.

Functions returning an allocated artifact string transfer ownership to the
caller; use the matching plugin free function. Emit functions stream content
through the supplied host callbacks instead. The debug-map contract has its own
`CXPR_DEBUG_MAP_ABI_VERSION == 1`; generated evaluator descriptors separately
use `CXPR_GENERATED_MODEL_ABI_VERSION == 5`.

The C backend is the normal deployment path for CPU hosts and the scalar basis
for bulk execution. The CUDA backend emits device-compatible model math, but it
does not choose grid geometry, allocate/copy device buffers, create streams, or
launch kernels. Those operations belong in the host wrapper. CUDA codegen also
rejects unsupported dynamic extension indexing and aggregate array transport
rather than introducing a CUDA-specific expression semantic.

A typical build pipeline parses the `.cxpr` model, compiles it once, invokes one
or more plugins, writes each artifact through the host sink, compiles generated
C/CUDA with the application's toolchain, and validates the artifact ABI before
execution.

See `tests/c_plugin.test.c`, `tests/plugin_meta.test.c`,
`tests/plugin_graph.test.c`, `tests/debug_map_plugin.test.c`, and
`tests/cuda_bulk_codegen_tool.c`.

## API inventory

| Backend/header | Public entry points |
| --- | --- |
| Generic `<cxpr/model/plugin.h>` | `cxpr_model_plugin_run` |
| C `<cxpr/plugins/c.h>` | `cxpr_c_plugin_emit_source`, `cxpr_c_plugin_source_from_program`, `cxpr_c_plugin_artifact_from_program`, `cxpr_c_plugin_emit_artifact`, `cxpr_c_plugin_source_free`, `cxpr_c_plugin_backend` |
| CUDA `<cxpr/plugins/cuda.h>` | `cxpr_cuda_plugin_emit_source`, `cxpr_cuda_plugin_source_from_program`, `cxpr_cuda_plugin_source_free`, `cxpr_cuda_plugin_backend` |
| Metadata `<cxpr/plugins/meta.h>` | `cxpr_meta_plugin_emit_manifest`, `cxpr_meta_plugin_manifest_from_model`, `cxpr_meta_plugin_manifest_free` |
| Graph `<cxpr/plugins/graph.h>` | `cxpr_graph_plugin_emit_graph`, `cxpr_graph_plugin_graph_from_model`, `cxpr_graph_plugin_graph_free` |
| Debug map `<cxpr/plugins/debug_map.h>` | `cxpr_debug_map_plugin_source_from_model`, `cxpr_debug_map_plugin_emit`, `cxpr_debug_map_plugin_source_free`, `cxpr_debug_map_plugin_backend` |
| Debug-map ABI `<cxpr/debug_map.h>` | `cxpr_debug_map_validate` |
