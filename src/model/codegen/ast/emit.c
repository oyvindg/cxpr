#include "model/codegen/ast/internal.h"
#include "model/window/plan.h"
#include "model/window/window.h"
#include "registry/internal.h"
#include <cxpr/codegen.h>
#include <cxpr/resample.h>
#include "eval/internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t cxpr_model_c_state_slot_for_name(const cxpr_model_compiled* program,
                                               const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) return i;
    }
    return (size_t)-1;
}

static size_t cxpr_model_c_state_slot_for_fused_slot(const cxpr_model_compiled* program,
                                                     size_t fused_slot) {
    if (!program || fused_slot >= program->fused_slot_count) return (size_t)-1;
    return cxpr_model_c_state_slot_for_name(program, program->fused_slot_names[fused_slot]);
}

static size_t CXPR_MODEL_MAYBE_UNUSED
cxpr_model_c_history_base(const cxpr_model_compiled* program,
                          size_t history_index) {
    (void)program;
    (void)history_index;
    return (size_t)-1;
}

static size_t cxpr_model_c_history_find(const cxpr_model_compiled* program,
                                        const char* name) {
    const char* dot;
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        if (cxpr_model_names_match(program->history_specs[i].name, name)) return i;
    }
    dot = strchr(name, '.');
    if (dot && dot != name) {
        char root[128];
        size_t root_len = (size_t)(dot - name);
        if (root_len < sizeof(root)) {
            const cxpr_model_compiled_binding* binding;
            char* expression;
            char* expanded;
            size_t expression_len;
            memcpy(root, name, root_len);
            root[root_len] = '\0';
            binding = cxpr_model_c_binding_for_name(program, root);
            expression = binding && binding->ast
                ? cxpr_expr_ast_to_string(binding->ast) : NULL;
            expression_len = expression ? strlen(expression) : 0u;
            expanded = expression
                ? (char*)malloc(expression_len + strlen(dot) + 1u) : NULL;
            if (expanded) {
                memcpy(expanded, expression, expression_len);
                strcpy(expanded + expression_len, dot);
                for (size_t i = 0u; i < program->history_spec_count; ++i) {
                    if (cxpr_model_names_match(
                            program->history_specs[i].name, expanded)) {
                        free(expanded);
                        free(expression);
                        return i;
                    }
                }
                free(expanded);
            }
            free(expression);
        }
    }
    return (size_t)-1;
}

char* cxpr_model_c_current_symbol_expr(const cxpr_model_compiled* program,
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
        static CXPR_THREAD_LOCAL char message[256];
        err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
        snprintf(message, sizeof(message),
                 "Unknown model C history target '%s'", name);
        err->message = message;
    }
    return NULL;
}

