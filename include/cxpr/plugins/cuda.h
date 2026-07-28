/**
 * @file cuda.h
 * @brief Host-agnostic CUDA source artifact plugin for cxpr models.
 */

#ifndef CXPR_PLUGINS_CUDA_H
#define CXPR_PLUGINS_CUDA_H

#include <cxpr/model/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Options controlling CUDA source generation for one model. */
typedef struct cxpr_cuda_plugin_options {
    const char* function_name; /**< Generated device function name. */
    const char* qualifiers;    /**< Optional CUDA function qualifiers. */
} cxpr_cuda_plugin_options;

/**
 * @brief Generate CUDA source for a plugin model event and emit it through the host.
 * @return Non-zero on success, otherwise zero with @p err populated.
 */
int cxpr_cuda_plugin_emit_source(
    const cxpr_model_plugin_event* event,
    const cxpr_cuda_plugin_options* options,
    const cxpr_model_plugin_host* host,
    cxpr_error* err);

/**
 * @brief Generate an owned CUDA source string from a compiled model program.
 * @return Owned source on success, or NULL on failure.
 */
char* cxpr_cuda_plugin_source_from_program(
    const cxpr_model_compiled* program,
    const cxpr_cuda_plugin_options* options,
    cxpr_error* err);

/** @brief Free source returned by @ref cxpr_cuda_plugin_source_from_program. */
void cxpr_cuda_plugin_source_free(char* source);

/** @brief Return the static CUDA source plugin backend descriptor. */
const cxpr_model_plugin_backend* cxpr_cuda_plugin_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_PLUGINS_CUDA_H */
