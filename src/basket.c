/**
 * @file basket.c
 * @brief Basket aggregate helpers layered on top of cxpr.
 */

#include "core.h"
#include <cxpr/basket.h>
#include <limits.h>
#include <math.h>
#include <string.h>

static double cxpr_basket_nan(void) {
    return NAN;
}

typedef struct {
    size_t bound_count;
    double* values;
    size_t value_count;
} cxpr_basket_role_binding;

static void cxpr_basket_role_binding_clear(cxpr_basket_role_binding* binding) {
    if (!binding) return;
    free(binding->values);
    binding->values = NULL;
    binding->bound_count = 0;
    binding->value_count = 0;
}

static char* cxpr_basket_role_struct_name(const char* role) {
    const char* prefix = "__dynasty_role_";
    const size_t prefix_len = strlen(prefix);
    const size_t role_len = role ? strlen(role) : 0;
    char* name = (char*)malloc(prefix_len + role_len + 1);
    if (!name) return NULL;
    memcpy(name, prefix, prefix_len);
    if (role_len > 0) memcpy(name + prefix_len, role, role_len);
    name[prefix_len + role_len] = '\0';
    return name;
}

bool cxpr_basket_is_builtin(const char* name) {
    if (!name) return false;
    return strcmp(name, "avg") == 0 ||
           strcmp(name, "any") == 0 ||
           strcmp(name, "all") == 0 ||
           strcmp(name, "min") == 0 ||
           strcmp(name, "max") == 0 ||
           strcmp(name, "count") == 0;
}

bool cxpr_basket_is_aggregate_function(const char* name, size_t argc) {
    return argc == 1 && cxpr_basket_is_builtin(name) && strcmp(name, "count") != 0;
}

static bool cxpr_basket_load_role_binding(const cxpr_context* ctx,
                                          const char* role,
                                          cxpr_basket_role_binding* out) {
    const cxpr_struct_value* data;
    char* key;
    size_t i;

    if (!ctx || !role || !out) return false;
    key = cxpr_basket_role_struct_name(role);
    if (!key) return false;
    data = cxpr_context_get_struct(ctx, key);
    free(key);
    if (!data) return false;

    memset(out, 0, sizeof(*out));
    for (i = 0; i < data->field_count; ++i) {
        const char* field_name = data->field_names[i];
        const cxpr_value field_value = data->field_values[i];
        if (!field_name || field_value.type != CXPR_VALUE_NUMBER) continue;
        if (strcmp(field_name, "bound_count") == 0) {
            out->bound_count = field_value.d > 0.0 ? (size_t)field_value.d : 0;
            continue;
        }
        if (strcmp(field_name, "value_count") == 0) {
            continue;
        }
        if (field_name[0] == 'v' && field_name[1] != '\0') {
            double* values = (double*)realloc(out->values, sizeof(double) * (out->value_count + 1));
            if (!values) {
                cxpr_basket_role_binding_clear(out);
                return false;
            }
            out->values = values;
            out->values[out->value_count++] = field_value.d;
        }
    }
    return true;
}

static bool cxpr_basket_merge_unique(char*** names, size_t* count, const char* name) {
    size_t i;
    char** resized;

    if (!names || !count || !name) return false;
    for (i = 0; i < *count; ++i) {
        if ((*names)[i] && strcmp((*names)[i], name) == 0) return true;
    }
    resized = (char**)realloc(*names, sizeof(char*) * (*count + 1));
    if (!resized) return false;
    *names = resized;
    (*names)[*count] = cxpr_strdup(name);
    if (!(*names)[*count]) return false;
    (*count)++;
    return true;
}

