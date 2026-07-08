/**
 * @file cuda.h
 * @brief Host-agnostic CUDA source artifact plugin for cxpr models.
 */

#ifndef CXPR_PLUGINS_CUDA_H
#define CXPR_PLUGINS_CUDA_H

#include <cxpr/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cxpr_cuda_plugin_options {
    const char* function_name;
    const char* qualifiers;
} cxpr_cuda_plugin_options;

int cxpr_cuda_plugin_emit_source(
    const cxpr_plugin_model_event* event,
    const cxpr_cuda_plugin_options* options,
    const cxpr_plugin_host* host,
    cxpr_error* err);

char* cxpr_cuda_plugin_source_from_program(
    const cxpr_model_program* program,
    const cxpr_cuda_plugin_options* options,
    cxpr_error* err);

void cxpr_cuda_plugin_source_free(char* source);

const cxpr_plugin_backend* cxpr_cuda_plugin_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_PLUGINS_CUDA_H */
