/**
 * @file c.h
 * @brief Host-agnostic C source artifact plugin for cxpr models.
 */

#ifndef CXPR_PLUGINS_C_H
#define CXPR_PLUGINS_C_H

#include <cxpr/model/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Options controlling C source generation for one model. */
typedef struct cxpr_c_plugin_options {
    const char* function_name;       /**< Generated evaluator function name. */
    const char* qualifiers;          /**< Optional function qualifiers. */
    const double* param_values;      /**< Optional compile-time parameters. */
    size_t param_count;              /**< Number of parameter values. */
    const size_t* output_indices;    /**< Optional selected output indices. */
    size_t output_count;             /**< Number of selected outputs. */
    int include_headers;             /**< Whether to emit required C headers. */
} cxpr_c_plugin_options;

/**
 * @brief Generate C source for a plugin model event and emit it through the host.
 * @return Non-zero on success, otherwise zero with @p err populated.
 */
int cxpr_c_plugin_emit_source(
    const cxpr_model_plugin_event* event,
    const cxpr_c_plugin_options* options,
    const cxpr_model_plugin_host* host,
    cxpr_error* err);

/**
 * @brief Generate an owned C source string from a compiled model program.
 * @return Owned source on success, or NULL on failure.
 */
char* cxpr_c_plugin_source_from_program(
    const cxpr_model_compiled* program,
    const cxpr_c_plugin_options* options,
    cxpr_error* err);

/**
 * @brief Generate an owned, complete C artifact from a compiled model program.
 *
 * The artifact contains the evaluator and a static
 * `cxpr_generated_model_descriptor` named `<function_name>_descriptor`.
 *
 * @param model_name Stable model name stored in the descriptor.
 * @return Owned source on success, or NULL on failure.
 */
char* cxpr_c_plugin_artifact_from_program(
    const cxpr_model_compiled* program,
    const char* model_name,
    const cxpr_c_plugin_options* options,
    cxpr_error* err);

/**
 * @brief Generate and emit a complete evaluator plus descriptor artifact.
 * @return Non-zero on success, otherwise zero with @p err populated.
 */
int cxpr_c_plugin_emit_artifact(
    const cxpr_model_plugin_event* event,
    const cxpr_c_plugin_options* options,
    const cxpr_model_plugin_host* host,
    cxpr_error* err);

/** @brief Free source returned by @ref cxpr_c_plugin_source_from_program. */
void cxpr_c_plugin_source_free(char* source);

/** @brief Return the static C source plugin backend descriptor. */
const cxpr_model_plugin_backend* cxpr_c_plugin_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_PLUGINS_C_H */
