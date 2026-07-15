#include "model/internal.h"
#include "model/codegen/internal.h"
#include "model/window/plan.h"
#include "model/window/window.h"
#include "registry/internal.h"
#include <cxpr/codegen.h>
#include "eval/internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* cxpr_model_program_function_to_c_function(const cxpr_model_program* program,
                                                const char* name,
                                                const char* qualifiers,
                                                const char* return_type,
                                                const char* function_name,
                                                cxpr_error* err) {
    if (err) *err = (cxpr_error){0};
    if (!program || !program->registry) {
        cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION,
                             "Model has no defined function registry", 0, 0);
        return NULL;
    }
    return cxpr_registry_defined_fn_to_c_function(program->registry,
                                                  name,
                                                  qualifiers,
                                                  return_type,
                                                  function_name,
                                                  err);
}

size_t cxpr_model_program_param_index(const cxpr_model_program* program,
                                      const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->constant_count; ++i) {
        if (cxpr_model_names_match(program->constants[i].name, name)) return i;
    }
    return (size_t)-1;
}

const char* cxpr_model_c_binary_op(cxpr_opcode op) {
    switch (op) {
    case CXPR_OP_ADD: return "+";
    case CXPR_OP_SUB: return "-";
    case CXPR_OP_MUL: return "*";
    case CXPR_OP_DIV: return "/";
    case CXPR_OP_CMP_EQ: return "==";
    case CXPR_OP_CMP_NEQ: return "!=";
    case CXPR_OP_CMP_LT: return "<";
    case CXPR_OP_CMP_LTE: return "<=";
    case CXPR_OP_CMP_GT: return ">";
    case CXPR_OP_CMP_GTE: return ">=";
    default: return NULL;
    }
}

bool cxpr_model_c_emit_defined_functions(const cxpr_model_program* program,
                                         cxpr_model_c_buf* b,
                                         cxpr_error* err) {
    if (!program || !program->registry) return true;
    for (size_t i = 0u; i < program->registry->count; ++i) {
        cxpr_func_entry* entry = &program->registry->entries[i];
        char* fn_name;
        char* source;
        if (!entry->defined_body || entry->defined_return_field_count > 0u) continue;
        fn_name = cxpr_model_c_function_name(entry->name);
        if (!fn_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        source = cxpr_registry_defined_fn_to_c_function(program->registry,
                                                        entry->name,
                                                        "static inline",
                                                        "double",
                                                        fn_name,
                                                        err);
        free(fn_name);
        if (!source) return false;
        cxpr_model_c_printf(b, "/* Source function: %s */\n",
                            entry->name ? entry->name : "(unnamed)");
        cxpr_model_c_puts(b, source);
        cxpr_model_c_puts(b, "\n");
        free(source);
        if (b->oom) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    return true;
}

typedef struct {
    const cxpr_model_program* program;
    char** param_names;
    char** param_exprs;
    size_t param_count;
    const char* inline_fn_name;
    const char* function_prefix;
    const double* literal_param_values;
    size_t literal_param_count;
} cxpr_model_ast_c_target;

typedef struct {
    cxpr_model_c_buf* declarations;
    const cxpr_c_target* target;
    size_t next_temp;
} cxpr_model_ast_temp_emit;

static bool cxpr_model_c_defined_function_used(const cxpr_model_program* program,
                                               const char* name,
                                               bool* used,
                                               cxpr_error* err);
static bool cxpr_model_c_collect_defined_function_refs(const cxpr_model_program* program,
                                                       const cxpr_ast* ast,
                                                       bool* used,
                                                       cxpr_error* err);
static char* cxpr_model_ast_c_emit_leaf(const cxpr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        cxpr_error* err);
static char* cxpr_model_ast_c_emit_call(const cxpr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        bool* handled,
                                        cxpr_error* err);
static char* cxpr_model_ast_expr_to_c(const cxpr_model_program* program,
                                      const cxpr_ast* ast,
                                      const char* function_prefix,
                                      const double* literal_param_values,
                                      size_t literal_param_count,
                                      cxpr_error* err);
static char* cxpr_model_ast_expr_to_c_with_temps(cxpr_model_ast_temp_emit* emit,
                                                 const cxpr_ast* ast,
                                                 cxpr_error* err);

static bool cxpr_model_c_symbol_is_input(const cxpr_model_program* program,
                                         const char* name,
                                         size_t* out_index) {
    if (!program || !name) return false;
    for (size_t i = 0u; i < program->fused_input_count; ++i) {
        if (cxpr_model_names_match(program->fused_inputs[i].name, name)) {
            if (out_index) *out_index = i;
            return true;
        }
    }
    return false;
}

static bool cxpr_model_c_symbol_is_state(const cxpr_model_program* program,
                                         const char* name,
                                         size_t* out_slot) {
    if (!program || !name) return false;
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) {
            size_t slot = cxpr_model_fused_slot_find(
                program->fused_slot_names, program->fused_slot_count, name);
            if (out_slot) *out_slot = slot;
            return slot != (size_t)-1;
        }
    }
    return false;
}

static bool cxpr_model_c_symbol_is_binding(const cxpr_model_program* program,
                                           const char* name) {
    if (!program || !name) return false;
    for (size_t i = 0u; i < program->binding_count; ++i) {
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) continue;
        if (cxpr_model_names_match(program->bindings[i].name, name)) return true;
    }
    return false;
}

static const cxpr_ast* cxpr_model_producer_arg_for_param(const cxpr_ast* ast,
                                                         const char* param_name,
                                                         size_t param_index) {
    if (!ast || cxpr_ast_type(ast) != CXPR_NODE_PRODUCER_ACCESS) return NULL;
    if (cxpr_ast_producer_has_named_args(ast)) {
        for (size_t i = 0u; i < cxpr_ast_producer_argc(ast); ++i) {
            const char* arg_name = cxpr_ast_producer_arg_name(ast, i);
            if (arg_name && param_name && cxpr_model_names_match(arg_name, param_name)) {
                return cxpr_ast_producer_arg(ast, i);
            }
        }
        return NULL;
    }
    return param_index < cxpr_ast_producer_argc(ast)
               ? cxpr_ast_producer_arg(ast, param_index)
               : NULL;
}

static size_t cxpr_model_c_child_base_inline(const cxpr_model_program* program,
                                             size_t child_index) {
    size_t base;
    if (!program || child_index >= program->child_count) return (size_t)-1;
    base = program->state_default_count;
    for (size_t i = 0u; i < child_index; ++i) {
        const cxpr_model_program* child = program->children[i].program;
        base += cxpr_model_program_c_slot_count(child) + 1u + child->output_count;
    }
    return base;
}

static size_t cxpr_model_c_child_index_for_entry(const cxpr_model_program* program,
                                                 const cxpr_func_entry* entry) {
    if (!program || !entry || !entry->model_producer_userdata) return (size_t)-1;
    for (size_t i = 0u; i < program->child_count; ++i) {
        if (&program->children[i] == entry->model_producer_userdata) return i;
    }
    return (size_t)-1;
}

static char* cxpr_model_ast_producer_access_to_c(const cxpr_model_program* program,
                                                 const cxpr_ast* ast,
                                                 const char* function_prefix,
                                                 const double* literal_param_values,
                                                 size_t literal_param_count,
                                                 cxpr_error* err) {
    cxpr_func_entry* entry;
    const char* producer;
    const char* field;
    size_t selected_field = (size_t)-1;
    char** arg_exprs = NULL;
    char** field_exprs = NULL;
    cxpr_model_ast_c_target target_data;
    cxpr_c_target target;

    if (!program || !program->registry || !ast ||
        cxpr_ast_type(ast) != CXPR_NODE_PRODUCER_ACCESS) {
        return NULL;
    }
    producer = cxpr_ast_producer_name(ast);
    field = cxpr_ast_producer_field(ast);
    entry = cxpr_registry_find(program->registry, producer);
    if (!entry || (!entry->model_producer && entry->defined_return_field_count == 0u) ||
        entry->defined_param_count != cxpr_ast_producer_argc(ast)) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_FUNCTION;
            err->message = "Unsupported model C producer access";
        }
        return NULL;
    }
    for (size_t i = 0u; i < entry->defined_return_field_count; ++i) {
        if (entry->defined_return_field_names[i] &&
            field &&
            strcmp(entry->defined_return_field_names[i], field) == 0) {
            selected_field = i;
            break;
        }
    }
    if (selected_field == (size_t)-1) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            err->message = "Unknown producer field";
        }
        return NULL;
    }

    if (entry->model_producer) {
        size_t child_index = cxpr_model_c_child_index_for_entry(program, entry);
        const cxpr_model_program* child =
            child_index == (size_t)-1 ? NULL : program->children[child_index].program;
        char* helper_name;
        cxpr_model_c_buf call = {0};
        if (!child) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Unknown child model producer", 0, 0);
            return NULL;
        }
        (void)producer;
        helper_name = cxpr_model_c_child_field_name(function_prefix, child_index, selected_field);
        if (!helper_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        cxpr_model_c_printf(&call,
                            "%s(&_cx_state->child_%zu_initialized, _cx_state->child_%zu_outputs, &_cx_state->child_%zu_state",
                            helper_name,
                            child_index,
                            child_index,
                            child_index);
        free(helper_name);
        for (size_t i = 0u; i < child->input_count; ++i) {
            size_t input_index = 0u;
            if (!cxpr_model_c_symbol_is_input(program, child->inputs[i], &input_index)) {
                free(call.data);
                cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                     "Child model input is not a parent input", 0, 0);
                return NULL;
            }
            cxpr_model_c_printf(&call, ", _cx_input_%zu", input_index);
        }
        target_data = (cxpr_model_ast_c_target){
            .program = program,
            .function_prefix = function_prefix,
            .literal_param_values = literal_param_values,
            .literal_param_count = literal_param_count,
        };
        target = (cxpr_c_target){
            .api_version = CXPR_C_TARGET_API_VERSION,
            .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
            .emit_call_at_offset = cxpr_model_ast_c_emit_call,
            .userdata = &target_data,
        };
        for (size_t i = 0u; i < entry->defined_param_count; ++i) {
            const cxpr_ast* arg = cxpr_model_producer_arg_for_param(
                ast, entry->defined_param_names ? entry->defined_param_names[i] : NULL, i);
            char* arg_expr;
            if (!arg) {
                free(call.data);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Missing producer argument", 0, 0);
                return NULL;
            }
            arg_expr = cxpr_ast_to_c_at_offset(arg, 0u, &target, err);
            if (!arg_expr) {
                free(call.data);
                return NULL;
            }
            cxpr_model_c_printf(&call, ", %s", arg_expr);
            free(arg_expr);
        }
        cxpr_model_c_puts(&call, ")");
        if (call.oom) {
            free(call.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return call.data;
    }

    arg_exprs = (char**)calloc(entry->defined_param_count ? entry->defined_param_count : 1u,
                              sizeof(char*));
    if (!arg_exprs) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    target_data = (cxpr_model_ast_c_target){
        .program = program,
        .function_prefix = function_prefix,
        .literal_param_values = literal_param_values,
        .literal_param_count = literal_param_count,
    };
    target = (cxpr_c_target){
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .userdata = &target_data,
    };
    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        const cxpr_ast* arg = cxpr_model_producer_arg_for_param(
            ast, entry->defined_param_names ? entry->defined_param_names[i] : NULL, i);
        if (!arg) {
            if (err) {
                err->code = CXPR_ERR_SYNTAX;
                err->message = "Missing producer argument";
            }
            goto fail;
        }
        arg_exprs[i] = cxpr_ast_to_c_at_offset(arg, 0u, &target, err);
        if (!arg_exprs[i]) goto fail;
    }

    field_exprs = (char**)calloc(entry->defined_return_field_count, sizeof(char*));
    if (!field_exprs) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        goto fail;
    }
    for (size_t field_i = 0u; field_i <= selected_field; ++field_i) {
        const size_t name_count = entry->defined_param_count + field_i;
        char** names = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        char** exprs = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        if (!names || !exprs) {
            free(names);
            free(exprs);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            goto fail;
        }
        for (size_t i = 0u; i < entry->defined_param_count; ++i) {
            names[i] = entry->defined_param_names[i];
            exprs[i] = arg_exprs[i];
        }
        for (size_t i = 0u; i < field_i; ++i) {
            names[entry->defined_param_count + i] = entry->defined_return_field_names[i];
            exprs[entry->defined_param_count + i] = field_exprs[i];
        }
        target_data.param_names = names;
        target_data.param_exprs = exprs;
        target_data.param_count = name_count;
        field_exprs[field_i] = cxpr_ast_to_c_at_offset(
            entry->defined_return_field_bodies[field_i], 0u, &target, err);
        free(names);
        free(exprs);
        if (!field_exprs[field_i]) goto fail;
    }
    {
        char* out = field_exprs[selected_field];
        field_exprs[selected_field] = NULL;
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
        free(field_exprs);
        for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
        free(arg_exprs);
        return out;
    }

