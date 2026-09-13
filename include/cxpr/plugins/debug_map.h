/**
 * @file debug_map.h
 * @brief Generated-C debug-map artifact plugin for cxpr models.
 */

#ifndef CXPR_PLUGINS_DEBUG_MAP_H
#define CXPR_PLUGINS_DEBUG_MAP_H

#include <cxpr/model/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cxpr_debug_map_plugin_options {
    /** C identifier for the emitted cxpr_debug_map object. */
    const char* symbol_name;
    /** Object qualifiers; defaults to "static const". */
    const char* qualifiers;
    /** Optional selected public output indices. */
    const size_t* output_indices;
    /** Number of selected outputs; zero emits all public outputs. */
    size_t output_count;
} cxpr_debug_map_plugin_options;

/**
 * Build an owned C source artifact containing a versioned cxpr_debug_map.
 *
 * Node and output IDs are deterministic FNV-1a hashes of their semantic kind
 * and name. Source spans use @p source_path when the parser retained a span.
 */
char* cxpr_debug_map_plugin_source_from_model(
    const cxpr_model* model,
    const cxpr_model_compiled* compiled,
    const char* source_path,
    const cxpr_debug_map_plugin_options* options,
    cxpr_error* err);

/** Emit artifact kind `cxpr.debug-map.c.v1` through a generic plugin host. */
int cxpr_debug_map_plugin_emit(
    const cxpr_model_plugin_event* event,
    const cxpr_debug_map_plugin_options* options,
    const cxpr_model_plugin_host* host,
    cxpr_error* err);

void cxpr_debug_map_plugin_source_free(char* source);

/** Return the static generated-C debug-map backend descriptor. */
const cxpr_model_plugin_backend* cxpr_debug_map_plugin_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_PLUGINS_DEBUG_MAP_H */
