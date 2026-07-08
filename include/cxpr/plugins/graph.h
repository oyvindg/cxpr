/**
 * @file graph.h
 * @brief Host-agnostic model graph plugin for cxpr models.
 */

#ifndef CXPR_PLUGINS_GRAPH_H
#define CXPR_PLUGINS_GRAPH_H

#include <cxpr/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Options for graph manifest generation.
 *
 * The graph manifest is renderer-neutral. Cytoscape, dyn GUI, or any other
 * host can map the emitted nodes and edges to its own visual model.
 */
typedef struct cxpr_graph_plugin_options {
    /** Include expression source strings on param/binding/state nodes. */
    int include_expression_source;
    /** Include metadata nodes and annotation edges. */
    int include_metadata;
} cxpr_graph_plugin_options;

/**
 * @brief Emit a JSON graph artifact for a parsed model.
 *
 * Artifact kind: `cxpr.graph.v1`.
 *
 * @param event Model event. `event->model` is required; `event->program` is optional.
 * @param options Optional graph options. NULL uses defaults.
 * @param host Artifact callbacks supplied by the embedding host.
 * @param err Optional error output.
 * @return Non-zero on success, zero on invalid input, allocation failure, or host callback failure.
 */
int cxpr_graph_plugin_emit_graph(
    const cxpr_plugin_model_event* event,
    const cxpr_graph_plugin_options* options,
    const cxpr_plugin_host* host,
    cxpr_error* err);

/**
 * @brief Build a JSON graph manifest string for a parsed model.
 *
 * @param model Parsed model to inspect.
 * @param options Optional graph options. NULL uses defaults.
 * @param err Optional error output.
 * @return Allocated NUL-terminated JSON string, or NULL on failure. Free with
 *         @ref cxpr_graph_plugin_graph_free.
 */
char* cxpr_graph_plugin_graph_from_model(
    const cxpr_model* model,
    const cxpr_graph_plugin_options* options,
    cxpr_error* err);

/** @brief Free a graph string returned by @ref cxpr_graph_plugin_graph_from_model. */
void cxpr_graph_plugin_graph_free(char* graph);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_PLUGINS_GRAPH_H */