fail:
    if (field_exprs) {
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
        free(field_exprs);
    }
    for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
    free(arg_exprs);
    return NULL;
}

static size_t cxpr_model_c_state_slot_for_name(const cxpr_model_program* program,
                                               const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) return i;
    }
    return (size_t)-1;
}

static size_t cxpr_model_c_state_slot_for_fused_slot(const cxpr_model_program* program,
                                                     size_t fused_slot) {
    if (!program || fused_slot >= program->fused_slot_count) return (size_t)-1;
    return cxpr_model_c_state_slot_for_name(program, program->fused_slot_names[fused_slot]);
}

static size_t cxpr_model_c_history_base(const cxpr_model_program* program,
                                        size_t history_index) {
    (void)program;
    (void)history_index;
    return (size_t)-1;
}

static size_t cxpr_model_c_history_find(const cxpr_model_program* program,
                                        const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        if (cxpr_model_names_match(program->history_specs[i].name, name)) return i;
    }
    return (size_t)-1;
}

static char* cxpr_model_c_current_symbol_expr(const cxpr_model_program* program,
                                              char** state_next_names,
                                              const char* name,
                                              cxpr_error* err) {
    size_t index = 0u;
    size_t slot = 0u;
    if (!program || !name) return NULL;
    if (cxpr_model_c_symbol_is_input(program, name, &index)) {
        char raw[64];
        snprintf(raw, sizeof(raw), "_cx_input_%zu", index);
        return cxpr_strdup(raw);
    }
    if (cxpr_model_c_symbol_is_state(program, name, &slot)) {
        size_t c_slot = cxpr_model_c_state_slot_for_fused_slot(program, slot);
        if (c_slot == (size_t)-1) return NULL;
        (void)state_next_names;
        (void)c_slot;
        return cxpr_model_c_prefixed_name("_cx_state_", name);
    }
    if (cxpr_model_c_symbol_is_binding(program, name)) {
        return cxpr_model_c_safe_name(name);
    }
    if (err) {
        err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
        err->message = "Unknown model C history target";
    }
    return NULL;
}

bool cxpr_model_c_emit_dynamic_history_value(cxpr_model_c_buf* b,
                                             const char* value_name,
                                             const cxpr_ast* ast,
                                             const char* offset_expr,
                                             const cxpr_c_target* target,
                                             const cxpr_model_program* program,
                                             cxpr_error* err) {
    char* current_expr = NULL;
    char* key = NULL;
    size_t hist_index;
    size_t depth;
    size_t capacity;

    if (!b || !value_name || !ast || !offset_expr || !target || !program) return false;
    if (!cxpr_model_lookback_target_key(ast, &key, err)) return false;
    hist_index = cxpr_model_c_history_find(program, key);
    free(key);
    if (hist_index == (size_t)-1) return false;
    current_expr = cxpr_ast_to_c_at_offset(ast, 0u, target, err);
    if (!current_expr) return false;
    depth = program->history_specs[hist_index].depth;
    capacity = cxpr_model_c_history_capacity(depth);
    cxpr_model_c_printf(
        b,
        "            double %s; if ((%s) == 0u) { %s = %s; } else if ((%s) <= %zuu) { ",
        value_name,
        offset_expr,
        value_name,
        current_expr,
        offset_expr,
        depth);
    free(current_expr);
    if (cxpr_model_c_history_use_shift(depth)) {
        cxpr_model_c_printf(
            b,
            "%s = _cx_state->history_%zu.values[(%s) - 1u];",
            value_name,
            hist_index,
            offset_expr);
    } else if (cxpr_model_c_is_power_of_two(capacity)) {
        cxpr_model_c_printf(
            b,
            "%s = _cx_state->history_%zu.values[(_cx_history_next_%zu + %zuu - (%s)) & %zuu];",
            value_name,
            hist_index,
            hist_index,
            capacity,
            offset_expr,
            capacity - 1u);
    } else {
        cxpr_model_c_printf(
            b,
            "%s = _cx_state->history_%zu.values[(_cx_history_next_%zu + %zuu - (%s)) %% %zuu];",
            value_name,
            hist_index,
            hist_index,
            capacity,
            offset_expr,
            capacity);
    }
    cxpr_model_c_printf(b, " } else { %s = NAN; }\n", value_name);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static char* cxpr_model_ast_c_emit_leaf(const cxpr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        cxpr_error* err) {
    cxpr_model_ast_c_target* target = (cxpr_model_ast_c_target*)userdata;
    const cxpr_model_program* program = target ? target->program : NULL;
    const char* name = NULL;
    size_t index = 0u;
    size_t slot = 0u;

    if (!program || !ast) return NULL;
    if (lookback_offset != 0u) {
        size_t hist_index;
        size_t depth;
        size_t capacity;
        char* key = NULL;
        if (!cxpr_model_lookback_target_key(ast, &key, err)) return NULL;
        name = key;
        hist_index = cxpr_model_c_history_find(program, name);
        if (hist_index == (size_t)-1) {
            free(key);
            if (err) {
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                err->message = "Unknown model C history target";
            }
            return NULL;
        }
        depth = program->history_specs[hist_index].depth;
        if (depth == 0u || lookback_offset == 0u || lookback_offset > depth) {
            char raw[32];
            free(key);
            snprintf(raw, sizeof(raw), "NAN");
            return cxpr_strdup(raw);
        }
        capacity = cxpr_model_c_history_capacity(depth);
        free(key);
        {
            char raw[256];
            char index_expr[128];
            size_t delta = capacity - (size_t)lookback_offset;
            if (cxpr_model_c_history_use_shift(depth)) {
                snprintf(raw, sizeof(raw),
                         "_cx_state->history_%zu.values[%zu]",
                         hist_index,
                         (size_t)lookback_offset - 1u);
                return cxpr_strdup(raw);
            }
            if (capacity == 1u) {
                snprintf(index_expr, sizeof(index_expr), "0u");
            } else if (delta == 0u) {
                snprintf(index_expr, sizeof(index_expr), "_cx_history_next_%zu", hist_index);
            } else if (cxpr_model_c_is_power_of_two(capacity)) {
                snprintf(index_expr, sizeof(index_expr),
                         "((_cx_history_next_%zu + %zuu) & %zuu)",
                         hist_index, delta, capacity - 1u);
            } else {
                snprintf(index_expr, sizeof(index_expr),
                         "((_cx_history_next_%zu + %zuu) %% %zuu)",
                         hist_index, delta, capacity);
            }
            snprintf(raw, sizeof(raw),
                     "_cx_state->history_%zu.values[%s]",
                     hist_index,
                     index_expr);
            return cxpr_strdup(raw);
        }
    }
    if (cxpr_ast_type(ast) == CXPR_NODE_IDENTIFIER) {
        name = cxpr_ast_identifier_name(ast);
        for (size_t i = 0u; target && i < target->param_count; ++i) {
            if (cxpr_model_names_match(target->param_names[i], name)) {
                if (target->param_exprs && target->param_exprs[i]) {
                    return cxpr_strdup(target->param_exprs[i]);
                }
                return cxpr_model_c_safe_name(name);
            }
        }
        if (cxpr_model_c_symbol_is_input(program, name, &index)) {
            char raw[64];
            snprintf(raw, sizeof(raw), "_cx_input_%zu", index);
            return cxpr_strdup(raw);
        }
        if (cxpr_model_c_symbol_is_state(program, name, &slot)) {
            return cxpr_model_c_prefixed_name("_cx_state_", name);
        }
        if (cxpr_model_c_symbol_is_binding(program, name)) {
            return cxpr_model_c_safe_name(name);
        }
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            err->message = "Unknown model C identifier";
        }
        return NULL;
    }
    if (cxpr_ast_type(ast) == CXPR_NODE_PRODUCER_ACCESS) {
        return cxpr_model_ast_producer_access_to_c(
            program,
            ast,
            target ? target->function_prefix : NULL,
            target ? target->literal_param_values : NULL,
            target ? target->literal_param_count : 0u,
            err);
    }
    if (cxpr_ast_type(ast) == CXPR_NODE_VARIABLE) {
        name = cxpr_ast_variable_name(ast);
        index = cxpr_model_program_param_index(program, name);
        if (index == (size_t)-1) {
            if (err) {
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                err->message = "Unknown model C parameter";
            }
            return NULL;
        }
        if (target &&
            target->literal_param_values &&
            index < target->literal_param_count) {
            char raw[64];
            cxpr_model_c_format_double(raw, sizeof(raw),
                                       target->literal_param_values[index]);
            return cxpr_strdup(raw);
        }
        {
            char raw[64];
            snprintf(raw, sizeof(raw), "_cx_param_%zu", index);
            return cxpr_strdup(raw);
        }
    }
    if (err) {
        err->code = CXPR_ERR_SYNTAX;
        err->message = "Unsupported model C leaf";
    }
    return NULL;
}

static const char* cxpr_model_c_window_op(const char* name) {
    if (cxpr_model_names_match(name, "window_sum")) return "0";
    if (cxpr_model_names_match(name, "window_mean")) return "1";
    if (cxpr_model_names_match(name, "window_highest")) return "2";
    if (cxpr_model_names_match(name, "window_lowest")) return "3";
    if (cxpr_model_names_match(name, "window_stddev")) return "4";
    return NULL;
}

static bool cxpr_model_c_window_period_capacity(const cxpr_model_program* program,
                                                const cxpr_ast* period_ast,
                                                size_t* out_capacity,
                                                cxpr_error* err) {
    double raw = 0.0;
    long period;
    if (!period_ast || !out_capacity) return false;
    if (cxpr_ast_type(period_ast) == CXPR_NODE_VARIABLE) {
        const char* name = cxpr_ast_variable_name(period_ast);
        size_t index = cxpr_model_program_param_index(program, name);
        if (index == (size_t)-1 ||
            !program->constants[index].ast ||
            !cxpr_eval_constant_double(program->constants[index].ast, &raw)) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "window period parameter requires a numeric default",
                                 0, 0);
            return false;
        }
    } else if (!cxpr_eval_constant_double(period_ast, &raw)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "window period must be a constant or model parameter default",
                             0, 0);
        return false;
    }
    if (!isfinite(raw) || raw < 1.0) raw = 1.0;
    period = lround(raw);
    if (period < 1) period = 1;
    *out_capacity = (size_t)period;
    return true;
}

static bool cxpr_model_c_period_ast_same(const cxpr_ast* left, const cxpr_ast* right) {
    if (!left || !right || cxpr_ast_type(left) != cxpr_ast_type(right)) return false;
    if (cxpr_ast_type(left) == CXPR_NODE_VARIABLE) {
        return cxpr_model_names_match(cxpr_ast_variable_name(left), cxpr_ast_variable_name(right));
    }
    if (cxpr_ast_type(left) == CXPR_NODE_NUMBER) {
        return fabs(cxpr_ast_number_value(left) - cxpr_ast_number_value(right)) < 1e-12;
    }
    return false;
}

static bool cxpr_model_c_ast_same_simple(const cxpr_ast* left, const cxpr_ast* right) {
    if (!left || !right || cxpr_ast_type(left) != cxpr_ast_type(right)) return false;
    switch (cxpr_ast_type(left)) {
    case CXPR_NODE_IDENTIFIER:
        return cxpr_model_names_match(cxpr_ast_identifier_name(left),
                                      cxpr_ast_identifier_name(right));
    case CXPR_NODE_VARIABLE:
        return cxpr_model_names_match(cxpr_ast_variable_name(left),
                                      cxpr_ast_variable_name(right));
    case CXPR_NODE_NUMBER:
        return fabs(cxpr_ast_number_value(left) - cxpr_ast_number_value(right)) < 1e-12;
    default:
        return false;
    }
}

static const char* cxpr_model_c_find_common_binding_expr(const cxpr_model_program* program,
                                                         size_t binding_index,
                                                         const bool* needed_bindings,
                                                         const bool* skip_bindings,
                                                         char* const* emitted_names) {
    const cxpr_ast* ast;
    if (!program || binding_index >= program->binding_count || !emitted_names) return NULL;
    if (program->bindings[binding_index].kind == CXPR_MODEL_BINDING_STATE_UPDATE) return NULL;
    ast = program->bindings[binding_index].ast;
    if (!ast) return NULL;
    (void)skip_bindings;
    for (size_t i = 0u; i < binding_index; ++i) {
        if (!needed_bindings[i] || !emitted_names[i]) continue;
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) continue;
        if (program->bindings[i].ast &&
            cxpr_model_ast_equal(program->bindings[i].ast, ast)) {
            return emitted_names[i];
        }
    }
    return NULL;
}