static bool cxpr_basket_collect_free_roles(const cxpr_ast* node,
                                           const cxpr_context* ctx,
                                           char*** names,
                                           size_t* count) {
    size_t i;

    if (!node || !names || !count) return true;

    switch (cxpr_ast_type(node)) {
        case CXPR_NODE_VARIABLE: {
            const char* role = cxpr_ast_variable_name(node);
            cxpr_basket_role_binding binding = {0};
            bool ok = cxpr_basket_load_role_binding(ctx, role, &binding);
            if (!ok || binding.bound_count <= 1) {
                cxpr_basket_role_binding_clear(&binding);
                return true;
            }
            if (binding.bound_count > 1) {
                ok = cxpr_basket_merge_unique(names, count, role);
            }
            cxpr_basket_role_binding_clear(&binding);
            return ok;
        }
        case CXPR_NODE_BINARY_OP:
            return cxpr_basket_collect_free_roles(cxpr_ast_left(node), ctx, names, count) &&
                   cxpr_basket_collect_free_roles(cxpr_ast_right(node), ctx, names, count);
        case CXPR_NODE_UNARY_OP:
            return cxpr_basket_collect_free_roles(cxpr_ast_operand(node), ctx, names, count);
        case CXPR_NODE_LOOKBACK:
            return cxpr_basket_collect_free_roles(cxpr_ast_lookback_target(node), ctx, names, count) &&
                   cxpr_basket_collect_free_roles(cxpr_ast_lookback_index(node), ctx, names, count);
        case CXPR_NODE_TERNARY:
            return cxpr_basket_collect_free_roles(cxpr_ast_ternary_condition(node), ctx, names, count) &&
                   cxpr_basket_collect_free_roles(cxpr_ast_ternary_true_branch(node), ctx, names, count) &&
                   cxpr_basket_collect_free_roles(cxpr_ast_ternary_false_branch(node), ctx, names, count);
        case CXPR_NODE_FUNCTION_CALL: {
            const char* fn = cxpr_ast_function_name(node);
            const size_t argc = cxpr_ast_function_argc(node);
            if (argc == 1 && cxpr_basket_is_builtin(fn)) return true;
            for (i = 0; i < argc; ++i) {
                if (!cxpr_basket_collect_free_roles(cxpr_ast_function_arg(node, i), ctx, names, count)) return false;
            }
            return true;
        }
        case CXPR_NODE_PRODUCER_ACCESS: {
            const size_t argc = cxpr_ast_producer_argc(node);
            for (i = 0; i < argc; ++i) {
                if (!cxpr_basket_collect_free_roles(cxpr_ast_producer_arg(node, i), ctx, names, count)) return false;
            }
            return true;
        }
        default:
            return true;
    }
}

static void cxpr_basket_free_names(char** names, size_t count) {
    size_t i;
    if (!names) return;
    for (i = 0; i < count; ++i) free(names[i]);
    free(names);
}

static cxpr_value cxpr_basket_eval_error(cxpr_error* err, const char* message);

static bool cxpr_basket_value_truthy(cxpr_value value) {
    return value.type == CXPR_VALUE_BOOL ? value.b :
           value.type == CXPR_VALUE_NUMBER ? (value.d != 0.0) :
           false;
}

static cxpr_value cxpr_basket_eval_folded_results(const char* fn,
                                                  const cxpr_value* results,
                                                  size_t count,
                                                  cxpr_error* err) {
    size_t i;
    double numeric;

    if (!fn || !results || count == 0) {
        return cxpr_basket_eval_error(err, "Unsupported basket builtin");
    }

    if (strcmp(fn, "avg") == 0) {
        numeric = 0.0;
        for (i = 0; i < count; ++i) {
            if (results[i].type != CXPR_VALUE_NUMBER) {
                return cxpr_basket_eval_error(err, "avg() requires numeric results");
            }
            numeric += results[i].d;
        }
        return cxpr_fv_double(numeric / (double)count);
    }

    if (strcmp(fn, "any") == 0) {
        for (i = 0; i < count; ++i) {
            if (cxpr_basket_value_truthy(results[i])) return cxpr_fv_bool(true);
        }
        return cxpr_fv_bool(false);
    }

    if (strcmp(fn, "all") == 0) {
        for (i = 0; i < count; ++i) {
            if (!cxpr_basket_value_truthy(results[i])) return cxpr_fv_bool(false);
        }
        return cxpr_fv_bool(true);
    }

    if (strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0) {
        if (results[0].type != CXPR_VALUE_NUMBER) {
            return cxpr_basket_eval_error(err, "min()/max() basket aggregation requires numeric results");
        }
        numeric = results[0].d;
        for (i = 1; i < count; ++i) {
            if (results[i].type != CXPR_VALUE_NUMBER) {
                return cxpr_basket_eval_error(err, "min()/max() basket aggregation requires numeric results");
            }
            numeric = (strcmp(fn, "min") == 0)
                ? ((results[i].d < numeric) ? results[i].d : numeric)
                : ((results[i].d > numeric) ? results[i].d : numeric);
        }
        return cxpr_fv_double(numeric);
    }

    return cxpr_basket_eval_error(err, "Unsupported basket builtin");
}

