#include "model/codegen/ast/internal.h"
#include "model/window/window.h"
#include "registry/internal.h"

#include <cxpr/resample.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool cxpr_model_c_collect_defined_function_refs(
    const cxpr_model_compiled* program,
    const cxpr_expr_ast* ast,
    bool* used,
    cxpr_error* err);

bool cxpr_model_c_defined_function_captures_param(
    const cxpr_model_compiled* program,
    const cxpr_func_entry* entry,
    size_t param_index) {
    if (!program || !entry || !entry->defined_body ||
        param_index >= program->constant_count) return false;
    return cxpr_expr_ast_contains_reference(
               entry->defined_body, program->constants[param_index].name) ||
           cxpr_expr_ast_contains_variable(
               entry->defined_body, program->constants[param_index].name);
}

static bool cxpr_model_c_defined_function_used(const cxpr_model_compiled* program,
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

static bool cxpr_model_c_collect_defined_function_refs(const cxpr_model_compiled* program,
                                                       const cxpr_expr_ast* ast,
                                                       bool* used,
                                                       cxpr_error* err) {
    if (!ast) return true;
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_c_collect_defined_function_refs(program, cxpr_expr_ast_binary_left(ast),
                                                          used, err) &&
               cxpr_model_c_collect_defined_function_refs(program, cxpr_expr_ast_binary_right(ast),
                                                          used, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_c_collect_defined_function_refs(program, cxpr_expr_ast_unary_operand(ast),
                                                          used, err);
    case CXPR_NODE_FUNCTION_CALL: {
        const char* name = cxpr_expr_ast_call_name(ast);
        size_t argc = cxpr_expr_ast_call_arg_count(ast);
        if (!cxpr_model_c_defined_function_used(program, name, used, err)) return false;
        for (size_t i = 0u; i < argc; ++i) {
            if (!cxpr_model_c_collect_defined_function_refs(
                    program, cxpr_expr_ast_call_arg(ast, i), used, err)) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_PRODUCER_ACCESS:
        {
            cxpr_func_entry* entry = program && program->registry
                ? cxpr_registry_find(program->registry, cxpr_expr_ast_producer_name(ast))
                : NULL;
            if (entry && !entry->model_producer && entry->defined_return_field_count > 0u) {
                const char* field = cxpr_expr_ast_producer_field(ast);
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
        for (size_t i = 0u; i < cxpr_expr_ast_producer_arg_count(ast); ++i) {
            if (!cxpr_model_c_collect_defined_function_refs(
                    program, cxpr_expr_ast_producer_arg(ast, i), used, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_INDEX:
        return cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_expr_ast_index_target(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_expr_ast_index_expression(ast), used, err);
    case CXPR_NODE_TERNARY:
        return cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_expr_ast_ternary_condition(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_expr_ast_ternary_true(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_expr_ast_ternary_false(ast), used, err);
    default:
        return true;
    }
}

static char* cxpr_model_ast_defined_fn_to_c(const cxpr_model_compiled* program,
                                            const cxpr_func_entry* entry,
                                            const char* function_prefix,
                                            cxpr_error* err) {
    cxpr_model_c_buf b = {0};
    char* fn_name;
    cxpr_model_ast_c_target userdata = {0};
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
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
    /* A model-defined function may close over model parameters (for example
     * `fn sadd(a, b) = min(a + b, $INF)`). Pass those captures explicitly so
     * the standalone helper is valid C and retains the tick's parameter view. */
    {
        bool wrote_arg = entry->defined_param_count > 0u;
        for (size_t i = 0u; i < program->constant_count; ++i) {
            if (!cxpr_model_c_defined_function_captures_param(program, entry, i)) continue;
            if (wrote_arg) cxpr_model_c_puts(&b, ", ");
            cxpr_model_c_printf(&b, "double _cx_param_%zu", i);
            wrote_arg = true;
        }
    }
    if (entry->defined_param_count == 0u) {
        bool has_capture = false;
        for (size_t i = 0u; i < program->constant_count; ++i)
            has_capture = has_capture ||
                cxpr_model_c_defined_function_captures_param(program, entry, i);
        if (!has_capture) cxpr_model_c_puts(&b, "void");
    }
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

bool cxpr_model_c_emit_defined_functions_ast(const cxpr_model_compiled* program,
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