static bool cxpr_model_c_ast_is_number(const cxpr_ast* ast, double value) {
    return ast &&
           cxpr_ast_type(ast) == CXPR_NODE_NUMBER &&
           fabs(cxpr_ast_number_value(ast) - value) < 1e-12;
}

static bool cxpr_model_c_match_high_low_midpoint(const cxpr_ast* ast,
                                                 const cxpr_ast** out_high_ast,
                                                 const cxpr_ast** out_low_ast,
                                                 const cxpr_ast** out_period_ast) {
    const cxpr_ast* left;
    const cxpr_ast* right;
    const cxpr_ast* high_call = NULL;
    const cxpr_ast* low_call = NULL;
    const cxpr_ast* high_period;
    const cxpr_ast* low_period;

    if (out_high_ast) *out_high_ast = NULL;
    if (out_low_ast) *out_low_ast = NULL;
    if (out_period_ast) *out_period_ast = NULL;
    if (!ast || cxpr_ast_type(ast) != CXPR_NODE_BINARY_OP ||
        cxpr_ast_operator(ast) != CXPR_TOK_PLUS) {
        return false;
    }
    left = cxpr_ast_left(ast);
    right = cxpr_ast_right(ast);
    if (left && cxpr_ast_type(left) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_names_match(cxpr_ast_function_name(left), "window_highest")) {
        high_call = left;
    } else if (left && cxpr_ast_type(left) == CXPR_NODE_FUNCTION_CALL &&
               cxpr_model_names_match(cxpr_ast_function_name(left), "window_lowest")) {
        low_call = left;
    }
    if (right && cxpr_ast_type(right) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_names_match(cxpr_ast_function_name(right), "window_highest")) {
        high_call = right;
    } else if (right && cxpr_ast_type(right) == CXPR_NODE_FUNCTION_CALL &&
               cxpr_model_names_match(cxpr_ast_function_name(right), "window_lowest")) {
        low_call = right;
    }
    if (!high_call || !low_call ||
        cxpr_ast_function_argc(high_call) != 2u ||
        cxpr_ast_function_argc(low_call) != 2u) {
        return false;
    }
    high_period = cxpr_ast_function_arg(high_call, 1u);
    low_period = cxpr_ast_function_arg(low_call, 1u);
    if (!cxpr_model_c_period_ast_same(high_period, low_period)) return false;
    if (out_high_ast) *out_high_ast = cxpr_ast_function_arg(high_call, 0u);
    if (out_low_ast) *out_low_ast = cxpr_ast_function_arg(low_call, 0u);
    if (out_period_ast) *out_period_ast = high_period;
    return true;
}

static bool cxpr_model_c_match_scaled_high_low_midpoint(const cxpr_ast* ast,
                                                        const cxpr_ast** out_high_ast,
                                                        const cxpr_ast** out_low_ast,
                                                        const cxpr_ast** out_period_ast) {
    const cxpr_ast* sum = NULL;
    if (out_high_ast) *out_high_ast = NULL;
    if (out_low_ast) *out_low_ast = NULL;
    if (out_period_ast) *out_period_ast = NULL;
    if (!ast || cxpr_ast_type(ast) != CXPR_NODE_BINARY_OP ||
        cxpr_ast_operator(ast) != CXPR_TOK_STAR) {
        return false;
    }
    if (cxpr_model_c_ast_is_number(cxpr_ast_left(ast), 0.5)) {
        sum = cxpr_ast_right(ast);
    } else if (cxpr_model_c_ast_is_number(cxpr_ast_right(ast), 0.5)) {
        sum = cxpr_ast_left(ast);
    }
    return sum && cxpr_model_c_match_high_low_midpoint(
                      sum, out_high_ast, out_low_ast, out_period_ast);
}