bool cxpr_model_c_emit_dynamic_history_value(cxpr_model_c_buf* b,
                                             const char* value_name,
                                             const cxpr_expr_ast* ast,
                                             const char* offset_expr,
                                             const cxpr_c_target* target,
                                             const cxpr_model_compiled* program,
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
    current_expr = cxpr_expr_ast_to_c_at_offset(ast, 0u, target, err);
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

char* cxpr_model_ast_c_emit_leaf(const cxpr_expr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        cxpr_error* err) {
    cxpr_model_ast_c_target* target = (cxpr_model_ast_c_target*)userdata;
    const cxpr_model_compiled* program = target ? target->program : NULL;
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
        for (size_t i = 0u; i < program->state_default_count; ++i) {
            const cxpr_model_compiled_binding* state = &program->state_defaults[i];
            if (state->declared_type == CXPR_MODEL_DECL_BUFFER &&
                cxpr_model_names_match(state->name, name)) {
                char* field_name = cxpr_model_c_prefixed_name("state_", state->name);
                char out[512];
                free(key);
                if (!field_name) return NULL;
                snprintf(out, sizeof(out),
                    "((%uu < _cx_state->%s.count) ? _cx_state->%s.values[(_cx_state->%s.next + %zuu - 1u - %uu) %% %zuu] : NAN)",
                    lookback_offset, field_name, field_name, field_name,
                    state->buffer_samples, lookback_offset, state->buffer_samples);
                free(field_name);
                return cxpr_strdup(out);
            }
        }
        hist_index = cxpr_model_c_history_find(program, name);
        if (hist_index == (size_t)-1) {
            if (err) {
                static CXPR_THREAD_LOCAL char message[256];
                size_t used;
                err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                used = (size_t)snprintf(message, sizeof(message),
                    "Unknown model C history target '%s' (available:", name);
                for (size_t i = 0u; i < program->history_spec_count &&
                     used + 4u < sizeof(message); ++i) {
                    int n = snprintf(message + used, sizeof(message) - used,
                        "%s%s", i ? "," : "", program->history_specs[i].name);
                    if (n < 0 || (size_t)n >= sizeof(message) - used) break;
                    used += (size_t)n;
                }
                if (used + 2u < sizeof(message)) strcpy(message + used, ")");
                err->message = message;
            }
            free(key);
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
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_IDENTIFIER) {
        name = cxpr_expr_ast_identifier_name(ast);
        for (size_t i = 0u; i < program->state_default_count; ++i) {
            const cxpr_model_compiled_binding* state = &program->state_defaults[i];
            if (state->declared_type == CXPR_MODEL_DECL_BUFFER &&
                cxpr_model_names_match(state->name, name)) {
                char* field_name = cxpr_model_c_prefixed_name("state_", state->name);
                char out[512];
                if (!field_name) return NULL;
                snprintf(out, sizeof(out),
                    "((%uu < _cx_state->%s.count) ? _cx_state->%s.values[(_cx_state->%s.next + %zuu - 1u - %uu) %% %zuu] : NAN)",
                    lookback_offset, field_name, field_name, field_name,
                    state->buffer_samples, lookback_offset, state->buffer_samples);
                free(field_name);
                return cxpr_strdup(out);
            }
        }
        for (size_t i = 0u; target && i < target->param_count; ++i) {
            if (cxpr_model_names_match(target->param_names[i], name)) {
                if (target->param_exprs && target->param_exprs[i]) {
                    return cxpr_strdup(target->param_exprs[i]);
                }
                return cxpr_model_c_safe_name(name);
            }
        }
        /* Model-defined function bodies parse `$param` references as canonical
         * identifiers. Standalone generated helpers receive captured model
         * parameters as explicit `_cx_param_N` arguments. */
        if (target && target->inline_fn_name) {
            index = cxpr_model_compiled_param_index(program, name);
            if (index != (size_t)-1) {
                char raw[64];
                snprintf(raw, sizeof(raw), "_cx_param_%zu", index);
                return cxpr_strdup(raw);
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
            static CXPR_THREAD_LOCAL char message[256];
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            snprintf(message, sizeof(message),
                     "Unknown model C identifier '%s'", name ? name : "?");
            err->message = message;
        }
        return NULL;
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS) {
        return cxpr_model_ast_producer_access_to_c(
            program,
            ast,
            target ? target->function_prefix : NULL,
            target ? target->literal_param_values : NULL,
            target ? target->literal_param_count : 0u,
            target ? target->child_call_keys : NULL,
            target ? target->child_call_child_indices : NULL,
            target ? target->child_call_count : 0u,
            err);
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FIELD_ACCESS) {
        cxpr_c_target field_target = {
            .api_version = CXPR_C_TARGET_API_VERSION,
            .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
            .emit_call_at_offset = cxpr_model_ast_c_emit_call,
            .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
            .userdata = target,
        };
        if (ast->data.field_access.base) {
            char* expr = cxpr_model_ast_field_expr_to_c(
                program, ast->data.field_access.base, ast->data.field_access.field, &field_target, err, 0u);
            if (expr) return expr;
        }
        const cxpr_model_compiled_binding* binding =
            cxpr_model_c_binding_for_name(program, ast->data.field_access.object);
        if (binding) {
            char* expr = cxpr_model_ast_field_expr_to_c(
                program, binding->ast, ast->data.field_access.field, &field_target, err, 0u);
            if (expr) return expr;
        }
        const cxpr_model_compiled_binding* constant =
            cxpr_model_c_constant_for_name(program, ast->data.field_access.object);
        if (constant) {
            char* expr = cxpr_model_ast_field_expr_to_c(
                program, constant->ast, ast->data.field_access.field, &field_target, err, 0u);
            if (expr) return expr;
        }
        if (!ast->data.field_access.base && ast->data.field_access.object &&
            ast->data.field_access.field) {
            char qualified[256];
            int written = snprintf(qualified, sizeof(qualified), "%s.%s",
                                   ast->data.field_access.object,
                                   ast->data.field_access.field);
            if (written > 0 && (size_t)written < sizeof(qualified) &&
                cxpr_model_c_symbol_is_input(program, qualified, &index)) {
                char raw[64];
                snprintf(raw, sizeof(raw), "_cx_input_%zu", index);
                return cxpr_strdup(raw);
            }
        }
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_VARIABLE) {
        name = cxpr_expr_ast_param_name(ast);
        const char* dot = name ? strchr(name, '.') : NULL;
        if (dot && dot > name && dot[1] != '\0') {
            size_t root_len = (size_t)(dot - name);
            char* root = (char*)malloc(root_len + 1u);
            if (!root) return NULL;
            memcpy(root, name, root_len);
            root[root_len] = '\0';
            const cxpr_model_compiled_binding* constant =
                cxpr_model_c_constant_for_name(program, root);
            free(root);
            if (constant) {
                cxpr_c_target field_target = {
                    .api_version = CXPR_C_TARGET_API_VERSION,
                    .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
                    .emit_call_at_offset = cxpr_model_ast_c_emit_call,
                    .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
                    .userdata = target,
                };
                char* expr = cxpr_model_ast_field_expr_to_c(
                    program, constant->ast, dot + 1, &field_target, err, 0u);
                if (expr) return expr;
            }
        }
        index = cxpr_model_compiled_param_index(program, name);
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
        static CXPR_THREAD_LOCAL char message[256];
        err->code = CXPR_ERR_SYNTAX;
        if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FIELD_ACCESS) {
            snprintf(message, sizeof(message),
                     "Unsupported model C field '%s.%s'",
                     ast->data.field_access.object ? ast->data.field_access.object : "?",
                     ast->data.field_access.field ? ast->data.field_access.field : "?");
        } else {
            snprintf(message, sizeof(message), "Unsupported model C leaf kind %d",
                     (int)cxpr_expr_ast_kind_of(ast));
        }
        err->message = message;
    }
    return NULL;
}

char* cxpr_model_ast_c_emit_lookback(const cxpr_expr_ast* ast,
                                            unsigned lookback_offset,
                                            void* userdata,
                                            cxpr_error* err) {
    cxpr_model_ast_c_target* data = (cxpr_model_ast_c_target*)userdata;
    const cxpr_model_compiled* program = data ? data->program : NULL;
    const cxpr_expr_ast* target_ast;
    const cxpr_expr_ast* index_ast;
    cxpr_c_target target;
    char* key = NULL;
    char* index_expr = NULL;
    char* current_expr = NULL;
    size_t hist_index;
    size_t depth;
    size_t capacity;
    char out[1024];

    if (!data || !program || !ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_INDEX) return NULL;
    target_ast = cxpr_expr_ast_index_target(ast);
    index_ast = cxpr_expr_ast_index_expression(ast);
    if (!cxpr_model_lookback_target_key(target_ast, &key, err)) return NULL;
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        const cxpr_model_compiled_binding* state = &program->state_defaults[i];
        if (state->declared_type == CXPR_MODEL_DECL_BUFFER &&
            cxpr_model_names_match(state->name, key)) {
            char* field_name = cxpr_model_c_prefixed_name("state_", state->name);
            target = (cxpr_c_target){
                .api_version = CXPR_C_TARGET_API_VERSION,
                .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
                .emit_call_at_offset = cxpr_model_ast_c_emit_call,
                .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
                .userdata = data,
            };
            index_expr = cxpr_expr_ast_to_c_at_offset(index_ast, 0u, &target, err);
            free(key);
            if (!field_name || !index_expr) {
                free(field_name); free(index_expr);
                return NULL;
            }
            snprintf(out, sizeof(out),
                "((isfinite(%s) && (%s) >= 0.0 && floor(%s) == (%s) && (size_t)(%s) < _cx_state->%s.count) ? _cx_state->%s.values[(_cx_state->%s.next + %zuu - 1u - (size_t)(%s)) %% %zuu] : NAN)",
                index_expr, index_expr, index_expr, index_expr, index_expr,
                field_name, field_name, field_name, state->buffer_samples,
                index_expr, state->buffer_samples);
            free(field_name); free(index_expr);
            return cxpr_strdup(out);
        }
    }
    hist_index = cxpr_model_c_history_find(program, key);
    free(key);
    if (hist_index == (size_t)-1) {
        cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                             "Unknown model C history target", 0, 0);
        return NULL;
    }
    target = (cxpr_c_target){
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
        .userdata = data,
    };
    index_expr = cxpr_expr_ast_to_c_at_offset(index_ast, 0u, &target, err);
    current_expr = cxpr_expr_ast_to_c_at_offset(target_ast, lookback_offset, &target, err);
    if (!index_expr || !current_expr) {
        free(index_expr); free(current_expr);
        return NULL;
    }
    depth = program->history_specs[hist_index].depth;
    capacity = cxpr_model_c_history_capacity(depth);
    if (lookback_offset != 0u) {
        /* Nested dynamic lookbacks are conservatively bounded. */
        snprintf(out, sizeof(out), "NAN");
    } else if (cxpr_model_c_history_use_shift(depth)) {
        snprintf(out, sizeof(out), "(((%s) <= 0.0) ? %s : ((%s) <= %zuu ? _cx_state->history_%zu.values[(size_t)(%s) - 1u] : NAN))",
                 index_expr, current_expr, index_expr, depth, hist_index, index_expr);
    } else if (cxpr_model_c_is_power_of_two(capacity)) {
        snprintf(out, sizeof(out), "(((%s) <= 0.0) ? %s : ((%s) <= %zuu ? _cx_state->history_%zu.values[(_cx_history_next_%zu + %zuu - (size_t)(%s)) & %zuu] : NAN))",
                 index_expr, current_expr, index_expr, depth, hist_index, hist_index, capacity, index_expr, capacity - 1u);
    } else {
        snprintf(out, sizeof(out), "(((%s) <= 0.0) ? %s : ((%s) <= %zuu ? _cx_state->history_%zu.values[(_cx_history_next_%zu + %zuu - (size_t)(%s)) %% %zuu] : NAN))",
                 index_expr, current_expr, index_expr, depth, hist_index, hist_index, capacity, index_expr, capacity);
    }
    free(index_expr); free(current_expr);
    return cxpr_strdup(out);
}