static bool cxpr_basket_eval_direct_args(const cxpr_ast* call_ast,
                                         const cxpr_context* ctx,
                                         const cxpr_registry* reg,
                                         const char* fn,
                                         cxpr_error* err,
                                         cxpr_value* out) {
    size_t argc = cxpr_ast_function_argc(call_ast);
    size_t i;

    if (!out) return false;

    if (argc == 0) {
        if (strcmp(fn, "avg") == 0) {
            *out = cxpr_basket_eval_error(err, "avg() expects at least one argument");
            return false;
        }
        *out = cxpr_basket_eval_error(err, "Basket builtin expects exactly one argument");
        return false;
    }

    {
        cxpr_value* values = (cxpr_value*)malloc(sizeof(cxpr_value) * argc);
        if (!values) {
            *out = cxpr_basket_eval_error(err, "Out of memory");
            return false;
        }

        for (i = 0; i < argc; ++i) {
            double number = 0.0;
            if (!cxpr_eval_ast_number(cxpr_ast_function_arg(call_ast, i), ctx, reg, &number, err)) {
                free(values);
                *out = cxpr_fv_double(cxpr_basket_nan());
                return false;
            }
            values[i] = cxpr_fv_double(number);
        }

        *out = cxpr_basket_eval_folded_results(fn, values, argc, err);
        free(values);
        return true;
    }
}

static void cxpr_basket_cleanup_expanded_call(cxpr_value* results,
                                              cxpr_basket_role_binding* binding,
                                              char** free_roles,
                                              size_t free_role_count) {
    free(results);
    cxpr_basket_role_binding_clear(binding);
    cxpr_basket_free_names(free_roles, free_role_count);
}

static bool cxpr_basket_ast_uses_aggregates_impl(const cxpr_ast* ast) {
    size_t i;

    if (!ast) return false;
    switch (cxpr_ast_type(ast)) {
        case CXPR_NODE_FUNCTION_CALL: {
            const char* name = cxpr_ast_function_name(ast);
            const size_t argc = cxpr_ast_function_argc(ast);
            if (argc == 1 && cxpr_basket_is_builtin(name)) return true;
            for (i = 0; i < argc; ++i) {
                if (cxpr_basket_ast_uses_aggregates_impl(cxpr_ast_function_arg(ast, i))) return true;
            }
            return false;
        }
        case CXPR_NODE_BINARY_OP:
            return cxpr_basket_ast_uses_aggregates_impl(cxpr_ast_left(ast)) ||
                   cxpr_basket_ast_uses_aggregates_impl(cxpr_ast_right(ast));
        case CXPR_NODE_UNARY_OP:
            return cxpr_basket_ast_uses_aggregates_impl(cxpr_ast_operand(ast));
        case CXPR_NODE_LOOKBACK:
            return cxpr_basket_ast_uses_aggregates_impl(cxpr_ast_lookback_target(ast)) ||
                   cxpr_basket_ast_uses_aggregates_impl(cxpr_ast_lookback_index(ast));
        case CXPR_NODE_TERNARY:
            return cxpr_basket_ast_uses_aggregates_impl(cxpr_ast_ternary_condition(ast)) ||
                   cxpr_basket_ast_uses_aggregates_impl(cxpr_ast_ternary_true_branch(ast)) ||
                   cxpr_basket_ast_uses_aggregates_impl(cxpr_ast_ternary_false_branch(ast));
        case CXPR_NODE_PRODUCER_ACCESS:
            for (i = 0; i < cxpr_ast_producer_argc(ast); ++i) {
                if (cxpr_basket_ast_uses_aggregates_impl(cxpr_ast_producer_arg(ast, i))) return true;
            }
            return false;
        default:
            return false;
    }
}

static cxpr_value cxpr_basket_eval_error(cxpr_error* err, const char* message) {
    if (err) {
        err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
        err->message = message;
    }
    return cxpr_fv_double(cxpr_basket_nan());
}

