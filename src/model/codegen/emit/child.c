#include "model/codegen/ast/internal.h"
#include "model/window/window.h"
#include "registry/internal.h"

#include <cxpr/resample.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool cxpr_model_c_child_is_used(const size_t* child_call_child_indices,
                                       size_t child_call_count,
                                       size_t child_index) {
    for (size_t i = 0u; i < child_call_count; ++i) {
        if (child_call_child_indices[i] == child_index) return true;
    }
    return false;
}

static const char* cxpr_model_c_common_helpers_source(void) {
    return "#include <cxpr/model/runtime.h>\n\n"
           "#include <stdint.h>\n\n";
}

bool cxpr_model_c_emit_child_model_helpers(
    const cxpr_model_compiled* program,
    const char* function_prefix,
    const size_t* child_call_child_indices,
    size_t child_call_count,
    cxpr_model_c_buf* b,
    cxpr_error* err) {
    if (!program || !function_prefix || !b) return true;
    for (size_t i = 0u; i < program->child_count; ++i) {
        const cxpr_model_compiled* child = program->children[i].program;
        char* tick_name;
        char* tick_source;
        const char* nested_source;
        if (!cxpr_model_c_child_is_used(
                child_call_child_indices, child_call_count, i)) {
            continue;
        }
        if (!child) continue;
        tick_name = cxpr_model_c_child_tick_name(function_prefix, i);
        if (!tick_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        cxpr_model_c_printf(b, "/* Source model tick: %s */\n",
                            program->children[i].name ? program->children[i].name : "(unnamed)");
        tick_source = cxpr_model_compiled_generate_c_outputs(
            child, "static inline", tick_name, NULL, 0u, err);
        if (!tick_source) {
            free(tick_name);
            return false;
        }
        nested_source = tick_source;
        if (strncmp(nested_source,
                    cxpr_model_c_common_helpers_source(),
                    strlen(cxpr_model_c_common_helpers_source())) == 0) {
            nested_source += strlen(cxpr_model_c_common_helpers_source());
        }
        cxpr_model_c_puts(b, nested_source);
        cxpr_model_c_puts(b, "\n");
        free(tick_source);

        for (size_t field_i = 0u; field_i < child->output_count; ++field_i) {
            char* helper_name;
            helper_name = cxpr_model_c_child_field_name(function_prefix, i, field_i);
            if (!helper_name) {
                free(tick_name);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_printf(b, "/* Source model field: %s.%s */\n",
                                program->children[i].name ? program->children[i].name : "(unnamed)",
                                child->outputs[field_i] ? child->outputs[field_i] : "(unnamed)");
            cxpr_model_c_printf(b,
                                "static inline double %s(uint8_t* restrict _cx_child_initialized, double* restrict _cx_child_outputs, %s_state* restrict _cx_child_state",
                                helper_name,
                                tick_name);
            for (size_t in_i = 0u; in_i < child->input_count; ++in_i) {
                char* input_name = cxpr_model_c_safe_name(child->inputs[in_i]);
                if (!input_name) {
                    free(helper_name);
                    free(tick_name);
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    return false;
                }
                cxpr_model_c_printf(b, ", double %s", input_name);
                free(input_name);
            }
            for (size_t p = 0u; p < child->constant_count; ++p) {
                char* param_name = cxpr_model_c_prefixed_name("param_", child->constants[p].name);
                if (!param_name) {
                    free(helper_name);
                    free(tick_name);
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    return false;
                }
                cxpr_model_c_printf(b, ", double %s", param_name);
                free(param_name);
            }
            cxpr_model_c_puts(b, ") {\n");
            cxpr_model_c_printf(b, "    double _cx_child_inputs[%zu] = {",
                                child->input_count ? child->input_count : 1u);
            for (size_t in_i = 0u; in_i < child->input_count; ++in_i) {
                char* input_name = cxpr_model_c_safe_name(child->inputs[in_i]);
                if (in_i > 0u) cxpr_model_c_puts(b, ", ");
                cxpr_model_c_puts(b, input_name ? input_name : "0.0");
                free(input_name);
            }
            if (child->input_count == 0u) cxpr_model_c_puts(b, "0.0");
            cxpr_model_c_puts(b, "};\n");
            cxpr_model_c_printf(b, "    double _cx_child_params[%zu] = {",
                                child->constant_count ? child->constant_count : 1u);
            for (size_t p = 0u; p < child->constant_count; ++p) {
                char* param_name = cxpr_model_c_prefixed_name("param_", child->constants[p].name);
                if (p > 0u) cxpr_model_c_puts(b, ", ");
                cxpr_model_c_puts(b, param_name ? param_name : "0.0");
                free(param_name);
            }
            if (child->constant_count == 0u) cxpr_model_c_puts(b, "0.0");
            cxpr_model_c_puts(b, "};\n");
            cxpr_model_c_puts(b, "    if (*_cx_child_initialized == 0u) {\n");
            cxpr_model_c_printf(b,
                                "        %s(_cx_child_state, _cx_child_inputs, _cx_child_params, _cx_child_outputs);\n",
                                tick_name);
            cxpr_model_c_puts(b, "        *_cx_child_initialized = 1u;\n");
            cxpr_model_c_puts(b, "    }\n");
            cxpr_model_c_printf(b, "    return _cx_child_outputs[%zu];\n", field_i);
            cxpr_model_c_puts(b, "}\n\n");
            free(helper_name);
        }
        free(tick_name);
        if (b->oom) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    return true;
}

void cxpr_model_c_emit_common_helpers(cxpr_model_c_buf* b) {
    cxpr_model_c_puts(b, cxpr_model_c_common_helpers_source());
}
