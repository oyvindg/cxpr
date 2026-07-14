#ifndef CXPR_MODEL_CODEGEN_INTERNAL_H
#define CXPR_MODEL_CODEGEN_INTERNAL_H

#include "model/internal.h"
#include "model/window/plan.h"
#include <cxpr/codegen.h>
#include <stddef.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
    bool oom;
} cxpr_model_c_buf;

void cxpr_model_c_reserve(cxpr_model_c_buf* b, size_t extra);
void cxpr_model_c_puts(cxpr_model_c_buf* b, const char* s);
void cxpr_model_c_printf(cxpr_model_c_buf* b, const char* fmt, ...);
void cxpr_model_c_format_double(char* out, size_t out_size, double value);
char* cxpr_model_c_safe_name(const char* name);
char* cxpr_model_c_function_name(const char* name);
char* cxpr_model_c_scoped_function_name(const char* scope, const char* name);
char* cxpr_model_c_prefixed_name(const char* prefix, const char* name);
unsigned long cxpr_model_c_name_hash(const char* s);
char* cxpr_model_c_child_tick_name(const char* function_prefix, size_t child_index);
char* cxpr_model_c_child_field_name(const char* function_prefix,
                                    size_t child_index,
                                    size_t field_index);
bool cxpr_model_c_is_power_of_two(size_t value);
bool cxpr_model_c_history_use_shift(size_t depth);
size_t cxpr_model_c_history_capacity(size_t depth);
size_t cxpr_model_program_param_index(const cxpr_model_program* program,
                                      const char* name);
const char* cxpr_model_c_binary_op(cxpr_opcode op);
bool cxpr_model_c_emit_defined_functions(const cxpr_model_program* program,
                                         cxpr_model_c_buf* b,
                                         cxpr_error* err);
bool cxpr_model_c_emit_dynamic_history_value(cxpr_model_c_buf* b,
                                             const char* value_name,
                                             const cxpr_ast* ast,
                                             const char* offset_expr,
                                             const cxpr_c_target* target,
                                             const cxpr_model_program* program,
                                             cxpr_error* err);
bool cxpr_model_c_emit_planned_roc_rolling_update(
    cxpr_model_c_buf* b,
    const char* name,
    const cxpr_model_window_plan_node* node,
    size_t node_index,
    size_t roc_capacity,
    size_t aggregate_capacity,
    const char* counter_type,
    const char* now_expr,
    const char* prev_expr,
    cxpr_error* err);
bool cxpr_model_c_emit_planned_roc_aggregate_fallback(
    cxpr_model_c_buf* b,
    const char* name,
    const cxpr_ast* value_ast,
    cxpr_model_window_plan_op op,
    const cxpr_c_target* target,
    const cxpr_model_program* program,
    cxpr_error* err);
bool cxpr_model_program_to_c_tick_function_ast(const cxpr_model_program* program,
                                               const char* qualifiers,
                                               const char* function_name,
                                               const double* literal_param_values,
                                               size_t literal_param_count,
                                               const size_t* output_indices,
                                               size_t selected_output_count,
                                               char** out_source,
                                               cxpr_error* err);

#endif /* CXPR_MODEL_CODEGEN_INTERNAL_H */