static cxpr_value cxpr_basket_eval_call(const cxpr_ast* call_ast,
                                        const cxpr_context* ctx,
                                        const cxpr_registry* reg,
                                        void* userdata,
                                        cxpr_error* err) {
    const char* fn;
    size_t argc;
    size_t i;
    cxpr_basket_role_binding binding;
    cxpr_value direct;

    (void)userdata;
    fn = cxpr_ast_function_name(call_ast);
    argc = cxpr_ast_function_argc(call_ast);

    if (argc != 1) {
        if (fn && (strcmp(fn, "avg") == 0 || strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0)) {
            if (!cxpr_basket_eval_direct_args(call_ast, ctx, reg, fn, err, &direct)) {
                return direct;
            }
            return direct;
        }
        return cxpr_basket_eval_error(err, "Basket builtin expects exactly one argument");
    }

    if (fn && strcmp(fn, "count") == 0) {
        const cxpr_ast* arg_ast = cxpr_ast_function_arg(call_ast, 0);
        const char* role = cxpr_ast_variable_name(arg_ast);
        double bound_count;
        if (!arg_ast || cxpr_ast_type(arg_ast) != CXPR_NODE_VARIABLE || !role) {
            return cxpr_basket_eval_error(err, "count() requires a role variable like count($pair)");
        }
        if (!cxpr_basket_load_role_binding(ctx, role, &binding)) {
            return cxpr_basket_eval_error(err, "count() requires a bound role variable");
        }
        bound_count = (double)binding.bound_count;
        cxpr_basket_role_binding_clear(&binding);
        return cxpr_fv_double(bound_count);
    }

    {
        const cxpr_ast* arg_ast = cxpr_ast_function_arg(call_ast, 0);
        char** free_roles = NULL;
        size_t free_role_count = 0;
        cxpr_value result = {0};
        bool ok;

        ok = cxpr_basket_collect_free_roles(arg_ast, ctx, &free_roles, &free_role_count);
        if (!ok) {
            cxpr_basket_free_names(free_roles, free_role_count);
            return cxpr_fv_double(cxpr_basket_nan());
        }
        if (free_role_count == 0) {
            ok = cxpr_eval_ast(arg_ast, ctx, reg, &result, err);
            cxpr_basket_free_names(free_roles, free_role_count);
            if (!ok) return cxpr_fv_double(cxpr_basket_nan());
            return result;
        }
        if (free_role_count != 1) {
            cxpr_basket_free_names(free_roles, free_role_count);
            return cxpr_basket_eval_error(err, "Basket aggregate can only expand one role at a time");
        }

        if (!cxpr_basket_load_role_binding(ctx, free_roles[0], &binding)) {
            cxpr_basket_free_names(free_roles, free_role_count);
            return cxpr_basket_eval_error(err, "Basket aggregate requires a bound role variable");
        }
        if (binding.value_count == 0) {
            cxpr_basket_cleanup_expanded_call(NULL, &binding, free_roles, free_role_count);
            return cxpr_fv_double(0.0);
        }

        {
            cxpr_value* results = (cxpr_value*)malloc(sizeof(cxpr_value) * binding.value_count);
            if (!results) {
                cxpr_basket_role_binding_clear(&binding);
                cxpr_basket_free_names(free_roles, free_role_count);
                return cxpr_basket_eval_error(err, "Out of memory");
            }

            for (i = 0; i < binding.value_count; ++i) {
                cxpr_context* overlay = cxpr_context_overlay_new(ctx);
                if (!overlay) {
                    cxpr_basket_cleanup_expanded_call(results, &binding, free_roles, free_role_count);
                    return cxpr_basket_eval_error(err, "Out of memory");
                }
                cxpr_context_set_param(overlay, free_roles[0], binding.values[i]);
                ok = cxpr_eval_ast(arg_ast, overlay, reg, &results[i], err);
                cxpr_context_free(overlay);
                if (!ok) {
                    cxpr_basket_cleanup_expanded_call(results, &binding, free_roles, free_role_count);
                    return cxpr_fv_double(cxpr_basket_nan());
                }
            }

            result = cxpr_basket_eval_folded_results(fn, results, binding.value_count, err);
            cxpr_basket_cleanup_expanded_call(results, &binding, free_roles, free_role_count);
            return result;
        }

        cxpr_basket_cleanup_expanded_call(NULL, &binding, free_roles, free_role_count);
        return cxpr_basket_eval_error(err, "Unsupported basket builtin");
    }
}

void cxpr_register_basket_builtins(cxpr_registry* reg) {
    if (!reg) return;
    cxpr_registry_add_ast(reg, "avg", cxpr_basket_eval_call, 1, 1, CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_registry_add_ast(reg, "any", cxpr_basket_eval_call, 1, 1, CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_add_ast(reg, "all", cxpr_basket_eval_call, 1, 1, CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_add_ast(reg, "count", cxpr_basket_eval_call, 1, 1, CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_registry_add_ast(reg, "min", cxpr_basket_eval_call, 1, 8, CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_registry_add_ast(reg, "max", cxpr_basket_eval_call, 1, 8, CXPR_VALUE_NUMBER, NULL, NULL);
}

bool cxpr_ast_uses_basket_aggregates(const cxpr_ast* ast) {
    return cxpr_basket_ast_uses_aggregates_impl(ast);
}

bool cxpr_expression_uses_basket_aggregates(const char* source) {
    cxpr_parser* parser;
    cxpr_error err = {0};
    cxpr_ast* ast;
    bool uses;

    if (!source) return false;
    parser = cxpr_parser_new();
    if (!parser) return false;
    ast = cxpr_parse(parser, source, &err);
    if (!ast) {
        cxpr_parser_free(parser);
        return false;
    }
    uses = cxpr_ast_uses_basket_aggregates(ast);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    return uses;
}
