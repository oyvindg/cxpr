/**
 * @file model/imports.h
 * @brief Host-agnostic loading and compilation of .cxpr import bundles.
 */

#ifndef CXPR_MODEL_IMPORT_GRAPH_H
#define CXPR_MODEL_IMPORT_GRAPH_H

#include <cxpr/model/model.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque bundle containing a resolved model import graph. */
typedef struct cxpr_model_import_bundle cxpr_model_import_bundle;

/**
 * Resolve and load one `use` declaration.
 *
 * The callback owns path policy and I/O. On success it returns heap-allocated
 * canonical ID and source strings; the graph takes ownership of both. A
 * successful callback may leave both outputs NULL to mark a host-owned `use`
 * declaration that should not be compiled as a model import.
 */
typedef bool (*cxpr_model_import_load_fn)(
    const char* importer_id,
    const char* use_path,
    void* userdata,
    char** out_canonical_id,
    char** out_source,
    cxpr_error* err);

/**
 * Build and compile the imports reachable from an already parsed root model.
 *
 * `root_id` is an opaque identity passed back to the loader. CXPR does not
 * interpret it as a filesystem path. Imports are compiled with the reference
 * backend because their programs are compile-time dependencies of the caller;
 * choosing the root execution backend remains the caller's responsibility.
 */
cxpr_model_import_bundle* cxpr_model_import_bundle_build(
    const char* root_id,
    const cxpr_model* root_model,
    cxpr_model_import_load_fn load,
    void* userdata,
    cxpr_error* err);

void cxpr_model_import_bundle_free(cxpr_model_import_bundle* bundle);

/** Direct imports to pass to `cxpr_model_compile_with_imports*` for the root. */
const cxpr_model_import* cxpr_model_import_bundle_root_imports(
    const cxpr_model_import_bundle* bundle,
    size_t* out_count);

size_t cxpr_model_import_bundle_count(const cxpr_model_import_bundle* bundle);
const char* cxpr_model_import_bundle_id(
    const cxpr_model_import_bundle* bundle,
    size_t index);

#ifdef __cplusplus
}
#endif

#endif
