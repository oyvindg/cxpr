/**
 * @file c.h
 * @brief Host-agnostic C source artifact plugin for cxpr models.
 */

#ifndef CXPR_PLUGINS_C_H
#define CXPR_PLUGINS_C_H

#include <cxpr/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cxpr_c_plugin_options {
    const char* function_name;
    const char* qualifiers;
    const double* param_values;
    size_t param_count;
    const size_t* output_indices;
    size_t output_count;
    int include_headers;
} cxpr_c_plugin_options;

int cxpr_c_plugin_emit_source(
    const cxpr_plugin_model_event* event,
    const cxpr_c_plugin_options* options,
    const cxpr_plugin_host* host,
    cxpr_error* err);

char* cxpr_c_plugin_source_from_program(
    const cxpr_model_program* program,
    const cxpr_c_plugin_options* options,
    cxpr_error* err);

void cxpr_c_plugin_source_free(char* source);

const cxpr_plugin_backend* cxpr_c_plugin_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_PLUGINS_C_H */