static bool cxpr_model_c_emit_midpoint_binding(cxpr_model_c_buf* b,
                                               const char* name,
                                               const cxpr_ast* high_ast,
                                               const cxpr_ast* low_ast,
                                               const cxpr_ast* period_ast,
                                               const cxpr_c_target* target,
                                               const cxpr_model_program* program,
                                               cxpr_error* err) {
    size_t capacity = 0u;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;

    if (!b || !name || !high_ast || !low_ast || !period_ast || !target || !program) return false;
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return false;
    period_expr = cxpr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return false;
    {
        cxpr_model_c_buf pb = {0};
        cxpr_model_c_printf(
            &pb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            capacity,
            period_expr);
        if (pb.oom) {
            free(period_expr);
            free(pb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        period_limit_expr = pb.data;
    }

    cxpr_model_c_printf(
        b,
        "    double %s; { const size_t _cx_limit = (size_t)(%s); double _cx_highest = 0.0; double _cx_lowest = 0.0; size_t _cx_count = 0u;\n",
        name,
        period_limit_expr);
    {
        cxpr_model_c_buf loop = {0};
        cxpr_model_c_puts(&loop, "        for (size_t _cx_i = 0u; _cx_i < _cx_limit; ++_cx_i) {\n");
        if (cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_hi", high_ast, "_cx_i", target, program, err) &&
            cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_lo", low_ast, "_cx_i", target, program, err)) {
            cxpr_model_c_puts(
                &loop,
                "            if (!isnan(_cx_hi) && !isnan(_cx_lo)) { if (_cx_count == 0u) { _cx_highest = _cx_hi; _cx_lowest = _cx_lo; } if (_cx_hi > _cx_highest) _cx_highest = _cx_hi; if (_cx_lo < _cx_lowest) _cx_lowest = _cx_lo; _cx_count++; }\n"
                "        }\n");
            if (loop.oom) {
                free(period_expr);
                free(period_limit_expr);
                free(loop.data);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_puts(b, loop.data);
            free(loop.data);
            cxpr_model_c_printf(
                b,
                "        %s = _cx_count == 0u ? 0.0 : (_cx_highest + _cx_lowest) * 0.5; }\n",
                name);
            free(period_expr);
            free(period_limit_expr);
            if (b->oom) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            return true;
        }
        if (err && err->code != CXPR_OK) {
            free(period_expr);
            free(period_limit_expr);
            free(loop.data);
            return false;
        }
        free(loop.data);
    }
    for (size_t i = 0u; i < capacity; ++i) {
        char* high_expr = cxpr_ast_to_c_at_offset(high_ast, (unsigned)i, target, err);
        char* low_expr = high_expr
                             ? cxpr_ast_to_c_at_offset(low_ast, (unsigned)i, target, err)
                             : NULL;
        if (!high_expr || !low_expr) {
            free(high_expr);
            free(low_expr);
            free(period_expr);
            free(period_limit_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "        if (%zuu < _cx_limit) { double _cx_hi = %s; double _cx_lo = %s; if (!isnan(_cx_hi) && !isnan(_cx_lo)) { if (_cx_count == 0u) { _cx_highest = _cx_hi; _cx_lowest = _cx_lo; } if (_cx_hi > _cx_highest) _cx_highest = _cx_hi; if (_cx_lo < _cx_lowest) _cx_lowest = _cx_lo; _cx_count++; } }\n",
            i,
            high_expr,
            low_expr);
        free(high_expr);
        free(low_expr);
    }
    cxpr_model_c_printf(
        b,
        "        %s = _cx_count == 0u ? 0.0 : (_cx_highest + _cx_lowest) * 0.5; }\n",
        name);
    free(period_expr);
    free(period_limit_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static int cxpr_model_c_window_op_code(const char* name) {
    if (cxpr_model_names_match(name, "window_sum")) return 0;
    if (cxpr_model_names_match(name, "window_mean")) return 1;
    if (cxpr_model_names_match(name, "window_highest")) return 2;
    if (cxpr_model_names_match(name, "window_lowest")) return 3;
    if (cxpr_model_names_match(name, "window_stddev")) return 4;
    return -1;
}

static bool cxpr_model_c_emit_simple_window_binding(cxpr_model_c_buf* b,
                                                    const char* name,
                                                    const cxpr_ast* ast,
                                                    const cxpr_c_target* target,
                                                    const cxpr_model_program* program,
                                                    cxpr_error* err) {
    const char* fn_name;
    const cxpr_ast* value_ast;
    const cxpr_ast* period_ast;
    int op;
    size_t capacity = 0u;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;

    if (!b || !name || !ast || cxpr_ast_type(ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_ast_function_argc(ast) != 2u || !target || !program) {
        return false;
    }
    fn_name = cxpr_ast_function_name(ast);
    op = cxpr_model_c_window_op_code(fn_name);
    if (op < 0) return false;
    if (op != 1 && op != 4) return false;
    value_ast = cxpr_ast_function_arg(ast, 0u);
    if (cxpr_ast_type(value_ast) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_window_is_function(cxpr_ast_function_name(value_ast))) {
        return false;
    }
    period_ast = cxpr_ast_function_arg(ast, 1u);
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return false;
    period_expr = cxpr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return false;
    {
        cxpr_model_c_buf pb = {0};
        cxpr_model_c_printf(
            &pb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            capacity,
            period_expr);
        if (pb.oom) {
            free(period_expr);
            free(pb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        period_limit_expr = pb.data;
    }

    cxpr_model_c_printf(
        b,
        "    double %s; { const size_t _cx_limit = (size_t)(%s); double _cx_sum = 0.0; double _cx_sumsq = 0.0; double _cx_extreme = 0.0; size_t _cx_count = 0u;\n",
        name,
        period_limit_expr);
    {
        cxpr_model_c_buf loop = {0};
        cxpr_model_c_puts(&loop, "        for (size_t _cx_i = 0u; _cx_i < _cx_limit; ++_cx_i) {\n");
        if (cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_x", value_ast, "_cx_i", target, program, err)) {
            cxpr_model_c_printf(
                &loop,
                "            if (!isnan(_cx_x)) { if (_cx_count == 0u) _cx_extreme = _cx_x; if (%d == 2 && _cx_x > _cx_extreme) _cx_extreme = _cx_x; if (%d == 3 && _cx_x < _cx_extreme) _cx_extreme = _cx_x; _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_count++; }\n"
                "        }\n",
                op,
                op);
            if (loop.oom) {
                free(period_expr);
                free(period_limit_expr);
                free(loop.data);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_puts(b, loop.data);
            free(loop.data);
            if (op == 2 || op == 3) {
                cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_extreme; }\n", name);
            } else if (op == 1) {
                cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum / (double)_cx_count; }\n", name);
            } else if (op == 4) {
                cxpr_model_c_printf(
                    b,
                    "        if (_cx_count == 0u) %s = 0.0; else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
                    name,
                    name);
            } else {
                cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum; }\n", name);
            }
            free(period_expr);
            free(period_limit_expr);
            if (b->oom) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            return true;
        }
        if (err && err->code != CXPR_OK) {
            free(period_expr);
            free(period_limit_expr);
            free(loop.data);
            return false;
        }
        free(loop.data);
    }
    for (size_t i = 0u; i < capacity; ++i) {
        char* value_expr = cxpr_ast_to_c_at_offset(value_ast, (unsigned)i, target, err);
        if (!value_expr) {
            free(period_expr);
            free(period_limit_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "        if (%zuu < _cx_limit) { double _cx_x = %s; if (!isnan(_cx_x)) { if (_cx_count == 0u) _cx_extreme = _cx_x; if (%d == 2 && _cx_x > _cx_extreme) _cx_extreme = _cx_x; if (%d == 3 && _cx_x < _cx_extreme) _cx_extreme = _cx_x; _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_count++; } }\n",
            i,
            value_expr,
            op,
            op);
        free(value_expr);
    }
    if (op == 2 || op == 3) {
        cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_extreme; }\n", name);
    } else if (op == 1) {
        cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum / (double)_cx_count; }\n", name);
    } else if (op == 4) {
        cxpr_model_c_printf(
            b,
            "        if (_cx_count == 0u) %s = 0.0; else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
            name,
            name);
    } else {
        cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum; }\n", name);
    }
    free(period_expr);
    free(period_limit_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static bool cxpr_model_c_match_mean_stddev_pair(const cxpr_ast* mean_ast,
                                                const cxpr_ast* stddev_ast) {
    if (!mean_ast || !stddev_ast ||
        cxpr_ast_type(mean_ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_ast_type(stddev_ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_ast_function_argc(mean_ast) != 2u ||
        cxpr_ast_function_argc(stddev_ast) != 2u ||
        !cxpr_model_names_match(cxpr_ast_function_name(mean_ast), "window_mean") ||
        !cxpr_model_names_match(cxpr_ast_function_name(stddev_ast), "window_stddev")) {
        return false;
    }
    return cxpr_model_c_ast_same_simple(cxpr_ast_function_arg(mean_ast, 0u),
                                        cxpr_ast_function_arg(stddev_ast, 0u)) &&
           cxpr_model_c_period_ast_same(cxpr_ast_function_arg(mean_ast, 1u),
                                        cxpr_ast_function_arg(stddev_ast, 1u));
}

static bool cxpr_model_c_emit_mean_stddev_bindings(cxpr_model_c_buf* b,
                                                   const char* mean_name,
                                                   const char* stddev_name,
                                                   const cxpr_ast* mean_ast,
                                                   const cxpr_c_target* target,
                                                   const cxpr_model_program* program,
                                                   cxpr_error* err) {
    const cxpr_ast* value_ast;
    const cxpr_ast* period_ast;
    size_t capacity = 0u;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;

    if (!b || !mean_name || !stddev_name || !mean_ast ||
        cxpr_ast_type(mean_ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_ast_function_argc(mean_ast) != 2u || !target || !program) {
        return false;
    }
    value_ast = cxpr_ast_function_arg(mean_ast, 0u);
    period_ast = cxpr_ast_function_arg(mean_ast, 1u);
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return false;
    period_expr = cxpr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return false;
    {
        cxpr_model_c_buf pb = {0};
        cxpr_model_c_printf(
            &pb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            capacity,
            period_expr);
        if (pb.oom) {
            free(period_expr);
            free(pb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        period_limit_expr = pb.data;
    }

    cxpr_model_c_printf(
        b,
        "    double %s; double %s; { const size_t _cx_limit = (size_t)(%s); double _cx_sum = 0.0; double _cx_sumsq = 0.0; size_t _cx_count = 0u;\n",
        mean_name,
        stddev_name,
        period_limit_expr);
    {
        cxpr_model_c_buf loop = {0};
        cxpr_model_c_puts(&loop, "        for (size_t _cx_i = 0u; _cx_i < _cx_limit; ++_cx_i) {\n");
        if (cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_x", value_ast, "_cx_i", target, program, err)) {
            cxpr_model_c_puts(
                &loop,
                "            if (!isnan(_cx_x)) { _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_count++; }\n"
                "        }\n");
            if (loop.oom) {
                free(period_expr);
                free(period_limit_expr);
                free(loop.data);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_puts(b, loop.data);
            free(loop.data);
            cxpr_model_c_printf(
                b,
                "        if (_cx_count == 0u) { %s = 0.0; %s = 0.0; } else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
                mean_name,
                stddev_name,
                mean_name,
                stddev_name);
            free(period_expr);
            free(period_limit_expr);
            if (b->oom) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            return true;
        }
        if (err && err->code != CXPR_OK) {
            free(period_expr);
            free(period_limit_expr);
            free(loop.data);
            return false;
        }
        free(loop.data);
    }
    for (size_t i = 0u; i < capacity; ++i) {
        char* value_expr = cxpr_ast_to_c_at_offset(value_ast, (unsigned)i, target, err);
        if (!value_expr) {
            free(period_expr);
            free(period_limit_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "        if (%zuu < _cx_limit) { double _cx_x = %s; if (!isnan(_cx_x)) { _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_count++; } }\n",
            i,
            value_expr);
        free(value_expr);
    }
    cxpr_model_c_printf(
        b,
        "        if (_cx_count == 0u) { %s = 0.0; %s = 0.0; } else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
        mean_name,
        stddev_name,
        mean_name,
        stddev_name);
    free(period_expr);
    free(period_limit_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static size_t cxpr_model_c_standard_slot_count_inline(const cxpr_model_program* program) {
    (void)program;
    return 0u;
}

static size_t cxpr_model_c_window_plan_base(const cxpr_model_program* program,
                                            const cxpr_model_window_plan_node* node) {
    if (!program || !node || node->slot_count == 0u) return (size_t)-1;
    return cxpr_model_c_standard_slot_count_inline(program) + node->slot_offset;
}

static const char* cxpr_model_c_window_counter_type(
    const cxpr_model_window_plan_node* node) {
    if (!node || node->period_capacity > 255u) return "size_t";
    return "uint8_t";
}

static const char* cxpr_model_c_history_counter_type(size_t capacity) {
    return capacity > 255u ? "size_t" : "uint8_t";
}

static bool cxpr_model_c_emit_runtime_state_typedef(
    cxpr_model_c_buf* b,
    const cxpr_model_program* program,
    const cxpr_model_window_plan* window_plan,
    const char* safe_name,
    cxpr_error* err) {
    if (!b || !program || !safe_name) return false;
    cxpr_model_c_printf(b, "typedef struct %s_state {\n", safe_name);
    cxpr_model_c_puts(b, "    uint8_t init;\n");
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        char* field_name = cxpr_model_c_prefixed_name("state_", program->state_defaults[i].name);
        char* pending_name = cxpr_model_c_prefixed_name("pending_", program->state_defaults[i].name);
        char* has_pending_name = cxpr_model_c_prefixed_name(
            "has_pending_", program->state_defaults[i].name);
        if (!field_name || !pending_name || !has_pending_name) {
            free(field_name);
            free(pending_name);
            free(has_pending_name);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        cxpr_model_c_printf(b, "    double %s;\n", field_name);
        cxpr_model_c_printf(b, "    double %s;\n", pending_name);
        cxpr_model_c_printf(b, "    uint8_t %s;\n", has_pending_name);
        free(field_name);
        free(pending_name);
        free(has_pending_name);
    }
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t capacity;
        if (program->history_specs[i].depth == 0u) continue;
        capacity = cxpr_model_c_history_capacity(program->history_specs[i].depth);
        cxpr_model_c_printf(b, "    cxpr_history%zu history_%zu;\n", capacity, i);
    }
    if (window_plan) {
        for (size_t i = 0u; i < window_plan->node_count; ++i) {
            const cxpr_model_window_plan_node* node = &window_plan->nodes[i];
            if (node->slot_count < 4u) continue;
            cxpr_model_c_printf(b, "    cxpr_window%zu window_%zu;\n",
                                node->slot_count - 4u, i);
        }
    }
    for (size_t i = 0u; i < program->child_count; ++i) {
        char* child_tick_name = cxpr_model_c_child_tick_name(safe_name, i);
        if (!child_tick_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        cxpr_model_c_printf(b, "    %s_state child_%zu_state;\n", child_tick_name, i);
        cxpr_model_c_printf(b, "    uint8_t child_%zu_initialized;\n", i);
        cxpr_model_c_printf(b, "    double child_%zu_outputs[%zu];\n",
                            i,
                            program->children[i].program && program->children[i].program->output_count
                                ? program->children[i].program->output_count
                                : 1u);
        free(child_tick_name);
    }
    cxpr_model_c_printf(b, "} %s_state;\n\n", safe_name);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static bool cxpr_model_c_emit_state_typedefs(cxpr_model_c_buf* b,
                                             const cxpr_model_program* program,
                                             const cxpr_model_window_plan* window_plan,
                                             const char* safe_name,
                                             cxpr_error* err) {
    if (!b || !program || !safe_name) return false;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t depth = program->history_specs[i].depth;
        size_t capacity = cxpr_model_c_history_capacity(depth);
        const char* counter_type = cxpr_model_c_history_counter_type(capacity);
        bool already_emitted = false;
        if (depth == 0u) continue;
        for (size_t j = 0u; j < i; ++j) {
            if (program->history_specs[j].depth > 0u &&
                cxpr_model_c_history_capacity(program->history_specs[j].depth) == capacity) {
                already_emitted = true;
                break;
            }
        }
        if (already_emitted) continue;
        cxpr_model_c_printf(
            b,
            "#ifndef CXPR_HISTORY%zu_DEFINED\n"
            "#define CXPR_HISTORY%zu_DEFINED\n"
            "typedef struct { %s next; double values[%zu]; } cxpr_history%zu;\n"
            "#endif\n",
            capacity,
            capacity,
            counter_type,
            capacity,
            capacity);
    }
    if (window_plan) {
        for (size_t i = 0u; i < window_plan->node_count; ++i) {
            const cxpr_model_window_plan_node* node = &window_plan->nodes[i];
            const char* counter_type = cxpr_model_c_window_counter_type(node);
            size_t capacity = node->slot_count >= 4u ? node->slot_count - 4u : 0u;
            bool already_emitted = false;
            if (node->slot_count < 4u) continue;
            for (size_t j = 0u; j < i; ++j) {
                const cxpr_model_window_plan_node* prior = &window_plan->nodes[j];
                if (prior->slot_count >= 4u && prior->slot_count - 4u == capacity) {
                    already_emitted = true;
                    break;
                }
            }
            if (already_emitted) continue;
            cxpr_model_c_printf(
                b,
                "#ifndef CXPR_WINDOW%zu_DEFINED\n"
                "#define CXPR_WINDOW%zu_DEFINED\n"
                "typedef struct { uint8_t init; %s next; %s count; double sum; double values[%zu]; } cxpr_window%zu;\n"
                "#endif\n",
                capacity,
                capacity,
                counter_type,
                counter_type,
                capacity,
                capacity);
        }
    }
    if ((program->history_spec_count > 0u || (window_plan && window_plan->node_count > 0u))) {
        cxpr_model_c_puts(b, "\n");
    }
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static bool cxpr_model_c_init_sentinel_slot(const cxpr_model_program* program,
                                            const cxpr_model_window_plan* window_plan,
                                            size_t* out_slot) {
    (void)out_slot;
    if (!program) return false;
    if (program->state_default_count > 0u) return true;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        if (program->history_specs[i].depth > 0u) return true;
    }
    return window_plan && window_plan->node_count > 0u;
}

static bool cxpr_model_c_emit_slot_init_function(cxpr_model_c_buf* b,
                                                 const cxpr_model_program* program,
                                                 const cxpr_model_window_plan* window_plan,
                                                 const char* qualifiers,
                                                 const char* safe_name,
                                                 cxpr_error* err) {
    size_t sentinel = 0u;
    if (!b || !program || !safe_name) return false;
    if (!cxpr_model_c_init_sentinel_slot(program, window_plan, &sentinel)) return true;
    (void)sentinel;
    cxpr_model_c_printf(b, "/* Source model slot init: %s */\n", safe_name);
    if (qualifiers && qualifiers[0]) cxpr_model_c_printf(b, "%s ", qualifiers);
    cxpr_model_c_printf(
        b,
        "void %s_init_state(%s_state* restrict _cx_state) {\n",
        safe_name,
        safe_name);
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t depth = program->history_specs[i].depth;
        size_t capacity = cxpr_model_c_history_capacity(depth);
        if (depth == 0u) continue;
        cxpr_model_c_printf(
            b,
            "    for (size_t _cx_init_i = 0u; _cx_init_i < %zuu; ++_cx_init_i) _cx_state->history_%zu.values[_cx_init_i] = NAN;\n"
            "    _cx_state->history_%zu.next = 0u;\n",
            capacity,
            i,
            i);
    }
    if (window_plan) {
        for (size_t i = 0u; i < window_plan->node_count; ++i) {
            const cxpr_model_window_plan_node* node = &window_plan->nodes[i];
            size_t base = cxpr_model_c_window_plan_base(program, node);
            if (base == (size_t)-1 || node->slot_count < 4u) continue;
            (void)base;
            cxpr_model_c_printf(
                b,
                "    for (size_t _cx_init_i = 0u; _cx_init_i < %zuu; ++_cx_init_i) _cx_state->window_%zu.values[_cx_init_i] = NAN;\n"
                "    _cx_state->window_%zu.next = 0u;\n"
                "    _cx_state->window_%zu.count = 0u;\n"
                "    _cx_state->window_%zu.sum = 0.0;\n"
                "    _cx_state->window_%zu.init = 1u;\n",
                node->slot_count - 4u,
                i,
                i,
                i,
                i,
                i);
        }
    }
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        char* has_pending_name = cxpr_model_c_prefixed_name(
            "has_pending_", program->state_defaults[i].name);
        if (!has_pending_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        cxpr_model_c_printf(b, "    _cx_state->%s = 0u;\n", has_pending_name);
        free(has_pending_name);
    }
    cxpr_model_c_puts(b, "    _cx_state->init = 1u;\n");
    cxpr_model_c_puts(b, "}\n\n");
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static bool cxpr_model_c_period_default_value(const cxpr_model_program* program,
                                              const cxpr_ast* period_ast,
                                              double* out_value) {
    if (!period_ast || !out_value) return false;
    if (cxpr_ast_type(period_ast) == CXPR_NODE_VARIABLE) {
        const char* name = cxpr_ast_variable_name(period_ast);
        size_t index = cxpr_model_program_param_index(program, name);
        if (index == (size_t)-1 ||
            !program->constants[index].ast ||
            !cxpr_eval_constant_double(program->constants[index].ast, out_value)) {
            return false;
        }
        return true;
    }
    return cxpr_eval_constant_double(period_ast, out_value);
}

static char* cxpr_model_c_period_limit_expr(const cxpr_model_program* program,
                                            const cxpr_ast* period_ast,
                                            size_t capacity,
                                            const cxpr_c_target* target,
                                            cxpr_error* err) {
    double default_value = 0.0;
    long rounded;
    char default_raw[64];
    char* period_expr;
    cxpr_model_c_buf b = {0};

    if (!program || !period_ast || !target || capacity == 0u) return NULL;
    if (cxpr_model_c_period_default_value(program, period_ast, &default_value) &&
        isfinite(default_value)) {
        rounded = lround(default_value < 1.0 ? 1.0 : default_value);
        if (rounded < 1) rounded = 1;
        if ((size_t)rounded == capacity) {
            if (cxpr_ast_type(period_ast) != CXPR_NODE_VARIABLE) {
                cxpr_model_c_printf(&b, "%zuu", capacity);
                return b.oom ? NULL : b.data;
            }
            cxpr_model_c_format_double(default_raw, sizeof(default_raw), default_value);
            period_expr = cxpr_ast_to_c_at_offset(period_ast, 0u, target, err);
            if (!period_expr) return NULL;
            cxpr_model_c_printf(
                &b,
                "((%s) == %s ? %zuu : (size_t)((int)fmax(1.0, fmin((double)%zuu, round(%s)))))",
                period_expr,
                default_raw,
                capacity,
                capacity,
                period_expr);
            free(period_expr);
            return b.oom ? NULL : b.data;
        }
    }

    period_expr = cxpr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return NULL;
    cxpr_model_c_printf(
        &b,
        "(size_t)((int)fmax(1.0, fmin((double)%zuu, round(%s))))",
        capacity,
        period_expr);
    free(period_expr);
    return b.oom ? NULL : b.data;
}

static bool cxpr_model_c_emit_planned_roc_aggregate_binding(
    cxpr_model_c_buf* b,
    const char* name,
    const cxpr_model_window_plan* plan,
    const cxpr_model_window_plan_node* node,
    const cxpr_c_target* target,
    const cxpr_model_program* program,
    cxpr_error* err) {
    const cxpr_model_window_plan_node* roc_node;
    const cxpr_ast* value_ast;
    const cxpr_ast* roc_period_ast;
    const cxpr_ast* aggregate_period_ast;
    size_t roc_capacity;
    size_t aggregate_capacity;
    size_t extra_base;
    size_t node_index;
    char* roc_limit_expr = NULL;
    char* aggregate_limit_expr = NULL;

    if (!b || !name || !plan || !node || !target || !program ||
        !node->has_child ||
        (node->op != CXPR_MODEL_WINDOW_PLAN_OP_MEAN &&
         node->op != CXPR_MODEL_WINDOW_PLAN_OP_SUM) ||
        node->child_index >= plan->node_count) {
        return false;
    }
    roc_node = &plan->nodes[node->child_index];
    if (roc_node->op != CXPR_MODEL_WINDOW_PLAN_OP_ROC) return false;
    value_ast = roc_node->value_ast;
    roc_period_ast = roc_node->period_ast;
    aggregate_period_ast = node->period_ast;
    roc_capacity = roc_node->period_capacity;
    aggregate_capacity = node->period_capacity;
    extra_base = cxpr_model_c_window_plan_base(program, node);
    if (extra_base == (size_t)-1 || aggregate_capacity == 0u) return false;
    node_index = (size_t)(node - plan->nodes);

    roc_limit_expr = cxpr_model_c_period_limit_expr(
        program, roc_period_ast, roc_capacity, target, err);
    aggregate_limit_expr = roc_limit_expr
                               ? cxpr_model_c_period_limit_expr(
                                     program, aggregate_period_ast, aggregate_capacity, target, err)
                               : NULL;
    if (!roc_limit_expr || !aggregate_limit_expr) {
        free(roc_limit_expr);
        free(aggregate_limit_expr);
        return false;
    }

    {
        char* now_expr = cxpr_ast_to_c_at_offset(value_ast, 0u, target, err);
        char* prev_expr = now_expr
                              ? cxpr_ast_to_c_at_offset(
                                    value_ast, (unsigned)roc_capacity, target, err)
                              : NULL;
        if (!now_expr || !prev_expr) {
            free(now_expr);
            free(prev_expr);
            free(roc_limit_expr);
            free(aggregate_limit_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "    double %s; { const size_t _cx_rp = (size_t)(%s); const size_t _cx_ap = (size_t)(%s);\n",
            name,
            roc_limit_expr,
            aggregate_limit_expr);
        if (!cxpr_model_c_emit_planned_roc_rolling_update(
                b,
                name,
                node,
                node_index,
                roc_capacity,
                aggregate_capacity,
                cxpr_model_c_window_counter_type(node),
                now_expr,
                prev_expr,
                err)) {
            free(now_expr);
            free(prev_expr);
            free(roc_limit_expr);
            free(aggregate_limit_expr);
            return false;
        }
        free(now_expr);
        free(prev_expr);
    }

    if (!cxpr_model_c_emit_planned_roc_aggregate_fallback(
            b, name, value_ast, node->op, target, program, err)) {
        free(roc_limit_expr);
        free(aggregate_limit_expr);
        return false;
    }
    cxpr_model_c_puts(b, "        } } }\n");
    free(roc_limit_expr);
    free(aggregate_limit_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static bool cxpr_model_c_emit_planned_simple_aggregate_binding(
    cxpr_model_c_buf* b,
    const char* name,
    const cxpr_model_window_plan* plan,
    const cxpr_model_window_plan_node* node,
    const cxpr_c_target* target,
    const cxpr_model_program* program,
    cxpr_error* err) {
    const cxpr_ast* value_ast;
    const cxpr_ast* period_ast;
    size_t capacity;
    size_t node_index;
    char* period_limit_expr = NULL;
    char* value_expr = NULL;
    cxpr_model_c_buf fallback = {0};

    if (!b || !name || !plan || !node || !target || !program ||
        node->has_child ||
        (node->op != CXPR_MODEL_WINDOW_PLAN_OP_MEAN &&
         node->op != CXPR_MODEL_WINDOW_PLAN_OP_SUM) ||
        node->period_capacity == 0u) {
        return false;
    }
    value_ast = node->value_ast;
    period_ast = node->period_ast;
    capacity = node->period_capacity;
    node_index = (size_t)(node - plan->nodes);

    period_limit_expr = cxpr_model_c_period_limit_expr(
        program, period_ast, capacity, target, err);
    value_expr = period_limit_expr ? cxpr_ast_to_c_at_offset(value_ast, 0u, target, err) : NULL;
    if (!period_limit_expr || !value_expr) {
        free(period_limit_expr);
        free(value_expr);
        return false;
    }

    cxpr_model_c_puts(
        &fallback,
        "            { double _cx_fallback_sum = 0.0; size_t _cx_fallback_count = 0u;\n"
        "        for (size_t _cx_i = 0u; _cx_i < _cx_limit; ++_cx_i) {\n");
    if (!cxpr_model_c_emit_dynamic_history_value(
            &fallback, "_cx_x", value_ast, "_cx_i", target, program, err)) {
        free(fallback.data);
        free(period_limit_expr);
        free(value_expr);
        return false;
    }
    cxpr_model_c_puts(
        &fallback,
        "            if (!isnan(_cx_x)) { _cx_fallback_sum += _cx_x; _cx_fallback_count++; }\n"
        "        }\n");
    cxpr_model_c_printf(
        &fallback,
        "        %s = _cx_fallback_count == 0u ? 0.0 : %s; }\n",
        name,
        node->op == CXPR_MODEL_WINDOW_PLAN_OP_MEAN
            ? "_cx_fallback_sum / (double)_cx_fallback_count"
            : "_cx_fallback_sum");
    if (fallback.oom) {
        free(fallback.data);
        free(period_limit_expr);
        free(value_expr);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }

    cxpr_model_c_printf(
        b,
        "    double %s; { const size_t _cx_limit = (size_t)(%s);\n"
        "        { size_t _cx_next = (size_t)_cx_state->window_%zu.next; size_t _cx_count = (size_t)_cx_state->window_%zu.count; double _cx_sum = _cx_state->window_%zu.sum; double _cx_value = %s; double _cx_old = _cx_state->window_%zu.values[_cx_next]; if (!isnan(_cx_old)) { _cx_sum -= _cx_old; if (_cx_count > 0u) _cx_count--; } if (!isnan(_cx_value)) { _cx_sum += _cx_value; _cx_count++; } _cx_state->window_%zu.values[_cx_next] = _cx_value; _cx_state->window_%zu.next = (%s)((_cx_next + 1u) %% %zuu); _cx_state->window_%zu.count = (%s)_cx_count; _cx_state->window_%zu.sum = _cx_sum; if (!CXPR_UNLIKELY(_cx_limit != %zuu)) { %s = _cx_count == 0u ? 0.0 : %s; } else {\n",
        name,
        period_limit_expr,
        node_index,
        node_index,
        node_index,
        value_expr,
        node_index,
        node_index,
        node_index,
        cxpr_model_c_window_counter_type(node),
        capacity,
        node_index,
        cxpr_model_c_window_counter_type(node),
        node_index,
        capacity,
        name,
        node->op == CXPR_MODEL_WINDOW_PLAN_OP_MEAN ? "_cx_sum / (double)_cx_count" : "_cx_sum");
    cxpr_model_c_puts(b, fallback.data);
    cxpr_model_c_puts(b, "        } } }\n");
    free(fallback.data);
    free(period_limit_expr);
    free(value_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

typedef bool (*cxpr_model_c_single_binding_emitter)(cxpr_model_c_buf* b,
                                                    const char* name,
                                                    const cxpr_ast* ast,
                                                    const cxpr_c_target* target,
                                                    const cxpr_model_program* program,
                                                    cxpr_error* err);

static bool cxpr_model_c_emit_midpoint_binding_from_ast(cxpr_model_c_buf* b,
                                                        const char* name,
                                                        const cxpr_ast* ast,
                                                        const cxpr_c_target* target,
                                                        const cxpr_model_program* program,
                                                        cxpr_error* err) {
    const cxpr_ast* high_ast = NULL;
    const cxpr_ast* low_ast = NULL;
    const cxpr_ast* period_ast = NULL;
    if (!cxpr_model_c_match_scaled_high_low_midpoint(ast, &high_ast, &low_ast, &period_ast)) {
        return false;
    }
    return cxpr_model_c_emit_midpoint_binding(
        b, name, high_ast, low_ast, period_ast, target, program, err);
}

static bool cxpr_model_c_emit_optimized_single_binding(cxpr_model_c_buf* b,
                                                       const char* name,
                                                       const cxpr_ast* ast,
                                                       const cxpr_c_target* target,
                                                       const cxpr_model_program* program,
                                                       cxpr_error* err) {
    static const cxpr_model_c_single_binding_emitter emitters[] = {
        cxpr_model_c_emit_midpoint_binding_from_ast,
        cxpr_model_c_emit_simple_window_binding,
    };
    for (size_t i = 0u; i < sizeof(emitters) / sizeof(emitters[0]); ++i) {
        if (emitters[i](b, name, ast, target, program, err)) return true;
        if (err && err->code != CXPR_OK) return false;
    }
    return false;
}

static bool cxpr_model_c_emit_optimized_binding_pair(cxpr_model_c_buf* b,
                                                     const char* first_name,
                                                     const char* second_name,
                                                     const cxpr_ast* first_ast,
                                                     const cxpr_ast* second_ast,
                                                     const cxpr_c_target* target,
                                                     const cxpr_model_program* program,
                                                     cxpr_error* err) {
    if (!cxpr_model_c_match_mean_stddev_pair(first_ast, second_ast)) return false;
    return cxpr_model_c_emit_mean_stddev_bindings(
        b, first_name, second_name, first_ast, target, program, err);
}

static char* cxpr_model_ast_c_emit_window_call(const cxpr_ast* ast,
                                               unsigned lookback_offset,
                                               const cxpr_c_target* target,
                                               const cxpr_model_program* program,
                                               cxpr_error* err) {
    const char* name = cxpr_ast_function_name(ast);
    const char* op = cxpr_model_c_window_op(name);
    bool is_roc = cxpr_model_names_match(name, "window_roc");
    const cxpr_ast* value_ast;
    const cxpr_ast* period_ast;
    size_t capacity = 0u;
    size_t value_count;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;
    bool guard_values = false;
    cxpr_model_c_buf b = {0};
    if (!program || (!op && !is_roc) || cxpr_ast_function_argc(ast) != 2u) {
        cxpr_model_set_error(err, CXPR_ERR_WRONG_ARITY,
                             "window function expects two arguments", 0, 0);
        return NULL;
    }
    value_ast = cxpr_ast_function_arg(ast, 0u);
    period_ast = cxpr_ast_function_arg(ast, 1u);
    guard_values = cxpr_ast_type(period_ast) == CXPR_NODE_VARIABLE;
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return NULL;
    period_expr = cxpr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return NULL;
    {
        cxpr_model_c_buf pb = {0};
        cxpr_model_c_printf(
            &pb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            capacity,
            period_expr);
        if (pb.oom) {
            free(period_expr);
            free(pb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        period_limit_expr = pb.data;
    }
    if (cxpr_model_names_match(name, "window_mean") &&
        cxpr_ast_type(value_ast) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_names_match(cxpr_ast_function_name(value_ast), "window_roc") &&
        cxpr_ast_function_argc(value_ast) == 2u) {
        const cxpr_ast* roc_value_ast = cxpr_ast_function_arg(value_ast, 0u);
        const cxpr_ast* roc_period_ast = cxpr_ast_function_arg(value_ast, 1u);
        size_t roc_capacity = 0u;
        size_t source_count;
        char* roc_period_expr;
        char* roc_limit_expr;
        cxpr_model_c_buf rb = {0};
        if (!cxpr_model_c_window_period_capacity(program, roc_period_ast, &roc_capacity, err)) {
            free(period_expr);
            free(period_limit_expr);
            return NULL;
        }
        roc_period_expr = cxpr_ast_to_c_at_offset(roc_period_ast, 0u, target, err);
        if (!roc_period_expr) {
            free(period_expr);
            free(period_limit_expr);
            return NULL;
        }
        cxpr_model_c_printf(
            &rb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            roc_capacity,
            roc_period_expr);
        if (rb.oom) {
            free(roc_period_expr);
            free(period_expr);
            free(period_limit_expr);
            free(rb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        roc_limit_expr = rb.data;
        source_count = capacity + roc_capacity;
        cxpr_model_c_puts(&b, "cxpr_model_window_mean_roc_c((const double[]){");
        for (size_t i = 0u; i < source_count; ++i) {
            char* source_expr;
            if (i > 0u) cxpr_model_c_puts(&b, ", ");
            source_expr = cxpr_ast_to_c_at_offset(
                roc_value_ast, lookback_offset + (unsigned)i, target, err);
            if (!source_expr) {
                free(roc_period_expr);
                free(roc_limit_expr);
                free(period_expr);
                free(period_limit_expr);
                free(b.data);
                return NULL;
            }
            cxpr_model_c_printf(&b, "(%s)", source_expr);
            free(source_expr);
        }
        cxpr_model_c_printf(
            &b,
            "}, %zuu, %s, %s)",
            source_count,
            roc_limit_expr,
            period_limit_expr);
        free(roc_period_expr);
        free(roc_limit_expr);
        free(period_expr);
        free(period_limit_expr);
        if (b.oom) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return b.data;
    }
    value_count = is_roc ? capacity + 1u : capacity;
    cxpr_model_c_puts(&b, is_roc
                          ? "cxpr_model_window_roc_c((const double[]){"
                          : "cxpr_model_window_eval_c((const double[]){");
    for (size_t i = 0u; i < value_count; ++i) {
        char* value_expr;
        if (i > 0u) cxpr_model_c_puts(&b, ", ");
        value_expr = (cxpr_ast_type(value_ast) == CXPR_NODE_FUNCTION_CALL &&
                      cxpr_model_window_is_function(cxpr_ast_function_name(value_ast)))
                         ? cxpr_model_ast_c_emit_window_call(
                               value_ast, lookback_offset + (unsigned)i, target, program, err)
                         : cxpr_ast_to_c_at_offset(
                               value_ast, lookback_offset + (unsigned)i, target, err);
        if (!value_expr) {
            free(period_expr);
            free(period_limit_expr);
            free(b.data);
            return NULL;
        }
        if (!guard_values) {
            cxpr_model_c_printf(&b, "(%s)", value_expr);
        } else if (is_roc) {
            cxpr_model_c_printf(&b,
                                "((%zuu == 0u || %zuu <= (size_t)(%s)) ? (%s) : NAN)",
                                i,
                                i,
                                period_limit_expr,
                                value_expr);
        } else {
            cxpr_model_c_printf(&b,
                                "((%zuu < (size_t)(%s)) ? (%s) : NAN)",
                                i,
                                period_limit_expr,
                                value_expr);
        }
        free(value_expr);
    }
    if (is_roc) {
        cxpr_model_c_printf(
            &b,
            "}, %zuu, %s)",
            value_count,
            period_limit_expr);
    } else {
        cxpr_model_c_printf(
            &b,
            "}, %zuu, %s, %s)",
            capacity,
            period_limit_expr,
            op);
    }
    free(period_expr);
    free(period_limit_expr);
    if (b.oom) {
        free(b.data);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    return b.data;
}

static char* cxpr_model_ast_c_emit_call(const cxpr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        bool* handled,
                                        cxpr_error* err) {
    const char* name = cxpr_ast_function_name(ast);
    size_t argc = cxpr_ast_function_argc(ast);
    cxpr_model_ast_c_target* target_data = (cxpr_model_ast_c_target*)userdata;
    const cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .userdata = userdata,
    };
    cxpr_model_c_buf b = {0};

    (void)target_data;
    if (handled) *handled = false;
    if (!name) return NULL;

    if (cxpr_model_window_is_function(name)) {
        if (handled) *handled = true;
        return cxpr_model_ast_c_emit_window_call(
            ast, lookback_offset, &target,
            target_data ? target_data->program : NULL,
            err);
    }

    if ((cxpr_model_names_match(name, "min") || cxpr_model_names_match(name, "max")) &&
        argc == 2u) {
        char* left;
        char* right;
        const char* op = cxpr_model_names_match(name, "min") ? "<" : ">";
        if (handled) *handled = true;
        left = cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, 0u),
                                       lookback_offset, &target, err);
        right = left ? cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, 1u),
                                               lookback_offset, &target, err) : NULL;
        if (!left || !right) {
            free(left);
            free(right);
            return NULL;
        }
        cxpr_model_c_printf(&b, "((%s %s %s) ? (%s) : (%s))",
                            left, op, right, left, right);
        free(left);
        free(right);
        if (b.oom) {
            free(b.data);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return NULL;
        }
        return b.data;
    }

    if (cxpr_model_names_match(name, "if") && argc == 3u) {
        char* cond;
        char* yes;
        char* no;
        if (handled) *handled = true;
        cond = cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, 0u),
                                       lookback_offset, &target, err);
        yes = cond ? cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, 1u),
                                             lookback_offset, &target, err) : NULL;
        no = yes ? cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, 2u),
                                           lookback_offset, &target, err) : NULL;
        if (!cond || !yes || !no) {
            free(cond);
            free(yes);
            free(no);
            return NULL;
        }
        cxpr_model_c_printf(&b, "((%s) ? (%s) : (%s))", cond, yes, no);
        free(cond);
        free(yes);
        free(no);
        if (b.oom) {
            free(b.data);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return NULL;
        }
        return b.data;
    }

    if (target_data && target_data->program && target_data->program->registry) {
        cxpr_func_entry* entry = cxpr_registry_find(target_data->program->registry, name);
        if (entry && entry->defined_body && entry->defined_return_field_count == 0u) {
            char* fn_name;
            if (entry->defined_param_count != argc) {
                if (err) {
                    err->code = CXPR_ERR_SYNTAX;
                    err->message = "Model function arity mismatch";
                }
                return NULL;
            }
            if (handled) *handled = true;
            fn_name = cxpr_model_c_scoped_function_name(target_data->function_prefix,
                                                        entry->name);
            if (!fn_name) {
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Out of memory";
                }
                return NULL;
            }
            cxpr_model_c_printf(&b, "%s(", fn_name);
            free(fn_name);
            for (size_t i = 0u; i < argc; ++i) {
                char* arg;
                if (i > 0u) cxpr_model_c_puts(&b, ", ");
                arg = cxpr_ast_to_c_at_offset(cxpr_ast_function_arg(ast, i),
                                              lookback_offset, &target, err);
                if (!arg) {
                    free(b.data);
                    return NULL;
                }
                cxpr_model_c_puts(&b, arg);
                free(arg);
            }
            cxpr_model_c_puts(&b, ")");
            if (b.oom) {
                free(b.data);
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Out of memory";
                }
                return NULL;
            }
            return b.data;
        }
    }

    if (handled) *handled = false;
    return NULL;
}

static char* cxpr_model_ast_expr_to_c(const cxpr_model_program* program,
                                      const cxpr_ast* ast,
                                      const char* function_prefix,
                                      const double* literal_param_values,
                                      size_t literal_param_count,
                                      cxpr_error* err) {
    cxpr_model_ast_c_target userdata = {
        .program = program,
        .function_prefix = function_prefix,
        .literal_param_values = literal_param_values,
        .literal_param_count = literal_param_count,
    };
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .userdata = &userdata,
    };
    return cxpr_ast_to_c(ast, &target, err);
}

static char* cxpr_model_ast_temp_make(cxpr_model_ast_temp_emit* emit,
                                      const char* expr,
                                      cxpr_error* err) {
    char name[64];
    if (!emit || !emit->declarations || !expr) return NULL;
    snprintf(name, sizeof(name), "_cx_t%zu", emit->next_temp++);
    cxpr_model_c_printf(emit->declarations, "    const double %s = %s;\n", name, expr);
    if (emit->declarations->oom) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return NULL;
    }
    return cxpr_strdup(name);
}

static char* cxpr_model_ast_binary_to_c_with_temps(cxpr_model_ast_temp_emit* emit,
                                                   const cxpr_ast* ast,
                                                   cxpr_error* err) {
    int op = cxpr_ast_operator(ast);
    const char* ops = NULL;
    char* left;
    char* right;
    cxpr_model_c_buf b = {0};

    switch (op) {
    case CXPR_TOK_PLUS: ops = "+"; break;
    case CXPR_TOK_MINUS: ops = "-"; break;
    case CXPR_TOK_STAR: ops = "*"; break;
    case CXPR_TOK_SLASH: ops = "/"; break;
    case CXPR_TOK_EQ: ops = "=="; break;
    case CXPR_TOK_NEQ: ops = "!="; break;
    case CXPR_TOK_LT: ops = "<"; break;
    case CXPR_TOK_GT: ops = ">"; break;
    case CXPR_TOK_LTE: ops = "<="; break;
    case CXPR_TOK_GTE: ops = ">="; break;
    case CXPR_TOK_AND: ops = "&&"; break;
    case CXPR_TOK_OR: ops = "||"; break;
    default:
        return cxpr_ast_to_c(ast, emit ? emit->target : NULL, err);
    }

    left = cxpr_model_ast_expr_to_c_with_temps(emit, cxpr_ast_left(ast), err);
    right = left ? cxpr_model_ast_expr_to_c_with_temps(emit, cxpr_ast_right(ast), err) : NULL;
    if (!left || !right) {
        free(left);
        free(right);
        return NULL;
    }
    cxpr_model_c_printf(&b, "(%s %s %s)", left, ops, right);
    free(left);
    free(right);
    if (b.oom) {
        free(b.data);
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return NULL;
    }
    return b.data;
}

static char* cxpr_model_ast_expr_to_c_with_temps(cxpr_model_ast_temp_emit* emit,
                                                 const cxpr_ast* ast,
                                                 cxpr_error* err) {
    if (!emit || !ast) return NULL;
    if (cxpr_ast_type(ast) == CXPR_NODE_FUNCTION_CALL) {
        const char* name = cxpr_ast_function_name(ast);
        const size_t argc = cxpr_ast_function_argc(ast);
        if ((cxpr_model_names_match(name, "min") || cxpr_model_names_match(name, "max")) &&
            argc == 2u) {
            const char* op = cxpr_model_names_match(name, "min") ? "<" : ">";
            char* left = cxpr_model_ast_expr_to_c_with_temps(
                emit, cxpr_ast_function_arg(ast, 0u), err);
            char* right = left ? cxpr_model_ast_expr_to_c_with_temps(
                emit, cxpr_ast_function_arg(ast, 1u), err) : NULL;
            char* left_temp;
            char* right_temp;
            char* out;
            cxpr_model_c_buf b = {0};
            if (!left || !right) {
                free(left);
                free(right);
                return NULL;
            }
            left_temp = cxpr_model_ast_temp_make(emit, left, err);
            right_temp = left_temp ? cxpr_model_ast_temp_make(emit, right, err) : NULL;
            free(left);
            free(right);
            if (!left_temp || !right_temp) {
                free(left_temp);
                free(right_temp);
                return NULL;
            }
            cxpr_model_c_printf(&b, "((%s %s %s) ? %s : %s)",
                                left_temp, op, right_temp, left_temp, right_temp);
            free(left_temp);
            free(right_temp);
            if (b.oom) {
                free(b.data);
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Out of memory";
                }
                return NULL;
            }
            out = cxpr_model_ast_temp_make(emit, b.data, err);
            free(b.data);
            return out;
        }
    }
    if (cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP) {
        return cxpr_model_ast_binary_to_c_with_temps(emit, ast, err);
    }
    return cxpr_ast_to_c(ast, emit->target, err);
}

static bool cxpr_model_c_defined_function_used(const cxpr_model_program* program,
                                               const char* name,
                                               bool* used,
                                               cxpr_error* err) {
    if (!program || !program->registry || !name || !used) return true;
    for (size_t i = 0u; i < program->registry->count; ++i) {
        cxpr_func_entry* entry = &program->registry->entries[i];
        if (cxpr_model_names_match(entry->name, name) &&
            entry->defined_body &&
            entry->defined_return_field_count == 0u) {
            if (used[i]) return true;
            used[i] = true;
            return cxpr_model_c_collect_defined_function_refs(
                program, entry->defined_body, used, err);
        }
    }
    return true;
}

static bool cxpr_model_c_collect_defined_function_refs(const cxpr_model_program* program,
                                                       const cxpr_ast* ast,
                                                       bool* used,
                                                       cxpr_error* err) {
    if (!ast) return true;
    switch (cxpr_ast_type(ast)) {
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_c_collect_defined_function_refs(program, cxpr_ast_left(ast),
                                                          used, err) &&
               cxpr_model_c_collect_defined_function_refs(program, cxpr_ast_right(ast),
                                                          used, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_c_collect_defined_function_refs(program, cxpr_ast_operand(ast),
                                                          used, err);
    case CXPR_NODE_FUNCTION_CALL: {
        const char* name = cxpr_ast_function_name(ast);
        size_t argc = cxpr_ast_function_argc(ast);
        if (!cxpr_model_c_defined_function_used(program, name, used, err)) return false;
        for (size_t i = 0u; i < argc; ++i) {
            if (!cxpr_model_c_collect_defined_function_refs(
                    program, cxpr_ast_function_arg(ast, i), used, err)) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_PRODUCER_ACCESS:
        {
            cxpr_func_entry* entry = program && program->registry
                ? cxpr_registry_find(program->registry, cxpr_ast_producer_name(ast))
                : NULL;
            if (entry && !entry->model_producer && entry->defined_return_field_count > 0u) {
                const char* field = cxpr_ast_producer_field(ast);
                for (size_t f = 0u; f < entry->defined_return_field_count; ++f) {
                    if (entry->defined_return_field_names[f] &&
                        field &&
                        strcmp(entry->defined_return_field_names[f], field) == 0) {
                        if (!cxpr_model_c_collect_defined_function_refs(
                                program, entry->defined_return_field_bodies[f], used, err)) {
                            return false;
                        }
                        break;
                    }
                }
            }
        }
        for (size_t i = 0u; i < cxpr_ast_producer_argc(ast); ++i) {
            if (!cxpr_model_c_collect_defined_function_refs(
                    program, cxpr_ast_producer_arg(ast, i), used, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_LOOKBACK:
        return cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_ast_lookback_target(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_ast_lookback_index(ast), used, err);
    case CXPR_NODE_TERNARY:
        return cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_ast_ternary_condition(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_ast_ternary_true_branch(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_ast_ternary_false_branch(ast), used, err);
    default:
        return true;
    }
}

static char* cxpr_model_ast_defined_fn_to_c(const cxpr_model_program* program,
                                            const cxpr_func_entry* entry,
                                            const char* function_prefix,
                                            cxpr_error* err) {
    cxpr_model_c_buf b = {0};
    char* fn_name;
    cxpr_model_ast_c_target userdata = {0};
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .userdata = &userdata,
    };
    if (!entry || !entry->defined_body) return NULL;
    userdata.program = program;
    userdata.param_names = entry->defined_param_names;
    userdata.param_exprs = NULL;
    userdata.param_count = entry->defined_param_count;
    userdata.inline_fn_name = entry->name;
    userdata.function_prefix = function_prefix;
    fn_name = cxpr_model_c_scoped_function_name(function_prefix, entry->name);
    if (!fn_name) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    if (function_prefix && function_prefix[0]) {
        cxpr_model_c_printf(&b, "/* Source function: %s (scope: %s) */\n",
                            entry->name ? entry->name : "(unnamed)",
                            function_prefix);
    } else {
        cxpr_model_c_printf(&b, "/* Source function: %s */\n",
                            entry->name ? entry->name : "(unnamed)");
    }
    cxpr_model_c_printf(&b, "static inline double %s(", fn_name);
    free(fn_name);
    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        char* param_name = cxpr_model_c_safe_name(entry->defined_param_names[i]);
        if (!param_name) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        if (i > 0u) cxpr_model_c_puts(&b, ", ");
        cxpr_model_c_printf(&b, "double %s", param_name);
        free(param_name);
    }
    if (entry->defined_param_count == 0u) cxpr_model_c_puts(&b, "void");
    cxpr_model_c_puts(&b, ") { return ");
    {
        cxpr_model_c_buf declarations = {0};
        cxpr_model_ast_temp_emit temp_emit = {
            .declarations = &declarations,
            .target = &target,
            .next_temp = 0u,
        };
        char* expr = cxpr_model_ast_expr_to_c_with_temps(&temp_emit,
                                                         entry->defined_body,
                                                         err);
        if (!expr) {
            free(declarations.data);
            free(b.data);
            return NULL;
        }
        if (declarations.data && declarations.len > 0u) {
            size_t prefix_len = b.len;
            cxpr_model_c_buf nb = {0};
            if (b.len >= strlen(" { return ") &&
                strcmp(b.data + b.len - strlen(" { return "), " { return ") == 0) {
                prefix_len = b.len - strlen("return ");
            }
            cxpr_model_c_reserve(&nb, prefix_len + declarations.len + strlen("    return ") + strlen(expr) + 16u);
            if (!nb.oom) {
                memcpy(nb.data, b.data, prefix_len);
                nb.len = prefix_len;
                nb.data[nb.len] = '\0';
                cxpr_model_c_puts(&nb, "\n");
                cxpr_model_c_puts(&nb, declarations.data);
                cxpr_model_c_puts(&nb, "    return ");
                free(b.data);
                b = nb;
            }
        }
        free(declarations.data);
        cxpr_model_c_puts(&b, expr);
        free(expr);
    }
    cxpr_model_c_puts(&b, "; }\n\n");
    if (b.oom) {
        free(b.data);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    return b.data;
}

static bool cxpr_model_c_emit_defined_functions_ast(const cxpr_model_program* program,
                                                    const char* function_prefix,
                                                    cxpr_model_c_buf* b,
                                                    cxpr_error* err) {
    bool* used = NULL;
    if (!program || !program->registry) return true;
    used = (bool*)calloc(program->registry->count ? program->registry->count : 1u,
                         sizeof(bool));
    if (!used && program->registry->count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < program->binding_count; ++i) {
        if (!cxpr_model_c_collect_defined_function_refs(
                program, program->bindings[i].ast, used, err)) {
            free(used);
            return false;
        }
    }
    for (size_t i = 0u; i < program->registry->count; ++i) {
        cxpr_func_entry* entry = &program->registry->entries[i];
        char* source;
        if (!used[i]) continue;
        if (!entry->defined_body || entry->defined_return_field_count > 0u) continue;
        source = cxpr_model_ast_defined_fn_to_c(program, entry, function_prefix, err);
        if (!source) {
            free(used);
            return false;
        }
        cxpr_model_c_puts(b, source);
        free(source);
        if (b->oom) {
            free(used);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    free(used);
    return true;
}

static bool cxpr_model_c_emit_child_model_helpers(const cxpr_model_program* program,
                                                  const char* function_prefix,
                                                  cxpr_model_c_buf* b,
                                                  cxpr_error* err) {
    if (!program || !function_prefix || !b) return true;
    for (size_t i = 0u; i < program->child_count; ++i) {
        const cxpr_model_program* child = program->children[i].program;
        char* tick_name;
        char* tick_source;
        if (!child) continue;
        tick_name = cxpr_model_c_child_tick_name(function_prefix, i);
        if (!tick_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        cxpr_model_c_printf(b, "/* Source model tick: %s */\n",
                            program->children[i].name ? program->children[i].name : "(unnamed)");
        tick_source = cxpr_model_program_to_c_tick_function_select_outputs(
            child, "static inline", tick_name, NULL, 0u, err);
        if (!tick_source) {
            free(tick_name);
            return false;
        }
        cxpr_model_c_puts(b, tick_source);
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

static void cxpr_model_c_emit_common_helpers(cxpr_model_c_buf* b) {
    cxpr_model_c_puts(b,
                      "#include <cxpr/model_runtime.h>\n\n"
                      "#include <stdint.h>\n\n"
                      "#ifndef CXPR_UNLIKELY\n"
                      "#if defined(__GNUC__) || defined(__clang__)\n"
                      "#define CXPR_UNLIKELY(x) __builtin_expect(!!(x), 0)\n"
                      "#else\n"
                      "#define CXPR_UNLIKELY(x) (x)\n"
                      "#endif\n"
                      "#endif\n\n");
}

bool cxpr_model_program_to_c_tick_function_ast(const cxpr_model_program* program,
                                               const char* qualifiers,
                                               const char* function_name,
                                               const double* literal_param_values,
                                               size_t literal_param_count,
                                               const size_t* output_indices,
                                               size_t selected_output_count,
                                               char** out_source,
                                               cxpr_error* err) {
    cxpr_model_c_buf b = {0};
    char* safe_name = NULL;
    char** state_next_names = NULL;
    bool* needed_bindings = NULL;
    bool* skip_bindings = NULL;
    char** cse_names = NULL;
    cxpr_model_window_plan window_plan = {0};
    cxpr_model_ast_c_target ast_target_data = {0};
    cxpr_c_target ast_target = {0};

    if (out_source) *out_source = NULL;
    if (!program || !program->has_fused_layout || !function_name || !out_source) return false;
    ast_target_data.program = program;
    ast_target_data.function_prefix = function_name;
    ast_target_data.literal_param_values = literal_param_values;
    ast_target_data.literal_param_count = literal_param_count;
    ast_target.api_version = CXPR_C_TARGET_API_VERSION;
    ast_target.emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf;
    ast_target.emit_call_at_offset = cxpr_model_ast_c_emit_call;
    ast_target.userdata = &ast_target_data;
    state_next_names = (char**)calloc(program->fused_slot_count ? program->fused_slot_count : 1u,
                                      sizeof(char*));
    if (!state_next_names && program->fused_slot_count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    needed_bindings = (bool*)calloc(program->binding_count ? program->binding_count : 1u,
                                    sizeof(bool));
    if (!needed_bindings && program->binding_count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        free(state_next_names);
        return false;
    }
    skip_bindings = (bool*)calloc(program->binding_count ? program->binding_count : 1u,
                                  sizeof(bool));
    if (!skip_bindings && program->binding_count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        free(needed_bindings);
        free(state_next_names);
        return false;
    }
    cse_names = (char**)calloc(program->binding_count ? program->binding_count : 1u,
                               sizeof(char*));
    if (!cse_names && program->binding_count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        free(skip_bindings);
        free(needed_bindings);
        free(state_next_names);
        return false;
    }
    if (!cxpr_model_program_mark_required_bindings(
            program,
            output_indices,
            output_indices ? selected_output_count : 0u,
            output_indices ? false : true,
            true,
            true,
            needed_bindings,
            err)) {
        goto fail;
    }
    if (!cxpr_model_window_plan_build(program, &window_plan, err)) goto fail;
    cxpr_model_c_emit_common_helpers(&b);
    safe_name = cxpr_model_c_safe_name(function_name);
    if (!safe_name) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        goto fail;
    }
    cxpr_model_c_printf(&b, "typedef struct %s_state %s_state;\n\n", safe_name, safe_name);
    if (!cxpr_model_c_emit_child_model_helpers(program, function_name, &b, err)) goto fail;
    if (!cxpr_model_c_emit_defined_functions_ast(program, function_name, &b, err)) goto fail;
    if (!cxpr_model_c_emit_state_typedefs(
            &b, program, &window_plan, safe_name, err)) {
        goto fail;
    }
    if (!cxpr_model_c_emit_runtime_state_typedef(
            &b, program, &window_plan, safe_name, err)) {
        goto fail;
    }
    if (!cxpr_model_c_emit_slot_init_function(
            &b, program, &window_plan, qualifiers, safe_name, err)) {
        goto fail;
    }
    cxpr_model_c_printf(&b, "/* Source model tick: %s */\n", function_name);
    if (qualifiers && qualifiers[0]) cxpr_model_c_printf(&b, "%s ", qualifiers);
    cxpr_model_c_printf(
        &b,
        "void %s(%s_state* restrict _cx_state, const double* restrict _cx_inputs, const double* restrict _cx_params, double* restrict _cx_outputs) {\n"
        "",
        safe_name,
        safe_name);

    {
        size_t init_sentinel = 0u;
        if (cxpr_model_c_init_sentinel_slot(program, &window_plan, &init_sentinel)) {
            (void)init_sentinel;
            cxpr_model_c_printf(
                &b,
                "    if (CXPR_UNLIKELY(_cx_state->init == 0u)) %s_init_state(_cx_state);\n",
                safe_name);
        }
    }
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        char* field_name = cxpr_model_c_prefixed_name("state_", program->state_defaults[i].name);
        char* pending_name = cxpr_model_c_prefixed_name("pending_", program->state_defaults[i].name);
        char* has_pending_name = cxpr_model_c_prefixed_name(
            "has_pending_", program->state_defaults[i].name);
        if (!field_name || !pending_name || !has_pending_name) {
            free(field_name);
            free(pending_name);
            free(has_pending_name);
            goto oom;
        }
        cxpr_model_c_printf(
            &b,
            "    if (_cx_state->%s) { _cx_state->%s = _cx_state->%s; _cx_state->%s = 0u; }\n",
            has_pending_name,
            field_name,
            pending_name,
            has_pending_name);
        free(field_name);
        free(pending_name);
        free(has_pending_name);
    }
    for (size_t i = 0u; i < program->fused_input_count; ++i) {
        char* name = cxpr_model_c_prefixed_name("_cx_input_", program->fused_inputs[i].name);
        if (!name) goto oom;
        cxpr_model_c_printf(&b, "    const double _cx_input_%zu = _cx_inputs[%zu];\n", i, i);
        free(name);
    }
    if (!literal_param_values || literal_param_count < program->constant_count) {
        for (size_t i = 0u; i < program->constant_count; ++i) {
            cxpr_model_c_printf(&b, "    const double _cx_param_%zu = _cx_params[%zu];\n", i, i);
        }
    }
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        char* name = cxpr_model_c_prefixed_name("_cx_state_", program->state_defaults[i].name);
        char* field_name = cxpr_model_c_prefixed_name("state_", program->state_defaults[i].name);
        if (!name || !field_name) {
            free(name);
            free(field_name);
            goto fail;
        }
        cxpr_model_c_printf(&b, "    const double %s = _cx_state->%s;\n", name, field_name);
        free(name);
        free(field_name);
    }
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t depth = program->history_specs[i].depth;
        size_t capacity = cxpr_model_c_history_capacity(depth);
        if (depth == 0u) continue;
        if (capacity > 1u && !cxpr_model_c_history_use_shift(depth)) {
            cxpr_model_c_printf(&b, "    const size_t _cx_history_next_%zu = (size_t)_cx_state->history_%zu.next;\n",
                                i, i);
        }
    }
    for (size_t i = 0u; i < program->child_count; ++i) {
        cxpr_model_c_printf(&b, "    _cx_state->child_%zu_initialized = 0u;\n", i);
    }
    for (size_t i = 0u; i < program->binding_count; ++i) {
        char* expr;
        char* name;
        bool owns_name = false;
        if (!needed_bindings[i] || skip_bindings[i]) continue;
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
            size_t slot = cxpr_model_fused_slot_find(program->fused_slot_names,
                                                     program->fused_slot_count,
                                                     program->bindings[i].name);
            name = cxpr_model_c_prefixed_name("_cx_next_", program->bindings[i].name);
            if (slot == (size_t)-1) {
                free(name);
                name = NULL;
            } else if (name) {
                state_next_names[slot] = name;
            }
        } else {
            name = cxpr_model_c_safe_name(program->bindings[i].name);
            owns_name = true;
        }
        if (!name) goto oom;
        if (program->bindings[i].kind != CXPR_MODEL_BINDING_STATE_UPDATE) {
            const char* common = cxpr_model_c_find_common_binding_expr(
                program, i, needed_bindings, skip_bindings, cse_names);
            if (common) {
                cse_names[i] = cxpr_strdup(common);
                if (!cse_names[i]) {
                    if (owns_name) free(name);
                    goto oom;
                }
                cxpr_model_c_printf(&b, "    const double %s = %s;\n", name, common);
                if (owns_name) free(name);
                continue;
            }
        }
        if (i + 1u < program->binding_count &&
            needed_bindings[i + 1u] &&
            !skip_bindings[i + 1u] &&
            program->bindings[i].kind != CXPR_MODEL_BINDING_STATE_UPDATE &&
            program->bindings[i + 1u].kind != CXPR_MODEL_BINDING_STATE_UPDATE) {
            char* next_name = cxpr_model_c_safe_name(program->bindings[i + 1u].name);
            if (!next_name) {
                if (owns_name) free(name);
                goto oom;
            }
            if (cxpr_model_c_emit_optimized_binding_pair(
                    &b,
                    name,
                    next_name,
                    program->bindings[i].ast,
                    program->bindings[i + 1u].ast,
                    &ast_target,
                    program,
                    err)) {
                skip_bindings[i + 1u] = true;
                cse_names[i] = cxpr_strdup(name);
                cse_names[i + 1u] = cxpr_strdup(next_name);
                if (!cse_names[i] || !cse_names[i + 1u]) {
                    free(next_name);
                    if (owns_name) free(name);
                    goto oom;
                }
                free(next_name);
                if (owns_name) free(name);
                continue;
            } else if (err && err->code != CXPR_OK) {
                free(next_name);
                if (owns_name) free(name);
                goto fail;
            }
            free(next_name);
        }
        {
            const cxpr_model_window_plan_node* window_node =
                cxpr_model_window_plan_find_ast(&window_plan, program->bindings[i].ast);
            if (window_node &&
                cxpr_model_c_emit_planned_roc_aggregate_binding(
                    &b, name, &window_plan, window_node, &ast_target, program, err)) {
                cse_names[i] = cxpr_strdup(name);
                if (!cse_names[i]) {
                    if (owns_name) free(name);
                    goto oom;
                }
                if (owns_name) free(name);
                continue;
            } else if (window_node &&
                       cxpr_model_c_emit_planned_simple_aggregate_binding(
                           &b, name, &window_plan, window_node, &ast_target, program, err)) {
                cse_names[i] = cxpr_strdup(name);
                if (!cse_names[i]) {
                    if (owns_name) free(name);
                    goto oom;
                }
                if (owns_name) free(name);
                continue;
            } else if (err && err->code != CXPR_OK) {
                if (owns_name) free(name);
                goto fail;
            }
        }
        if (cxpr_model_c_emit_optimized_single_binding(
                &b, name, program->bindings[i].ast, &ast_target, program, err)) {
            cse_names[i] = cxpr_strdup(name);
            if (!cse_names[i]) {
                if (owns_name) free(name);
                goto oom;
            }
            if (owns_name) free(name);
            continue;
        } else if (err && err->code != CXPR_OK) {
            if (owns_name) free(name);
            goto fail;
        }
        expr = cxpr_ast_to_c(program->bindings[i].ast, &ast_target, err);
        if (!expr) {
            if (owns_name) free(name);
            goto fail;
        }
        cxpr_model_c_printf(&b, "    const double %s = %s;\n", name, expr);
        free(expr);
        cse_names[i] = cxpr_strdup(name);
        if (!cse_names[i]) {
            if (owns_name) free(name);
            goto oom;
        }
        if (owns_name) free(name);
    }
    for (size_t i = 0u; i < program->fused_commit_count; ++i) {
        char* next_name = state_next_names[program->fused_commits[i].state_slot];
        char* pending_name = cxpr_model_c_prefixed_name(
            "pending_", program->fused_slot_names[program->fused_commits[i].state_slot]);
        char* has_pending_name = cxpr_model_c_prefixed_name(
            "has_pending_", program->fused_slot_names[program->fused_commits[i].state_slot]);
        char* field_name = cxpr_model_c_prefixed_name(
            "state_", program->fused_slot_names[program->fused_commits[i].state_slot]);
        if (!next_name) goto oom;
        if (!pending_name || !has_pending_name || !field_name) {
            free(pending_name);
            free(has_pending_name);
            free(field_name);
            goto oom;
        }
        cxpr_model_c_printf(&b, "    _cx_state->%s = %s; _cx_state->%s = 1u;\n",
                            pending_name, next_name, has_pending_name);
        free(pending_name);
        free(has_pending_name);
        free(field_name);
    }
    for (size_t out_i = 0u; out_i < (output_indices ? selected_output_count : program->fused_output_count); ++out_i) {
        size_t i = output_indices ? output_indices[out_i] : out_i;
        const char* name;
        size_t state_slot = 0u;
        if (i >= program->fused_output_count) goto fail;
        name = program->fused_outputs[i].name;
        if (cxpr_model_c_symbol_is_state(program, name, &state_slot)) {
            char* field_name = cxpr_model_c_prefixed_name("state_", name);
            char* pending_name = cxpr_model_c_prefixed_name("pending_", name);
            char* has_pending_name = cxpr_model_c_prefixed_name("has_pending_", name);
            (void)state_slot;
            if (!field_name || !pending_name || !has_pending_name) {
                free(field_name);
                free(pending_name);
                free(has_pending_name);
                goto oom;
            }
            cxpr_model_c_printf(
                &b,
                "    _cx_outputs[%zu] = _cx_state->%s ? _cx_state->%s : _cx_state->%s;\n",
                out_i,
                has_pending_name,
                pending_name,
                field_name);
            free(field_name);
            free(pending_name);
            free(has_pending_name);
        } else {
            char* local_name = cxpr_model_c_safe_name(name);
            if (!local_name) goto oom;
            cxpr_model_c_printf(&b, "    _cx_outputs[%zu] = %s;\n", out_i, local_name);
            free(local_name);
        }
    }
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t depth = program->history_specs[i].depth;
        size_t capacity = cxpr_model_c_history_capacity(depth);
        char* current = NULL;
        if (program->history_specs[i].target &&
            cxpr_ast_type(program->history_specs[i].target) == CXPR_NODE_PRODUCER_ACCESS) {
            current = cxpr_model_ast_expr_to_c(program,
                                               program->history_specs[i].target,
                                               function_name,
                                               literal_param_values,
                                               literal_param_count,
                                               err);
        } else {
            current = cxpr_model_c_current_symbol_expr(
                program, state_next_names, program->history_specs[i].name, err);
        }
        if (!current) goto fail;
        if (depth > 0u) {
            if (cxpr_model_c_history_use_shift(depth)) {
                for (size_t j = depth; j > 1u; --j) {
                    cxpr_model_c_printf(&b, "    _cx_state->history_%zu.values[%zu] = _cx_state->history_%zu.values[%zu];\n",
                                        i,
                                        j - 1u,
                                        i,
                                        j - 2u);
                }
                cxpr_model_c_printf(
                    &b,
                    "    _cx_state->history_%zu.values[0] = %s;\n",
                    i,
                    current);
            } else if (cxpr_model_c_is_power_of_two(capacity)) {
                cxpr_model_c_printf(
                    &b,
                    "    { const double _cx_history_value_%zu = %s; _cx_state->history_%zu.values[_cx_history_next_%zu] = _cx_history_value_%zu; _cx_state->history_%zu.next = (%s)((_cx_history_next_%zu + 1u) & %zuu); }\n",
                    i,
                    current,
                    i,
                    i,
                    i,
                    i,
                    cxpr_model_c_history_counter_type(capacity),
                    i,
                    capacity - 1u);
            } else {
                cxpr_model_c_printf(
                    &b,
                    "    { const double _cx_history_value_%zu = %s; _cx_state->history_%zu.values[_cx_history_next_%zu] = _cx_history_value_%zu; _cx_state->history_%zu.next = (%s)((_cx_history_next_%zu + 1u) %% %zuu); }\n",
                    i,
                    current,
                    i,
                    i,
                    i,
                    i,
                    cxpr_model_c_history_counter_type(capacity),
                    i,
                    capacity);
            }
        }
        free(current);
    }
    cxpr_model_c_puts(&b, "}\n");
    if (b.oom) goto oom;
    for (size_t i = 0u; i < program->fused_slot_count; ++i) free(state_next_names[i]);
    free(state_next_names);
    free(needed_bindings);
    free(skip_bindings);
    for (size_t i = 0u; i < program->binding_count; ++i) free(cse_names[i]);
    free(cse_names);
    free(safe_name);
    cxpr_model_window_plan_free(&window_plan);
    *out_source = b.data;
    return true;

oom:
    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
fail:
    free(safe_name);
    if (state_next_names) {
        for (size_t i = 0u; i < program->fused_slot_count; ++i) free(state_next_names[i]);
        free(state_next_names);
    }
    free(needed_bindings);
    free(skip_bindings);
    if (cse_names) {
        for (size_t i = 0u; i < program->binding_count; ++i) free(cse_names[i]);
        free(cse_names);
    }
    cxpr_model_window_plan_free(&window_plan);
    free(b.data);
    return false;
}
