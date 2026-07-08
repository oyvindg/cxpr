/**
 * @file meta.h
 * @brief Host-agnostic metadata manifest plugin for cxpr models.
 */

#ifndef CXPR_PLUGINS_META_H
#define CXPR_PLUGINS_META_H

#include <cxpr/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Options for metadata manifest generation.
 *
 * The default options include declarations and raw metadata bodies. Raw bodies
 * preserve the original `.cxpr` metadata content, so hosts can keep existing
 * chart/plot/label behavior while migrating to a generic plugin boundary.
 */
typedef struct cxpr_meta_plugin_options {
    /** Include `use`, `in`, `$param`, binding, and output declarations. */
    int include_declarations;
    /** Include raw metadata blocks and per-output metadata references. */
    int include_metadata;
} cxpr_meta_plugin_options;

/**
 * @brief Emit a JSON metadata manifest artifact for a parsed model.
 *
 * Artifact kind: `cxpr.meta.manifest.v1`.
 *
 * @param event Model event. `event->model` is required; `event->program` is optional.
 * @param options Optional manifest options. NULL uses the defaults.
 * @param host Artifact callbacks supplied by the embedding host.
 * @param err Optional error output.
 * @return Non-zero on success, zero on invalid input, allocation failure, or host callback failure.
 */
int cxpr_meta_plugin_emit_manifest(
    const cxpr_plugin_model_event* event,
    const cxpr_meta_plugin_options* options,
    const cxpr_plugin_host* host,
    cxpr_error* err);

/**
 * @brief Build a JSON metadata manifest string for a parsed model.
 *
 * @param model Parsed model to inspect.
 * @param options Optional manifest options. NULL uses the defaults.
 * @param err Optional error output.
 * @return Allocated NUL-terminated JSON string, or NULL on failure. Free with
 *         @ref cxpr_meta_plugin_manifest_free.
 */
char* cxpr_meta_plugin_manifest_from_model(
    const cxpr_model* model,
    const cxpr_meta_plugin_options* options,
    cxpr_error* err);

/** @brief Free a manifest string returned by @ref cxpr_meta_plugin_manifest_from_model. */
void cxpr_meta_plugin_manifest_free(char* manifest);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_PLUGINS_META_H */
