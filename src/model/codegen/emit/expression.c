#include "model/codegen/ast/internal.h"
#include "model/window/window.h"
#include "registry/internal.h"

#include <cxpr/resample.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool cxpr_model_defined_body_uses_view_syntax(const cxpr_expr_ast* ast) {
    const char* names[64];
    const size_t count = cxpr_expr_ast_functions_used(ast, names, 64u);
    for (size_t i = 0u; i < count && i < 64u; ++i) {
        if (cxpr_model_names_match(names[i], "get") ||
            cxpr_model_names_match(names[i], "length")) return true;
    }
    return false;
}

char* cxpr_model_ast_expr_to_c_with_temps(
    cxpr_model_ast_temp_emit* emit,
    const cxpr_expr_ast* ast,
    cxpr_error* err);
static char* cxpr_model_ast_c_emit_window_call(const cxpr_expr_ast* ast,
                                               unsigned lookback_offset,
                                               const cxpr_c_target* target,
                                               const cxpr_model_compiled* program,
                                               cxpr_error* err) {
    const char* name = cxpr_expr_ast_call_name(ast);
    const char* op = cxpr_model_c_window_op(name);
    const cxpr_window_ir* window = cxpr_window_ir_find(name);
    bool is_roc = window && window->op == CXPR_WINDOW_OP_ROC;
    bool is_bars_since_extreme =
        window && window->op == CXPR_WINDOW_OP_BARS_SINCE_EXTREME;
    bool is_window_mean_absdev =
        window && window->op == CXPR_WINDOW_OP_MEAN_ABSDEV;
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* period_ast;
    size_t capacity = 0u;
    size_t value_count;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;
    bool guard_values = false;
    cxpr_model_c_buf b = {0};
    if (!program || !window ||
        (!op && !is_roc && !is_bars_since_extreme && !is_window_mean_absdev) ||
        cxpr_expr_ast_call_arg_count(ast) != window->arity) {
        cxpr_model_set_error(err, CXPR_ERR_WRONG_ARITY,
                             "window function has wrong arity", 0, 0);
        return NULL;
    }
    value_ast = cxpr_expr_ast_call_arg(ast, 0u);
    period_ast = cxpr_expr_ast_call_arg(ast, 1u);
    guard_values = cxpr_expr_ast_kind_of(period_ast) == CXPR_NODE_VARIABLE;
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return NULL;
    period_expr = cxpr_expr_ast_to_c_at_offset(period_ast, 0u, target, err);
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
    if (cxpr_model_names_match(name, "__cxpr_window_mean") &&
        cxpr_expr_ast_kind_of(value_ast) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_names_match(cxpr_expr_ast_call_name(value_ast), "__cxpr_window_roc") &&
        cxpr_expr_ast_call_arg_count(value_ast) == 2u) {
        const cxpr_expr_ast* roc_value_ast = cxpr_expr_ast_call_arg(value_ast, 0u);
        const cxpr_expr_ast* roc_period_ast = cxpr_expr_ast_call_arg(value_ast, 1u);
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
        roc_period_expr = cxpr_expr_ast_to_c_at_offset(roc_period_ast, 0u, target, err);
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
            source_expr = cxpr_expr_ast_to_c_at_offset(
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
    if (is_bars_since_extreme) {
        const cxpr_expr_ast* mode_ast = cxpr_expr_ast_call_arg(ast, 2u);
        char* mode_expr = cxpr_expr_ast_to_c_at_offset(mode_ast, 0u, target, err);
        if (!mode_expr) {
            free(period_expr);
            free(period_limit_expr);
            return NULL;
        }
        cxpr_model_c_puts(&b, "cxpr_model_bars_since_extreme_c((const double[]){");
        for (size_t i = 0u; i < capacity; ++i) {
            char* value_expr;
            if (i > 0u) cxpr_model_c_puts(&b, ", ");
            value_expr = cxpr_expr_ast_to_c_at_offset(
                value_ast, lookback_offset + (unsigned)i, target, err);
            if (!value_expr) {
                free(mode_expr);
                free(period_expr);
                free(period_limit_expr);
                free(b.data);
                return NULL;
            }
            if (!guard_values) {
                cxpr_model_c_printf(&b, "(%s)", value_expr);
            } else {
                cxpr_model_c_printf(&b,
                                    "((%zuu < (size_t)(%s)) ? (%s) : NAN)",
                                    i,
                                    period_limit_expr,
                                    value_expr);
            }
            free(value_expr);
        }
        cxpr_model_c_printf(
            &b,
            "}, %zuu, %s, %s)",
            capacity,
            period_limit_expr,
            mode_expr);
        free(mode_expr);
        free(period_expr);
        free(period_limit_expr);
        if (b.oom) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return b.data;
    }
    if (is_window_mean_absdev) {
        const cxpr_expr_ast* center_ast = cxpr_expr_ast_call_arg(ast, 2u);
        char* center_expr = cxpr_expr_ast_to_c_at_offset(center_ast, lookback_offset, target, err);
        if (!center_expr) {
            free(period_expr);
            free(period_limit_expr);
            return NULL;
        }
        cxpr_model_c_puts(&b, "cxpr_model_window_mean_absdev_c((const double[]){");
        for (size_t i = 0u; i < capacity; ++i) {
            char* value_expr;
            if (i > 0u) cxpr_model_c_puts(&b, ", ");
            value_expr = cxpr_expr_ast_to_c_at_offset(
                value_ast, lookback_offset + (unsigned)i, target, err);
            if (!value_expr) {
                free(center_expr);
                free(period_expr);
                free(period_limit_expr);
                free(b.data);
                return NULL;
            }
            if (!guard_values) {
                cxpr_model_c_printf(&b, "(%s)", value_expr);
            } else {
                cxpr_model_c_printf(&b,
                                    "((%zuu < (size_t)(%s)) ? (%s) : NAN)",
                                    i,
                                    period_limit_expr,
                                    value_expr);
            }
            free(value_expr);
        }
        cxpr_model_c_printf(
            &b,
            "}, %zuu, %s, %s)",
            capacity,
            period_limit_expr,
            center_expr);
        free(center_expr);
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
        value_expr = (cxpr_expr_ast_kind_of(value_ast) == CXPR_NODE_FUNCTION_CALL &&
                      cxpr_model_window_is_function(cxpr_expr_ast_call_name(value_ast)))
                         ? cxpr_model_ast_c_emit_window_call(
                               value_ast, lookback_offset + (unsigned)i, target, program, err)
                         : cxpr_expr_ast_to_c_at_offset(
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

char* cxpr_model_ast_c_emit_call(const cxpr_expr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        bool* handled,
                                        cxpr_error* err) {
    const char* name = cxpr_expr_ast_call_name(ast);
    size_t argc = cxpr_expr_ast_call_arg_count(ast);
    cxpr_model_ast_c_target* target_data = (cxpr_model_ast_c_target*)userdata;
    const cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
        .userdata = userdata,
    };
    cxpr_model_c_buf b = {0};

    (void)target_data;
    if (handled) *handled = false;
    if (!name) return NULL;

    if (target_data && target_data->program &&
        cxpr_model_names_match(name, "get") && argc >= 2u &&
        cxpr_expr_ast_kind_of(cxpr_expr_ast_call_arg(ast, 0u)) ==
            CXPR_NODE_IDENTIFIER) {
        const cxpr_expr_ast* input_ast = cxpr_expr_ast_call_arg(ast, 0u);
        const char* input_name = cxpr_expr_ast_identifier_name(input_ast);
        size_t input_index = 0u;
        size_t view_slot = 0u;
        size_t rank = 0u;
        cxpr_model_element_type element_type = CXPR_MODEL_ELEMENT_UNKNOWN;
        char* indices[CXPR_VIEW_MAX_RANK] = {0};
        char* fallback = NULL;
        const char* c_type;
        const size_t index_count = argc - 2u;
        for (size_t i = 0u; i < target_data->param_ast_count; ++i) {
            if (target_data->param_names[i] && target_data->param_asts &&
                target_data->param_asts[i] &&
                cxpr_model_names_match(target_data->param_names[i], input_name) &&
                cxpr_expr_ast_kind_of(target_data->param_asts[i]) ==
                    CXPR_NODE_IDENTIFIER) {
                input_ast = target_data->param_asts[i];
                input_name = cxpr_expr_ast_identifier_name(input_ast);
                break;
            }
        }
        if (!cxpr_model_c_symbol_is_input(
                target_data->program, input_name, &input_index) ||
            !cxpr_model_compiled_input_view_requirement(
                target_data->program, input_index, &element_type, &rank, NULL))
            return NULL;
        if (rank != index_count) {
            cxpr_model_set_error(err, CXPR_ERR_WRONG_ARITY,
                "External view helper requires one index per dimension", 0u, 0u);
            if (handled) *handled = true;
            return NULL;
        }
        for (size_t i = 0u; i < input_index; ++i)
            if (cxpr_model_compiled_input_view_requirement(
                    target_data->program, i, NULL, NULL, NULL)) ++view_slot;
        for (size_t i = 0u; i < rank; ++i) {
            indices[i] = cxpr_expr_ast_to_c_at_offset(
                cxpr_expr_ast_call_arg(ast, i + 1u), lookback_offset,
                &target, err);
            if (!indices[i]) goto view_helper_fail;
        }
        fallback = cxpr_expr_ast_to_c_at_offset(
            cxpr_expr_ast_call_arg(ast, argc - 1u), lookback_offset,
            &target, err);
        if (!fallback) goto view_helper_fail;
        c_type = element_type == CXPR_MODEL_ELEMENT_BOOL ? "bool" :
                 element_type == CXPR_MODEL_ELEMENT_INT ? "int64_t" : "double";
        cxpr_model_c_printf(&b,
            "((_cx_views && %zuu < _cx_views->count && "
            "_cx_views->views[%zuu].rank == %zuu",
            view_slot, view_slot, rank);
        for (size_t i = 0u; i < rank; ++i) {
            cxpr_model_c_printf(&b,
                " && isfinite((double)(%s)) && floor((double)(%s)) == (double)(%s)",
                indices[i], indices[i], indices[i]);
            cxpr_model_c_printf(&b,
                " && (double)(%s) >= 0.0 && (size_t)(%s) < "
                "_cx_views->views[%zuu].shape[%zuu]",
                indices[i], indices[i], view_slot, i);
        }
        cxpr_model_c_printf(&b,
            ") ? ((const %s*)_cx_views->views[%zuu].data)[",
            c_type, view_slot);
        for (size_t i = 0u; i < rank; ++i) {
            if (i) cxpr_model_c_puts(&b, " + ");
            cxpr_model_c_printf(&b,
                "(size_t)(%s) * _cx_views->views[%zuu].element_strides[%zuu]",
                indices[i], view_slot, i);
        }
        cxpr_model_c_printf(&b, "] : %s)", fallback);
        for (size_t i = 0u; i < rank; ++i) free(indices[i]);
        free(fallback);
        if (b.oom) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                 "Out of memory", 0u, 0u);
            return NULL;
        }
        if (handled) *handled = true;
        return b.data;

view_helper_fail:
        for (size_t i = 0u; i < rank; ++i) free(indices[i]);
        free(fallback);
        free(b.data);
        if (handled) *handled = true;
        return NULL;
    }

    if (target_data && cxpr_model_names_match(name, "__cxpr_let") && argc == 3u &&
        cxpr_expr_ast_kind_of(cxpr_expr_ast_call_arg(ast, 0u)) == CXPR_NODE_IDENTIFIER) {
        const char* local = cxpr_expr_ast_identifier_name(cxpr_expr_ast_call_arg(ast, 0u));
        char* value = cxpr_expr_ast_to_c_at_offset(
            cxpr_expr_ast_call_arg(ast, 1u), lookback_offset, &target, err);
        char temp[64];
        char** names;
        char** exprs;
        cxpr_model_ast_c_target nested;
        cxpr_c_target nested_target = target;
        char* result;
        size_t base = target_data->param_count;
        cxpr_model_local_cse* cse = target_data->local_cse;
        if (!value || !target_data->local_declarations) {
            if (err && !value) { err->code = CXPR_ERR_SYNTAX; err->message = "let value C emission failed"; }
            free(value); return NULL;
        }
        temp[0] = '\0';
        for (size_t i = 0u; cse && i < cse->count; ++i)
            if (strcmp(cse->exprs[i], value) == 0) {
                snprintf(temp, sizeof(temp), "%s", cse->names[i]);
                break;
            }
        if (!temp[0]) {
            char** exprs_grown;
            char** names_grown;
            if (!cse) { free(value); return NULL; }
            if (cse->count == cse->capacity) {
                size_t capacity = cse->capacity ? cse->capacity * 2u : 16u;
                exprs_grown = (char**)realloc(
                    cse->exprs, capacity * sizeof(*exprs_grown));
                if (!exprs_grown) { free(value); return NULL; }
                cse->exprs = exprs_grown;
                names_grown = (char**)realloc(
                    cse->names, capacity * sizeof(*names_grown));
                if (!names_grown) { free(value); return NULL; }
                cse->names = names_grown;
                cse->capacity = capacity;
            }
            snprintf(temp, sizeof(temp), "_cx_local_%zu", cse->next_temp++);
            cxpr_model_c_printf(target_data->local_declarations,
                                "    const double %s = %s;\n", temp, value);
            cse->exprs[cse->count] = cxpr_strdup(value);
            cse->names[cse->count] = cxpr_strdup(temp);
            if (!cse->exprs[cse->count] || !cse->names[cse->count]) { free(value); return NULL; }
            cse->count++;
        }
        free(value);
        names = (char**)calloc(base + 1u, sizeof(*names));
        exprs = (char**)calloc(base + 1u, sizeof(*exprs));
        if (!names || !exprs) { free(names); free(exprs); return NULL; }
        for (size_t i = 0u; i < base; ++i) {
            names[i] = target_data->param_names[i];
            exprs[i] = target_data->param_exprs ? target_data->param_exprs[i] : NULL;
        }
        names[base] = (char*)local;
        exprs[base] = temp;
        nested = *target_data;
        nested.param_names = names;
        nested.param_exprs = exprs;
        nested.param_count = base + 1u;
        nested_target.userdata = &nested;
        result = cxpr_expr_ast_to_c_at_offset(
            cxpr_expr_ast_call_arg(ast, 2u), lookback_offset, &nested_target, err);
        free(names);
        free(exprs);
        if (!result && err && err->code == CXPR_OK) { err->code = CXPR_ERR_SYNTAX; err->message = "let body C emission failed"; }
        if (handled) *handled = true;
        return result;
    }

    if (target_data && target_data->program &&
        cxpr_model_names_match(name, "length") && argc == 2u &&
        cxpr_expr_ast_kind_of(cxpr_expr_ast_call_arg(ast, 0u)) ==
            CXPR_NODE_IDENTIFIER) {
        const char* input_name = cxpr_expr_ast_identifier_name(
            cxpr_expr_ast_call_arg(ast, 0u));
        const cxpr_expr_ast* dimension_ast = cxpr_expr_ast_call_arg(ast, 1u);
        size_t input_index = 0u;
        size_t view_slot = 0u;
        size_t rank = 0u;
        size_t dimension = SIZE_MAX;
        char raw[192];
        for (size_t i = 0u; i < target_data->param_ast_count; ++i) {
            if (target_data->param_names[i] && target_data->param_asts &&
                target_data->param_asts[i] &&
                cxpr_model_names_match(target_data->param_names[i], input_name) &&
                cxpr_expr_ast_kind_of(target_data->param_asts[i]) ==
                    CXPR_NODE_IDENTIFIER) {
                input_name = cxpr_expr_ast_identifier_name(target_data->param_asts[i]);
                break;
            }
        }
        if (!cxpr_model_c_symbol_is_input(
                target_data->program, input_name, &input_index) ||
            !cxpr_model_compiled_input_view_requirement(
                target_data->program, input_index, NULL, &rank, NULL)) return NULL;
        if (cxpr_expr_ast_kind_of(dimension_ast) == CXPR_NODE_INT64) {
            const int64_t value = cxpr_expr_ast_int64_value(dimension_ast);
            if (value >= 0) dimension = (size_t)value;
        } else if (cxpr_expr_ast_kind_of(dimension_ast) == CXPR_NODE_NUMBER) {
            const double value = cxpr_expr_ast_number_value(dimension_ast);
            if (isfinite(value) && value >= 0.0 && floor(value) == value &&
                value <= (double)SIZE_MAX) dimension = (size_t)value;
        }
        if (dimension >= rank) {
            cxpr_model_set_error(err, CXPR_ERR_INVALID_INDEX,
                "External view length dimension must be a compile-time integer within rank",
                0u, 0u);
            return NULL;
        }
        for (size_t i = 0u; i < input_index; ++i)
            if (cxpr_model_compiled_input_view_requirement(
                    target_data->program, i, NULL, NULL, NULL)) ++view_slot;
        snprintf(raw, sizeof(raw),
            "((_cx_views && %zuu < _cx_views->count && "
            "_cx_views->views[%zuu].rank == %zuu) ? "
            "(double)_cx_views->views[%zuu].shape[%zuu] : NAN)",
            view_slot, view_slot, rank, view_slot, dimension);
        if (handled) *handled = true;
        return cxpr_strdup(raw);
    }

    if (target_data && target_data->program &&
        cxpr_model_names_match(name, "length") && argc == 1u) {
        const cxpr_expr_ast* collection = cxpr_model_c_static_collection(
            target_data->program, cxpr_expr_ast_call_arg(ast, 0u), 0u);
        size_t count = 0u;
        char raw[64];
        if (collection && cxpr_expr_ast_kind_of(collection) == CXPR_NODE_ARRAY) {
            count = cxpr_expr_ast_array_count(collection);
        } else if (collection && cxpr_expr_ast_kind_of(collection) == CXPR_NODE_RECORD &&
                   cxpr_expr_ast_record_field_count(collection) > 0u) {
            const cxpr_expr_ast* first = cxpr_model_c_static_collection(
                target_data->program,
                cxpr_expr_ast_record_field_value(collection, 0u), 0u);
            if (!first || cxpr_expr_ast_kind_of(first) != CXPR_NODE_ARRAY) return NULL;
            count = cxpr_expr_ast_array_count(first);
            for (size_t i = 1u; i < cxpr_expr_ast_record_field_count(collection); ++i) {
                const cxpr_expr_ast* field = cxpr_model_c_static_collection(
                    target_data->program,
                    cxpr_expr_ast_record_field_value(collection, i), 0u);
                if (!field || cxpr_expr_ast_kind_of(field) != CXPR_NODE_ARRAY ||
                    cxpr_expr_ast_array_count(field) != count) {
                    cxpr_model_set_error(err, CXPR_ERR_TYPE_MISMATCH,
                        "Static record view fields must have equal array lengths", 0, 0);
                    return NULL;
                }
            }
        } else return NULL;
        if (handled) *handled = true;
        snprintf(raw, sizeof(raw), "%zu.0", count);
        return cxpr_strdup(raw);
    }

    if (target_data && target_data->program &&
        cxpr_model_names_match(name, "upper_bound") && (argc == 2u || argc == 3u)) {
        const cxpr_expr_ast* collection = cxpr_model_c_static_collection(
            target_data->program, cxpr_expr_ast_call_arg(ast, 0u), 0u);
        const char* selector_field = NULL;
        char* value;
        if (!collection || cxpr_expr_ast_kind_of(collection) != CXPR_NODE_ARRAY)
            return NULL;
        if (argc == 3u) {
            const cxpr_expr_ast* selector = cxpr_expr_ast_call_arg(ast, 2u);
            const cxpr_expr_ast* body;
            if (!selector || cxpr_expr_ast_kind_of(selector) != CXPR_NODE_FUNCTION_CALL ||
                !cxpr_model_names_match(cxpr_expr_ast_call_name(selector), "__cxpr_lambda") ||
                cxpr_expr_ast_call_arg_count(selector) != 2u) return NULL;
            body = cxpr_expr_ast_call_arg(selector, 1u);
            if (!body || cxpr_expr_ast_kind_of(body) != CXPR_NODE_FIELD_ACCESS) return NULL;
            selector_field = cxpr_expr_ast_field_name(body);
        }
        value = cxpr_expr_ast_to_c_at_offset(
            cxpr_expr_ast_call_arg(ast, 1u), lookback_offset, &target, err);
        if (!value) return NULL;
        if (handled) *handled = true;
        cxpr_model_c_puts(&b, "(");
        for (size_t i = 0u; i < cxpr_expr_ast_array_count(collection); ++i) {
            const cxpr_expr_ast* key = cxpr_expr_ast_array_element(collection, i);
            if (selector_field) key = cxpr_model_c_static_record_field(
                target_data->program, key, selector_field, 0u);
            char* element = cxpr_expr_ast_to_c_at_offset(
                key, lookback_offset, &target, err);
            if (!element) { free(value); free(b.data); return NULL; }
            cxpr_model_c_printf(&b, "((%s) < (%s)) ? %zu.0 : ", value, element, i);
            free(element);
        }
        cxpr_model_c_printf(&b, "%zu.0)", cxpr_expr_ast_array_count(collection));
        free(value);
        if (b.oom) { free(b.data); cxpr_model_set_error(
            err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0); return NULL; }
        return b.data;
    }

    /* Static collection bindings have no runtime C representation. Expand a
     * bound map source here so the standalone codegen can fold map + reduce
     * while retaining its model-agnostic selector implementation. */
    if (target_data && target_data->program && argc == 1u &&
        (!strcmp(name, "sum") || !strcmp(name, "mean") ||
         !strcmp(name, "min") || !strcmp(name, "max") ||
         !strcmp(name, "any") || !strcmp(name, "all"))) {
        const cxpr_expr_ast* map = cxpr_expr_ast_call_arg(ast, 0u);
        if (map && cxpr_expr_ast_kind_of(map) == CXPR_NODE_FUNCTION_CALL &&
            cxpr_model_names_match(cxpr_expr_ast_call_name(map), "map") &&
            cxpr_expr_ast_call_arg_count(map) == 2u) {
            const cxpr_expr_ast* source = cxpr_expr_ast_call_arg(map, 0u);
            if (source && cxpr_expr_ast_kind_of(source) == CXPR_NODE_IDENTIFIER) {
                const cxpr_model_compiled_binding* binding =
                    cxpr_model_c_binding_for_name(
                        target_data->program, cxpr_expr_ast_identifier_name(source));
                if (binding && cxpr_expr_ast_kind_of(binding->ast) == CXPR_NODE_ARRAY) {
                    cxpr_expr_ast map_copy = *map;
                    cxpr_expr_ast reduce_copy = *ast;
                    cxpr_expr_ast* map_args[2] = {
                        binding->ast, (cxpr_expr_ast*)cxpr_expr_ast_call_arg(map, 1u)
                    };
                    cxpr_expr_ast* reduce_args[1] = { &map_copy };
                    map_copy.data.function_call.args = map_args;
                    reduce_copy.data.function_call.args = reduce_args;
                    if (handled) *handled = true;
                    return cxpr_expr_ast_to_c_at_offset(
                        &reduce_copy, lookback_offset, &target, err);
                }
            }
        }
    }

    if (cxpr_model_names_match(name, "resample")) {
        cxpr_resample_call call = {0};
        char* source_name;
        size_t slot = (size_t)-1;
        char raw[768];
        if (handled) *handled = true;
        if (!target_data || !target_data->program ||
            !cxpr_resample_call_parse(ast, &call, err) || !call.source ||
            !(source_name = cxpr_resample_static_source_name(call.source, err))) return NULL;
        for (size_t i = 0u; i < target_data->program->resample_requirement_count; ++i) {
            const cxpr_model_resample_requirement* req =
                &target_data->program->resample_requirements[i];
            if (req->duration_ns == call.every.duration_ns &&
                cxpr_model_names_match(req->source_name, source_name)) {
                slot = i;
                break;
            }
        }
        if (slot == (size_t)-1) {
            free(source_name);
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "Unknown generated resample requirement", 0, 0);
            return NULL;
        }
        free(source_name);
        {
            bool typed_views = false;
            const bool int64_value = target_data->program->resample_requirements[slot].result_kind ==
                                     CXPR_MODEL_RESULT_INT64;
            const char* values_field;
            const char* missing;
            for (size_t i = 0u; i < target_data->program->resample_requirement_count; ++i)
                if (target_data->program->resample_requirements[i].result_kind ==
                    CXPR_MODEL_RESULT_INT64) typed_views = true;
            values_field = typed_views
                ? (int64_value ? "int64_values" : "number_values") : "values";
            missing = int64_value ? "INT64_C(0)" : "NAN";
        for (size_t i = 0u; i < target_data->resample_cse_count; ++i) {
            const cxpr_model_resample_cse* cse = &target_data->resample_cse[i];
            if (cse->uses >= 2u && cse->slot == slot &&
                cse->lookback == lookback_offset) {
                snprintf(raw, sizeof(raw), "_cx_resample_value_%zu_%u",
                         slot, lookback_offset);
                return cxpr_strdup(raw);
            }
        }
        if (lookback_offset == 0u) {
            snprintf(raw, sizeof(raw),
                "((_cx_primary_cursor < _cx_resample_views[%zu].primary_count && "
                "_cx_resample_views[%zu].%s && "
                "_cx_resample_views[%zu].alignment && "
                "_cx_resample_views[%zu].alignment[_cx_primary_cursor] < "
                "_cx_resample_views[%zu].value_count) ? "
                "_cx_resample_views[%zu].%s[_cx_resample_views[%zu].alignment[_cx_primary_cursor]] : %s)",
                slot, slot, values_field, slot, slot, slot, slot, values_field,
                slot, missing);
            return cxpr_strdup(raw);
        }
        snprintf(raw, sizeof(raw),
            "((_cx_primary_cursor < _cx_resample_views[%zu].primary_count && "
            "_cx_resample_views[%zu].%s && "
            "_cx_resample_views[%zu].alignment && "
            "_cx_resample_views[%zu].alignment[_cx_primary_cursor] >= %uu && "
            "_cx_resample_views[%zu].alignment[_cx_primary_cursor] - %uu < "
            "_cx_resample_views[%zu].value_count) ? "
            "_cx_resample_views[%zu].%s[_cx_resample_views[%zu].alignment[_cx_primary_cursor] - %uu] : %s)",
            slot, slot, values_field, slot, slot, lookback_offset, slot,
            lookback_offset, slot, slot, values_field, slot, lookback_offset,
            missing);
        return cxpr_strdup(raw);
        }
    }

    if (target_data && target_data->program &&
        target_data->program->registry) {
        cxpr_func_entry* entry = cxpr_registry_find(
            target_data->program->registry, name);
        size_t scalar_field = (size_t)-1;
        size_t public_field_count = 0u;
        if (entry && entry->model_producer) {
            for (size_t i = 0u; i < entry->defined_return_field_count; ++i) {
                if (cxpr_model_names_match(
                        entry->defined_return_field_names[i],
                        "__cxpr_import_dummy")) {
                    continue;
                }
                scalar_field = i;
                ++public_field_count;
            }
        }
        if (entry && entry->model_producer &&
            !cxpr_model_names_match(name, "abs") &&
            public_field_count == 1u) {
            cxpr_expr_ast producer = {0};
            producer.type = CXPR_NODE_PRODUCER_ACCESS;
            producer.data.producer_access.name = ast->data.function_call.name;
            producer.data.producer_access.args = ast->data.function_call.args;
            producer.data.producer_access.arg_names = ast->data.function_call.arg_names;
            producer.data.producer_access.argc = ast->data.function_call.argc;
            producer.data.producer_access.field =
                entry->defined_return_field_names[scalar_field];
            if (handled) *handled = true;
            return cxpr_model_ast_producer_access_to_c(
                target_data->program,
                &producer,
                target_data->function_prefix,
                target_data->literal_param_values,
                target_data->literal_param_count,
                target_data->child_call_keys,
                target_data->child_call_child_indices,
                target_data->child_call_count,
                target_data,
                err);
        }
    }

    if (cxpr_model_window_is_function(name)) {
        if (handled) *handled = true;
        return cxpr_model_ast_c_emit_window_call(
            ast, lookback_offset, &target,
            target_data ? target_data->program : NULL,
            err);
    }

    if (cxpr_model_names_match(name, "roc") && argc == 2u) {
        char* current;
        char* previous;
        const cxpr_expr_ast* period = cxpr_expr_ast_call_arg(ast, 1u);
        double raw_period;
        unsigned period_offset;
        if (handled) *handled = true;
        if (!period || cxpr_expr_ast_kind_of(period) != CXPR_NODE_NUMBER) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "roc C codegen requires a constant period", 0, 0);
            return NULL;
        }
        raw_period = cxpr_expr_ast_number_value(period);
        period_offset = raw_period > 0.0 ? (unsigned)(raw_period + 0.5) : 0u;
        if (!isfinite(raw_period) || period_offset == 0u ||
            fabs(raw_period - (double)period_offset) > 1e-9) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "roc period must be a positive integer", 0, 0);
            return NULL;
        }
        current = cxpr_expr_ast_to_c_at_offset(
            cxpr_expr_ast_call_arg(ast, 0u), lookback_offset, &target, err);
        previous = current ? cxpr_expr_ast_to_c_at_offset(
            cxpr_expr_ast_call_arg(ast, 0u), lookback_offset + period_offset,
            &target, err) : NULL;
        if (!current || !previous) {
            free(current);
            free(previous);
            return NULL;
        }
        cxpr_model_c_printf(
            &b,
            "((isnan(%s) || isnan(%s) || fabs(%s) <= 1e-12) ? 0.0 : (((%s) - (%s)) / (%s)) * 100.0)",
            current, previous, previous, current, previous, previous);
        free(current);
        free(previous);
        if (b.oom) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return b.data;
    }

    if ((cxpr_model_names_match(name, "min") || cxpr_model_names_match(name, "max")) &&
        argc == 2u) {
        char* left;
        char* right;
        const char* op = cxpr_model_names_match(name, "min") ? "<" : ">";
        if (handled) *handled = true;
        left = cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, 0u),
                                       lookback_offset, &target, err);
        right = left ? cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, 1u),
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

    if (cxpr_model_names_match(name, "clamp") && argc == 3u) {
        if (handled) *handled = true;
        cxpr_model_c_puts(&b, "cxpr_model_clamp_c(");
        for (size_t i = 0u; i < argc; ++i) {
            char* arg = cxpr_expr_ast_to_c_at_offset(
                cxpr_expr_ast_call_arg(ast, i), lookback_offset, &target, err);
            if (!arg) {
                free(b.data);
                return NULL;
            }
            if (i > 0u) cxpr_model_c_puts(&b, ", ");
            cxpr_model_c_puts(&b, arg);
            free(arg);
        }
        cxpr_model_c_puts(&b, ")");
        if (b.oom) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                 "Out of memory", 0, 0);
            return NULL;
        }
        return b.data;
    }

    if (cxpr_model_names_match(name, "mean") && argc >= 1u && argc <= 8u) {
        if (handled) *handled = true;
        cxpr_model_c_puts(&b, "((");
        for (size_t i = 0u; i < argc; ++i) {
            char* arg = cxpr_expr_ast_to_c_at_offset(
                cxpr_expr_ast_call_arg(ast, i), lookback_offset, &target, err);
            if (!arg) {
                free(b.data);
                return NULL;
            }
            if (i > 0u) cxpr_model_c_puts(&b, " + ");
            cxpr_model_c_printf(&b, "(%s)", arg);
            free(arg);
        }
        cxpr_model_c_printf(&b, ") / %.1f)", (double)argc);
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
        cond = cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, 0u),
                                       lookback_offset, &target, err);
        yes = cond ? cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, 1u),
                                             lookback_offset, &target, err) : NULL;
        no = yes ? cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, 2u),
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
            if (target_data->inline_defined_functions ||
                cxpr_model_defined_body_uses_view_syntax(entry->defined_body)) {
                char** names = NULL;
                char** exprs = NULL;
                const cxpr_expr_ast** param_asts = NULL;
                cxpr_model_ast_c_target inline_data = *target_data;
                cxpr_c_target inline_target = {
                    .api_version = CXPR_C_TARGET_API_VERSION,
                    .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
                    .emit_call_at_offset = cxpr_model_ast_c_emit_call,
                    .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
                    .userdata = &inline_data,
                };
                char* expr;
                names = (char**)calloc(argc ? argc : 1u, sizeof(char*));
                exprs = (char**)calloc(argc ? argc : 1u, sizeof(char*));
                param_asts = (const cxpr_expr_ast**)calloc(
                    argc ? argc : 1u, sizeof(*param_asts));
                if (!names || !exprs || !param_asts) {
                    free(names);
                    free(exprs);
                    free(param_asts);
                    if (err) {
                        err->code = CXPR_ERR_OUT_OF_MEMORY;
                        err->message = "Out of memory";
                    }
                    return NULL;
                }
                for (size_t i = 0u; i < argc; ++i) {
                    names[i] = entry->defined_param_names[i];
                    param_asts[i] = cxpr_expr_ast_call_arg(ast, i);
                    exprs[i] = cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, i),
                                                       lookback_offset, &target, err);
                    if (!exprs[i]) {
                        for (size_t j = 0u; j < i; ++j) free(exprs[j]);
                        free(exprs);
                        free(names);
                        free(param_asts);
                        return NULL;
                    }
                }
                inline_data.param_names = names;
                inline_data.param_exprs = exprs;
                inline_data.param_asts = param_asts;
                inline_data.param_ast_count = argc;
                inline_data.param_count = argc;
                expr = cxpr_expr_ast_to_c_at_offset(entry->defined_body,
                                               lookback_offset,
                                               &inline_target,
                                               err);
                for (size_t i = 0u; i < argc; ++i) free(exprs[i]);
                free(exprs);
                free(names);
                free(param_asts);
                if (!expr) return NULL;
                cxpr_model_c_printf(&b, "(%s)", expr);
                free(expr);
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
                arg = cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, i),
                                              lookback_offset, &target, err);
                if (!arg) {
                    free(b.data);
                    return NULL;
                }
                cxpr_model_c_puts(&b, arg);
                free(arg);
            }
            {
                bool wrote_arg = argc > 0u;
                for (size_t i = 0u; i < target_data->program->constant_count; ++i) {
                    if (!cxpr_model_c_defined_function_captures_param(
                            target_data->program, entry, i)) continue;
                    if (wrote_arg) cxpr_model_c_puts(&b, ", ");
                    if (target_data->literal_param_values &&
                        i < target_data->literal_param_count) {
                        char raw[64];
                        cxpr_model_c_format_double(
                            raw, sizeof(raw), target_data->literal_param_values[i]);
                        cxpr_model_c_puts(&b, raw);
                    } else {
                        cxpr_model_c_printf(&b, "_cx_param_%zu", i);
                    }
                    wrote_arg = true;
                }
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

char* cxpr_model_ast_expr_to_c(const cxpr_model_compiled* program,
                                      const cxpr_expr_ast* ast,
                                      const char* function_prefix,
                                      const double* literal_param_values,
                                      size_t literal_param_count,
                                      char** child_call_keys,
                                      size_t* child_call_child_indices,
                                      size_t child_call_count,
                                      cxpr_error* err) {
    cxpr_model_ast_c_target userdata = {
        .program = program,
        .function_prefix = function_prefix,
        .literal_param_values = literal_param_values,
        .literal_param_count = literal_param_count,
        .child_call_keys = child_call_keys,
        .child_call_child_indices = child_call_child_indices,
        .child_call_count = child_call_count,
    };
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
        .userdata = &userdata,
    };
    return cxpr_expr_ast_to_c(ast, &target, err);
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
                                                   const cxpr_expr_ast* ast,
                                                   cxpr_error* err) {
    int op = cxpr_expr_ast_operator(ast);
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
        return cxpr_expr_ast_to_c(ast, emit ? emit->target : NULL, err);
    }

    left = cxpr_model_ast_expr_to_c_with_temps(emit, cxpr_expr_ast_binary_left(ast), err);
    right = left ? cxpr_model_ast_expr_to_c_with_temps(emit, cxpr_expr_ast_binary_right(ast), err) : NULL;
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

char* cxpr_model_ast_expr_to_c_with_temps(cxpr_model_ast_temp_emit* emit,
                                                 const cxpr_expr_ast* ast,
                                                 cxpr_error* err) {
    if (!emit || !ast) return NULL;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) {
        const char* name = cxpr_expr_ast_call_name(ast);
        const size_t argc = cxpr_expr_ast_call_arg_count(ast);
        if ((cxpr_model_names_match(name, "min") || cxpr_model_names_match(name, "max")) &&
            argc == 2u) {
            const char* op = cxpr_model_names_match(name, "min") ? "<" : ">";
            char* left = cxpr_model_ast_expr_to_c_with_temps(
                emit, cxpr_expr_ast_call_arg(ast, 0u), err);
            char* right = left ? cxpr_model_ast_expr_to_c_with_temps(
                emit, cxpr_expr_ast_call_arg(ast, 1u), err) : NULL;
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
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_BINARY_OP) {
        return cxpr_model_ast_binary_to_c_with_temps(emit, ast, err);
    }
    return cxpr_expr_ast_to_c(ast, emit->target, err);
}
